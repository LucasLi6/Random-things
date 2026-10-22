/*
 * BLE receiver (peripheral / GATT server) —— 独立项目，本文件夹 = 接收端
 *
 * 广播自己的名字，等发送端自动连上。
 * 连上后 LED 常亮，断开则灭掉；收到发送端写进来的 0~100 压力等级后，
 * 把它映射成舵机角度：0 -> 180°(2.6ms)，100 -> 45°，并用 LEDC 输出到 GPIO4。
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define LED_PIN       8                                    // onboard blue LED (active-low)
#define DEVICE_NAME   "ESP32-LED-RX"                       // advertised name
#define SERVICE_UUID  "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ---- 舵机（GPIO4，LEDC 输出 50Hz PWM）----
#define SERVO_PIN     4                                    // 舵机信号线
#define SERVO_CH      0                                    // LEDC 通道
#define SERVO_FREQ    50                                   // 50Hz = 20ms 周期
#define SERVO_RES     14                                   // ESP32-C3 的 LEDC 最大 14 位（16 位会报错）

// 舵机标定（可按你的舵机实测调整）：
#define SERVO_MIN_US  500                                   // 0°   脉冲宽度
#define SERVO_MAX_US  2600                                  // 180° 脉冲宽度（你实测 2.6ms，不是 2.5）

// 6 个力区间 -> 6 个舵机角度（压力越大，角度越小）
// 区间划分：0~19 / 20~39 / 40~59 / 60~79 / 80~99 平分 5 档，只有 100 单独落到 25°。
// 想换 6 个角度就改这一行：索引 0 = 最小力档，索引 5 = 最大力档（只有 100）。
static const int ANGLE_STEPS[6] = {180, 149, 118, 87, 56, 25};

// 0~100 等级 -> 6 档角度 -> 脉冲宽度(us)
int levelToPulseUs(uint8_t value) {
  int idx;
  if (value >= 100) {
    idx = 5;                         // 只有 100 -> 25°
  } else {
    idx = value / 20;                // 0~99 平分 5 档
  }
  int angle = ANGLE_STEPS[idx];
  return (int)(SERVO_MIN_US + (angle / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US));
}

// 把脉冲宽度写到舵机（LEDC 16 位 @ 50Hz）
void servoWriteUs(int us) {
  const uint32_t maxDuty = (1u << SERVO_RES) - 1;           // 16383 = 20ms
  uint32_t duty = (uint32_t)((uint64_t)us * maxDuty / 20000u);
  ledcWrite(SERVO_CH, duty);
}

// 收到发送端写进来的 0~100 压力等级
class LedCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic,
               esp_ble_gatts_cb_param_t *param) override {
    std::string value = pCharacteristic->getValue();
    if (!value.empty()) {
      uint8_t level = (uint8_t)value[0];
      int us = levelToPulseUs(level);
      servoWriteUs(us);
      Serial.print("RX: level=");
      Serial.print(level);
      Serial.print("  pulse=");
      Serial.print(us);
      Serial.println("us");
    }
  }
};
static LedCallbacks ledCallbacks;

// 连接状态：连上 LED 常亮，断开 LED 闪烁 + 重新广播，方便发送端自动重连
static bool g_connected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    g_connected = true;
    digitalWrite(LED_PIN, LOW);               // 连上：LED 常亮
    Serial.println("RX: connected");
  }
  void onDisconnect(BLEServer *pServer) override {
    g_connected = false;
    BLEDevice::getAdvertising()->start();
    Serial.println("RX: disconnected, re-advertising");
  }
};
static ServerCallbacks serverCallbacks;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);              // start off

  // 舵机 PWM 初始化，上电默认 value=0 -> 180°（2600us）
  ledcSetup(SERVO_CH, SERVO_FREQ, SERVO_RES);
  ledcAttachPin(SERVO_PIN, SERVO_CH);
  servoWriteUs(levelToPulseUs(0));
  Serial.print("RX: servo ready on GPIO");
  Serial.print(SERVO_PIN);
  Serial.print(", default pulse=");
  Serial.print(levelToPulseUs(0));
  Serial.println("us (180deg)");

  BLEDevice::init(DEVICE_NAME);

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pChar = pService->createCharacteristic(
      CHAR_UUID, BLECharacteristic::PROPERTY_WRITE_NR);
  pChar->setCallbacks(&ledCallbacks);
  pService->start();

  // 名字放在主广播包中，最容易被扫描到；不要 addServiceUUID / setScanResponse
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();

  Serial.println("RX: advertising, waiting for connection...");
}

void loop() {
  // 未连接时蓝灯心跳闪烁（约 2Hz）；连上后由 onConnect 设为常亮，这里不动。
  if (!g_connected) {
    static unsigned long lastToggle = 0;
    static bool on = false;
    if (millis() - lastToggle >= 250) {
      lastToggle = millis();
      on = !on;
      digitalWrite(LED_PIN, on ? LOW : HIGH);   // 板载蓝灯低电平点亮
    }
  }
}
