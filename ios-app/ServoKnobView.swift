import SwiftUI

/// 可拖拽的旋钮：把手指相对圆心的角度映射成 0~100 压力值。
/// 0 = 指针指向左下(-120°)，100 = 指针指向右下(+120°)，共 240° 行程。
struct ServoKnobView: View {
    @Binding var level: Double                 // 0...100
    var onChanged: (Int) -> Void

    var body: some View {
        GeometryReader { geo in
            let size = min(geo.size.width, geo.size.height)
            let center = CGPoint(x: geo.size.width / 2, y: geo.size.height / 2)
            let radius = size / 2 - 20

            ZStack {
                Circle()
                    .stroke(Color.secondary.opacity(0.35), lineWidth: 16)

                // 指针（绕中心旋转）
                Capsule()
                    .fill(Color.accentColor)
                    .frame(width: 8, height: radius)
                    .offset(y: -radius / 2)
                    .rotationEffect(.degrees(levelToAngle(level)))

                // 中心圆点
                Circle()
                    .fill(Color.accentColor)
                    .frame(width: 30, height: 30)
            }
            .frame(width: size, height: size)
            .position(center)
            .contentShape(Circle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        let dx = value.location.x - center.x
                        let dy = value.location.y - center.y
                        // 以"向上"为 0°，顺时针为正
                        let deg = atan2(dx, -dy) * 180 / Double.pi
                        level = angleToLevel(deg)
                        onChanged(Int(level.rounded()))
                    }
            )
        }
    }

    private func levelToAngle(_ l: Double) -> Double {
        -120 + (l / 100.0) * 240
    }

    private func angleToLevel(_ deg: Double) -> Double {
        let clamped = min(max(deg, -120), 120)
        return (clamped + 120) / 240 * 100
    }
}
