# 舵机控制 iOS App（备用控制器）

手机作为 **备用控制器**，通过 BLE 连上接收端（`ESP32-LED-RX`），拖拽旋钮写 0~100 压力值。
接收端取「sender 的压力值」和「手机的压力值」的**最大值**驱动舵机，所以谁的值大谁接管。

## 文件

| 文件 | 作用 |
|---|---|
| `BLEManager.swift` | CoreBluetooth 中央管理器：扫描/连接/写特征值 |
| `ServoKnobView.swift` | 可拖拽旋钮，把角度映射成 0~100 |
| `ContentView.swift` | 主界面 |

## 在 Xcode 里建工程（一次性）

1. Xcode → `File → New → Project → iOS → App`，Product Name 填 `ServoControl`，
   Interface 选 **SwiftUI**，Language 选 **Swift**。
2. 删掉自动生成的 `ContentView.swift`，把上面 3 个 `.swift` 文件拖进工程
   （勾选 `Copy items if needed`）。
3. 给 App 加蓝牙权限描述（否则一调蓝牙就崩）：
   - 选中 Target → `Info` 标签 → 点 `+` 添加一行，Key 填 `Privacy - Bluetooth Always Usage Description`
     （`NSBluetoothAlwaysUsageDescription`），Value 填 `用于连接舵机控制器`。
4. 签名：Target → `Signing & Capabilities` → 勾选 `Automatically manage signing`，
   选你的 Team，Bundle Identifier 改成唯一值（如 `com.你的名字.servocontrol`）。
5. 真机运行（**BLE 在模拟器上不可用，必须真机**）。

## 与固件的对应关系

App 里两个 UUID 必须和 `receiver/src/main.cpp` 一致：

- Service UUID：`4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- 手机特征值 UUID：`beb5483e-36e1-4688-b7f5-ea07361b26a9`（固件里叫 `CHAR_PHONE_UUID`）

## 注意

- **多套设备同名会打架**：如果附近有好几台接收端，App 会自动连到第一台名字叫
  `ESP32-LED-RX` 的设备。建议给每台设备在 `receiver/src/main.cpp` 里把 `DEVICE_NAME`
  改成唯一名（如 `ESP32-LED-RX-01`），并把 `BLEManager.swift` 里的 `targetName` 改成对应名字。

## 分发安装包（给别人装）

iOS 不能直接发安装包。正规做法是 **TestFlight**（需要 Apple Developer Program，$99/年）：

1. Xcode → `Product → Archive` → `Distribute App → App Store Connect` 上传。
2. 到 App Store Connect → 你的 App → TestFlight → 添加测试员（对方的 Apple ID 邮箱）。
3. 对方 iPhone 装 TestFlight app → 收到邀请 → 点安装。

一个 build 有效期 90 天，到期重新上传一次即可。
