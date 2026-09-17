#!/usr/bin/env swift
import AppKit
import Foundation

// Rasterize the macOS SF Symbols fan.fill into the existing LVGL A4 glyph.
guard let symbol = NSImage(systemSymbolName: "fan.fill", accessibilityDescription: nil),
      let configured = symbol.withSymbolConfiguration(NSImage.SymbolConfiguration(pointSize: 48, weight: .regular)) else {
    fatalError("SF Symbol fan.fill is unavailable on this Mac")
}
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 96, pixelsHigh: 96,
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
    colorSpaceName: .deviceRGB, bytesPerRow: 384, bitsPerPixel: 32)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
configured.draw(in: NSRect(x: 8, y: 8, width: 80, height: 80), from: .zero, operation: .copy, fraction: 1)
NSGraphicsContext.restoreGraphicsState()
var left = 96, top = 96, right = 0, bottom = 0
for y in 0..<96 { for x in 0..<96 {
    if rep.colorAt(x: x, y: y)!.alphaComponent > 0.01 {
        left = min(left,x); right = max(right,x); top = min(top,y); bottom = max(bottom,y)
    }
}}
precondition(right > left && bottom > top, "Symbol rendered empty")
let side = Double(max(right-left+1, bottom-top+1))
let originX = Double(left+right+1)/2-side/2
let originY = Double(top+bottom+1)/2-side/2
var alpha = [UInt8]()
for y in 0..<24 { for x in 0..<24 {
    var sum = 0.0
    for sy in 0..<4 { for sx in 0..<4 {
        let px = Int(originX + (Double(x)+(Double(sx)+0.5)/4)*side/24)
        let py = Int(originY + (Double(y)+(Double(sy)+0.5)/4)*side/24)
        sum += Double(rep.colorAt(x: max(0,min(95,px)), y: max(0,min(95,py)))!.alphaComponent)
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
let regex = try NSRegularExpression(pattern: "constexpr uint8_t kFanBitmap\\[\\] = \\{[\\s\\S]*?\\};")
source = regex.stringByReplacingMatches(in: source, range: NSRange(source.startIndex..., in: source), withTemplate: "constexpr uint8_t kFanBitmap[] = {\n" + lines.joined(separator:"\n") + "\n};")
try source.write(toFile: path, atomically: true, encoding: .utf8)
print("Imported SF Symbols fan.fill as a 24×24 A4 glyph")
