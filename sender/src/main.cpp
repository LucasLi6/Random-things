/*
 * 发送端 ESP32-C3 SuperMini
 *
 * 当前模式（ENABLE_BLE = 1）：压力薄膜（FSR）分压法读取 + 蓝牙发送 0~100 等级
 *   - 采样脚 GPIO0 = ADC1 通道0，分压固定电阻 R0
 *   - 串口输出：电压（V / mV）+ 归一化压力等级 0~100%（正比趋势，非精确克数）
 *   - 连上接收端后本机 LED 常亮，断开则均匀频闪（硬件定时器驱动，节奏恒定）
 *   - 把 0~100 压力等级写进特征值发出。
 *   - ENABLE_BLE 改回 0 即只读压力、不启用蓝牙（代码保留，不删除）。
 */

#include <Arduino.h>

// ---------------------------------------------------------------------------
// 主开关：0 = 只读压力、不启用蓝牙；1 = 启用蓝牙，把 0~100 压力等级发给接收端
// ---------------------------------------------------------------------------
#define ENABLE_BLE 1

#if ENABLE_BLE
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <Ticker.h>
#endif

#define LED_PIN         8                          // 板载蓝色 LED（低电平点亮）

// ---------------------------------------------------------------------------
// 压力薄膜（FSR）分压法配置
//   接线：  3V3 ── FSR ──┬── GPIO0 (ADC1_CH0)
//                        └── R0 ── GND
//   Vout = VCC * R0 / (R0 + Rs)   =>   Rs = R0 * (VCC - Vout) / Vout
// ---------------------------------------------------------------------------
#define FSR_PIN       0                           // 采样脚 GPIO0 = ADC1_CH0
                                                  // （别用 GPIO5=ADC2，会和 BLE 冲突）
#define R0_OHM        10000.0                     // 分压固定电阻（欧姆）
#define VCC_MV        3300.0                      // 3V3 引脚电压

// FSR 功率律（RP-C10-ST，100g~2kg 有效）：R(kΩ) = A * F^(-k)  =>  F(g) = (A/R)^(1/k)
#define FSR_A         194.46
#define FSR_K         0.663
#define FSR_FULLSCALE_G 1937.0                    // 100% 对应的力(g) ≈ 19N（你实测的满压）
#define V_MIN_MV      30.0                        // 噪声地板：低于此 = 0%

#if ENABLE_BLE
// ---------------- 蓝牙：连接收端，发送 0~100 压力等级 ----------------
#define DEVICE_NAME     "ESP32-LED-RX"                     // receiver's advertised name
#define SERVICE_UUID    "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a8"

static BLEClient*               pClient = nullptr;
static BLERemoteCharacteristic* pChar   = nullptr;
static BLEAdvertisedDevice*     pTarget = nullptr;

// 连接状态 + 均匀频闪用的硬件定时器
static volatile bool g_connected = false;
static Ticker ledTicker;

// 定时器回调：未连接 = 每 125ms 翻转一次（4Hz 均匀频闪）；已连接 = 常亮
static void ledTick() {
  if (!g_connected) {
    static bool on = false;
    on = !on;
    digitalWrite(LED_PIN, on ? LOW : HIGH);   // 低电平点亮
  } else {
    digitalWrite(LED_PIN, LOW);               // 连上：常亮
  }
}

// Scan callback: log every device, and stop when the target name is found
class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    Serial.print("TX: saw device ");
    if (advertisedDevice.haveName()) {
      Serial.print(advertisedDevice.getName().c_str());
    } else {
      Serial.print("<no name>");
    }
    Serial.print(" addr=");
    Serial.print(advertisedDevice.getAddress().toString().c_str());
    Serial.print(" rssi=");
    Serial.println(advertisedDevice.getRSSI());

    if (advertisedDevice.haveName() &&
        advertisedDevice.getName() == DEVICE_NAME) {
      pTarget = new BLEAdvertisedDevice(advertisedDevice);
      BLEDevice::getScan()->stop();
      Serial.println("TX: target found, stopping scan");
    }
  }
};
static ScanCallbacks scanCallbacks;

bool connectToReceiver() {
  Serial.println("TX: scanning for receiver...");

  BLEScan *pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(&scanCallbacks);
  pScan->setActiveScan(true);
  pScan->start(5, false);                    // blocking scan, 5 s

  if (pTarget == nullptr) {
    Serial.println("TX: receiver not found, retrying...");
    return false;
  }

  if (!pClient->connect(pTarget)) {
    Serial.println("TX: connection failed");
    return false;
  }
  Serial.println("TX: connected");

  BLERemoteService *pService = pClient->getService(SERVICE_UUID);
  if (pService == nullptr) {
    Serial.println("TX: service not found");
    pClient->disconnect();
    return false;
  }

  pChar = pService->getCharacteristic(CHAR_UUID);
  if (pChar == nullptr) {
    Serial.println("TX: characteristic not found");
    pClient->disconnect();
    return false;
  }

  Serial.println("TX: ready, streaming pressure level");
  return true;
}
#endif  // ENABLE_BLE

// ---------------------------------------------------------------------------
// 压力读数
// ---------------------------------------------------------------------------

// 电压 -> 力(g) -> 归一化等级 0~100%（与真实压力成正比，均匀上涨）
float voltageToLevel(float v_mv) {
  if (v_mv <= V_MIN_MV) return 0.0f;
  float rs_ohm = R0_OHM * (VCC_MV - v_mv) / v_mv;   // 分压反推 FSR 电阻
  if (rs_ohm <= 0.0f) return 0.0f;
  float rs_k = rs_ohm / 1000.0f;
  float f = powf(FSR_A / rs_k, 1.0f / FSR_K);       // 电阻 -> 力(g)
  float level = f / FSR_FULLSCALE_G * 100.0f;       // 力 -> 0~100%
  if (level < 0.0f) level = 0.0f;
  if (level > 100.0f) level = 100.0f;
  return level;
}

// 采样 N 次取平均，返回 0~100% 等级；v_mv 带回平均电压(mV)
float readLevel(float &v_mv) {
  const int N = 64;
  long sum = 0;
  for (int i = 0; i < N; i++) {
    sum += analogReadMilliVolts(FSR_PIN);
  }
  v_mv = (float)sum / (float)N;
  return voltageToLevel(v_mv);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);                         // LED 灭

  // ADC1 通道0，11dB 衰减（量程约 0~2.5V）
  analogSetPinAttenuation(FSR_PIN, ADC_11db);

  Serial.println("=== FSR 压力读取（分压法）===");
  Serial.print("R0 = ");
  Serial.print((int)R0_OHM);
  Serial.println(" ohm, 采样脚 GPIO0/ADC1");
  Serial.println("输出: 等级 L (0~100%)  +  电压 V (mV)");

#if ENABLE_BLE
  BLEDevice::init("ESP32-LED-TX");
  pClient = BLEDevice::createClient();
  ledTicker.attach_ms(125, ledTick);            // 125ms 翻转 = 4Hz 均匀频闪
#endif
}

void loop() {
  float v_mv = 0.0f;
  float level = readLevel(v_mv);

#if ENABLE_BLE
  if (!g_connected) {
    if (connectToReceiver()) {
      g_connected = true;                        // LED 由定时器自动改为常亮
    } else {
      delay(1000);
      return;
    }
  }

  if (!pClient->isConnected()) {
    g_connected = false;                         // 掉线：回到均匀频闪
    pChar   = nullptr;
    pTarget = nullptr;
    Serial.println("TX: disconnected");
    return;
  }

  // 连上：把 0~100 压到 1 字节发给接收端
  if (pChar != nullptr) {
    uint8_t val = (uint8_t)constrain((int)level, 0, 100);
    pChar->writeValue(&val, 1);                  // write without response
  }
#endif

  Serial.print("L=");
  Serial.print(level, 1);
  Serial.print("%   V=");
  Serial.print(v_mv / 1000.0f, 3);
  Serial.print(" V (");
  Serial.print((int)v_mv);
  Serial.println(" mV)");

  delay(100);   // 约 10Hz 输出
}
