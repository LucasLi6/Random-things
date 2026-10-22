/*
 * BLE 接收端（peripheral / GATT server）—— 同时服务两个控制端：
 *   - sender（ESP32 主控）：写 0~100 压力等级到特征值 A
 *   - 手机 iOS App（备用）：写 0~100 压力等级到特征值 B
 * 接收端取两路的最大值 max(A, B)，映射成舵机角度输出（0 -> 180°，100 -> 25°），
 * 用 LEDC 输出 50Hz PWM 到 GPIO4。
 *
 * 掉线处理：记住每路对应的 conn_id，某一端断开就只把那一路的贡献清零，
 * 避免"拔掉一端后舵机被残留值卡死"。
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define LED_PIN       8                                    // onboard blue LED (active-low)
#define DEVICE_NAME   "ESP32-LED-RX"                       // advertised name
#define SERVICE_UUID  "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

// 特征值 A：sender 写 0~100 压力（UUID 保持不变，sender 代码一行都不用改）
#define CHAR_SENDER_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
// 特征值 B：手机 iOS App 写 0~100 压力（备用控制器）
#define CHAR_PHONE_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a9"

// ---- 舵机（GPIO4，LEDC 输出 50Hz PWM）----
#define SERVO_PIN     4                                    // 舵机信号线
#define SERVO_CH      0                                    // LEDC 通道
#define SERVO_FREQ    50                                   // 50Hz = 20ms 周期
#define SERVO_RES     14                                   // ESP32-C3 的 LEDC 最大 14 位

// 舵机标定（可按你的舵机实测调整）：
#define SERVO_MIN_US  500                                   // 0°   脉冲宽度
#define SERVO_MAX_US  2600                                  // 180° 脉冲宽度（你实测 2.6ms）

// 两路最新压力值（0~100），默认 0 = 无贡献
static uint8_t g_senderLevel = 0;
static uint8_t g_phoneLevel  = 0;

// 每路对应的 BLE conn_id（0xFFFF = 尚未记录）
static uint16_t g_senderConnId = 0xFFFF;
static uint16_t g_phoneConnId  = 0xFFFF;

// 0~100 压力 -> 舵机角度（连续线性，反向：0=张开 180°，100=闭合 25°）
int levelToAngle(int level) {
  if (level < 0) level = 0;
  if (level > 100) level = 100;
  return 180 - (level * 155) / 100;   // 155 = 180 - 25
}

// 角度 -> 脉冲宽度(us)
int angleToPulseUs(int angle) {
  return (int)(SERVO_MIN_US + (angle / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US));
}

// 把脉冲宽度写到舵机（LEDC 14 位 @ 50Hz）
void servoWriteUs(int us) {
  const uint32_t maxDuty = (1u << SERVO_RES) - 1;           // 16383 = 20ms
  uint32_t duty = (uint32_t)((uint64_t)us * maxDuty / 20000u);
  ledcWrite(SERVO_CH, duty);
}

// 两路取最大，映射成脉冲写舵机
void updateServo() {
  uint8_t level = (g_senderLevel > g_phoneLevel) ? g_senderLevel : g_phoneLevel;
  int angle = levelToAngle(level);
  int us = angleToPulseUs(angle);
  servoWriteUs(us);
  Serial.print("RX: sender=");
  Serial.print(g_senderLevel);
  Serial.print(" phone=");
  Serial.print(g_phoneLevel);
  Serial.print(" -> level=");
  Serial.print(level);
  Serial.print(" angle=");
  Serial.print(angle);
  Serial.print(" pulse=");
  Serial.print(us);
  Serial.println("us");
}

// 收到 sender 写进来的 0~100 压力
class SenderCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic,
               esp_ble_gatts_cb_param_t *param) override {
    std::string value = pCharacteristic->getValue();
    if (!value.empty()) {
      g_senderLevel = (uint8_t)value[0];
      g_senderConnId = param->write.conn_id;
      updateServo();
    }
  }
};
static SenderCallbacks senderCallbacks;

// 收到手机写进来的 0~100 压力
class PhoneCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic,
               esp_ble_gatts_cb_param_t *param) override {
    std::string value = pCharacteristic->getValue();
    if (!value.empty()) {
      g_phoneLevel = (uint8_t)value[0];
      g_phoneConnId = param->write.conn_id;
      updateServo();
    }
  }
};
static PhoneCallbacks phoneCallbacks;

// 连接计数：两个都断开才算"无连接"
static int g_connCount = 0;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    g_connCount++;
    digitalWrite(LED_PIN, LOW);               // 连上：LED 常亮
    Serial.print("RX: connected (");
    Serial.print(g_connCount);
    Serial.println(")");
  }
  void onDisconnect(BLEServer *pServer) override {
    if (g_connCount > 0) g_connCount--;
    if (g_connCount <= 0) {
      g_connCount = 0;
      digitalWrite(LED_PIN, HIGH);
      BLEDevice::getAdvertising()->start();
      Serial.println("RX: all disconnected, re-advertising");
    }
  }
  // 带 conn_id 的重载：精确清掉掉线那一路的贡献
  void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    onConnect(pServer);
  }
  void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    uint16_t id = param->disconnect.conn_id;
    if (id == g_senderConnId) {
      g_senderLevel = 0;
      g_senderConnId = 0xFFFF;
      Serial.println("RX: sender gone, cleared");
    }
    if (id == g_phoneConnId) {
      g_phoneLevel = 0;
      g_phoneConnId = 0xFFFF;
      Serial.println("RX: phone gone, cleared");
    }
    updateServo();
    onDisconnect(pServer);
  }
};
static ServerCallbacks serverCallbacks;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);              // start off

  // 舵机 PWM 初始化，上电默认两路都是 0 -> max=0 -> 180°（2600us）
  ledcSetup(SERVO_CH, SERVO_FREQ, SERVO_RES);
  ledcAttachPin(SERVO_PIN, SERVO_CH);
  updateServo();
  Serial.print("RX: servo ready on GPIO");
  Serial.println(SERVO_PIN);

  BLEDevice::init(DEVICE_NAME);

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pSenderChar = pService->createCharacteristic(
      CHAR_SENDER_UUID, BLECharacteristic::PROPERTY_WRITE_NR);
  pSenderChar->setCallbacks(&senderCallbacks);

  BLECharacteristic *pPhoneChar = pService->createCharacteristic(
      CHAR_PHONE_UUID, BLECharacteristic::PROPERTY_WRITE_NR);
  pPhoneChar->setCallbacks(&phoneCallbacks);

  pService->start();

  // 名字放在主广播包中，最容易被扫描到
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();

  Serial.println("RX: advertising, waiting for sender + phone...");
}

void loop() {
  // 无连接时蓝灯心跳闪烁（约 2Hz）；有连接时由 onConnect 设为常亮。
  if (g_connCount == 0) {
    static unsigned long lastToggle = 0;
    static bool on = false;
    if (millis() - lastToggle >= 250) {
      lastToggle = millis();
      on = !on;
      digitalWrite(LED_PIN, on ? LOW : HIGH);   // 板载蓝灯低电平点亮
    }
  }
}
