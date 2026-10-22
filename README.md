# 手机/蓝牙舵机控制系统

ESP32-C3 蓝牙舵机控制：**发送端（主控）** 与 **手机网页（备用）** 同时向 **接收端** 发送 `0~100` 压力值，接收端取两者 **最大值** 驱动舵机。

## 组成

| 目录 | 作用 | 说明 |
| --- | --- | --- |
| `sender/` | 蓝牙发送端（主控） | FSR 压力传感器 → 0~100 → BLE 客户端写入接收端 |
| `receiver/` | 蓝牙接收端（从机） | 同时收 sender 和手机两个 0~100，取最大值 → 舵机 PWM |
| `web/index.html` | 手机网页（备用控制） | Web Bluetooth 拖拽旋钮，写 0~100 给接收端 |
| `ios-app/` | 原生 iOS App（备选） | CoreBluetooth 实现，功能同网页 |

蓝牙服务 UUID：`4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- sender 特征：`beb5483e-36e1-4688-b7f5-ea07361b26a8`
- 手机特征：`beb5483e-36e1-4688-b7f5-ea07361b26a9`

## 烧录（PlatformIO）

1. 安装 [PlatformIO IDE](https://platformio.org/)（VS Code 插件）。
2. 用 PlatformIO **打开 `receiver/` 文件夹**，点 **Build** 后 **Upload** 烧录接收端。
3. 用 PlatformIO **打开 `sender/` 文件夹**，同样烧录发送端。

> 已把平台固定为 `espressif32@6.9.0`（Arduino 2.0 + Bluedroid BLE + 旧 `ledcSetup` API）。
> **不要升级到 7.x**（Arduino 3.0 + NimBLE），现有代码会编译失败。

### 烧录失败怎么办

- 报 `No serial data received` / `Unable to verify flash chip connection`：
  按住板子 **BOOT 键**（GPIO9）再按一下 **RESET** 进入下载模式，然后再点 Upload。
  波特率已固定为 115200，避免 USB 原生口切波特率断连。
- 烧录后一直重启、串口无输出（`SHA-256 comparison failed` + 反复 reset）：
  用 `esptool erase_flash` 清空后，把 `bootloader.bin`(0x0) + `partitions.bin`(0x8000) +
  `firmware.bin`(0x10000) 三个文件完整重烧一遍（不要只烧 firmware.bin 到 0x0）。

## 手机网页（备用控制）

把 `web/index.html` 部署到任意静态托管（GitHub Pages / Netlify）即可。
- **iPhone**：用 [Bluefy](https://apps.apple.com/app/bluefy-web-bluetooth-browser/id1492822055) 浏览器打开。
- **Android**：直接用 Chrome 打开。

连接设备名 `ESP32-LED-RX`，拖拽旋钮即发送 0~100 压力值。
