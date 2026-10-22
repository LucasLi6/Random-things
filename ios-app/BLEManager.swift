import Foundation
import CoreBluetooth

/// 蓝牙管理器：App 作为 BLE Central，连上 ESP32 接收端（外设），
/// 把 0~100 压力值写进"手机特征值"。
final class BLEManager: NSObject, ObservableObject {
    @Published var isPoweredOn = false
    @Published var isConnected = false
    @Published var statusText = "未连接"
    @Published var level: Double = 0            // 0~100 压力值

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var phoneCharacteristic: CBCharacteristic?
    private var lastSentLevel: Int?

    // 与 receiver 固件保持一致
    private let serviceUUID = CBUUID(string: "4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    private let phoneCharUUID = CBUUID(string: "beb5483e-36e1-4688-b7f5-ea07361b26a9")
    private let targetName = "ESP32-LED-RX"

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func startScan() {
        guard centralManager.state == .poweredOn else { return }
        statusText = "正在扫描…"
        centralManager.scanForPeripherals(withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    func disconnect() {
        guard let peripheral = peripheral else { return }
        centralManager.cancelPeripheralConnection(peripheral)
    }

    func sendLevel(_ newLevel: Int) {
        guard let peripheral = peripheral, let ch = phoneCharacteristic else { return }
        let clamped = min(max(newLevel, 0), 100)
        guard lastSentLevel != clamped else { return }   // 值没变就不发，避免 flooding
        lastSentLevel = clamped
        peripheral.writeValue(Data([UInt8(clamped)]), for: ch, type: .withoutResponse)
    }
}

extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        isPoweredOn = central.state == .poweredOn
        if central.state == .poweredOn {
            startScan()
        } else {
            statusText = "蓝牙不可用，请打开蓝牙"
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
            ?? peripheral.name ?? ""
        guard name == targetName else { return }

        self.peripheral = peripheral
        central.stopScan()
        statusText = "找到 \(name)，连接中…"
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager,
                        didConnect peripheral: CBPeripheral) {
        isConnected = true
        lastSentLevel = nil
        statusText = "已连接，拖拽旋钮控制舵机"
        peripheral.delegate = self
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        isConnected = false
        phoneCharacteristic = nil
        lastSentLevel = nil
        statusText = "已断开，重新扫描…"
        self.peripheral = nil
        startScan()
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        statusText = "连接失败，重新扫描…"
        self.peripheral = nil
        startScan()
    }
}

extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let services = peripheral.services else { return }
        for service in services where service.uuid == serviceUUID {
            peripheral.discoverCharacteristics([phoneCharUUID], for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        guard error == nil, let chars = service.characteristics else { return }
        for ch in chars where ch.uuid == phoneCharUUID {
            phoneCharacteristic = ch
        }
    }
}
