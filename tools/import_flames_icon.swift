#!/usr/bin/env swift
import AppKit
import Foundation

// Convert the supplied dark-on-white flame silhouette to an LVGL alpha mask.
let data = try Data(contentsOf: URL(fileURLWithPath: "assets/touchscreen/icons/flames-source.png"))
guard let rep = NSBitmapImageRep(data: data) else { fatalError("Cannot load flame source") }
let width = rep.pixelsWide, height = rep.pixelsHigh
func opacity(_ x: Int, _ y: Int) -> Double {
    guard x >= 0 && y >= 0 && x < width && y < height,
          let color = rep.colorAt(x: x, y: y)?.usingColorSpace(.deviceRGB) else { return 0 }
    let gray = Double((color.redComponent + color.greenComponent + color.blueComponent) / 3)
    return min(1, max(0, (1 - gray) / 0.8)) * Double(color.alphaComponent)
}
var left = width, top = height, right = 0, bottom = 0
for y in 0..<height { for x in 0..<width {
    if opacity(x, y) > 0.05 {
        left = min(left, x); right = max(right, x)
        top = min(top, y); bottom = max(bottom, y)
    }
}}
precondition(right > left && bottom > top, "Flame image is empty")
let side = Double(max(right-left+1, bottom-top+1))
let originX = Double(left+right+1)/2-side/2
let originY = Double(top+bottom+1)/2-side/2
var alpha = [UInt8]()
for y in 0..<24 { for x in 0..<24 {
    var sum = 0.0
    for sy in 0..<4 { for sx in 0..<4 {
        let px = Int(originX + (Double(x)+(Double(sx)+0.5)/4)*side/24)
        let py = Int(originY + (Double(y)+(Double(sy)+0.5)/4)*side/24)
        sum += opacity(px, py)
    }}
    alpha.append(UInt8((sum/16*15).rounded()))
}}
var lines = [String]()
for y in 0..<24 {
    var row = [String]()
    for x in stride(from: 0, to: 24, by: 2) {
        row.append(String(format:"0x%02X", (alpha[y*24+x]<<4)|alpha[y*24+x+1]))
    }
    lines.append("    " + row.joined(separator: ", ") + ",")
}
let path = "src/touchscreen/LightbulbFont.cpp"
var source = try String(contentsOfFile: path, encoding: .utf8)
let regex = try NSRegularExpression(pattern: "constexpr uint8_t kFlamesBitmap\\[\\] = \\{[\\s\\S]*?\\};")
source = regex.stringByReplacingMatches(in: source, range: NSRange(source.startIndex..., in: source), withTemplate: "constexpr uint8_t kFlamesBitmap[] = {\n" + lines.joined(separator:"\n") + "\n};")
try source.write(toFile: path, atomically: true, encoding: .utf8)
print("Imported supplied flame image as a 24×24 A4 glyph")
