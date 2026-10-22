import SwiftUI

struct ContentView: View {
    @StateObject private var ble = BLEManager()

    var body: some View {
        VStack(spacing: 28) {
            Text("舵机控制")
                .font(.largeTitle.bold())

            Text(ble.statusText)
                .font(.headline)
                .foregroundColor(ble.isConnected ? .green : .red)
                .multilineTextAlignment(.center)

            ServoKnobView(level: $ble.level) { newLevel in
                ble.sendLevel(newLevel)
            }
            .frame(width: 280, height: 280)

            Text("当前压力值：\(Int(ble.level))")
                .font(.title3.monospacedDigit())

            Button {
                if ble.isConnected {
                    ble.disconnect()
                } else {
                    ble.startScan()
                }
            } label: {
                Text(ble.isConnected ? "断开连接" : "连接舵机")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .padding(.horizontal, 40)
        }
        .padding()
    }
}
