#!/usr/bin/env swift

import CoreGraphics
import Foundation
import ImageIO

struct IconDefinition {
    let arrayName: String
    let fileName: String
    let width: Int
    let height: Int
}

let icons = [
    IconDefinition(arrayName: "kFlamesBitmap", fileName: "flames.png", width: 24, height: 24),
    IconDefinition(arrayName: "kSpigotBitmap", fileName: "spigot.png", width: 24, height: 24),
    IconDefinition(arrayName: "kFanBitmap", fileName: "fan.png", width: 24, height: 24),
    IconDefinition(arrayName: "kLightbulbBitmap", fileName: "lightbulb.png", width: 14, height: 24),
    IconDefinition(arrayName: "kSunBitmap", fileName: "sun.png", width: 22, height: 22),
    IconDefinition(arrayName: "kPowerCordBitmap", fileName: "power-cord.png", width: 24, height: 17),
    IconDefinition(arrayName: "kThermometerBitmap", fileName: "thermometer.png", width: 14, height: 24),
    IconDefinition(arrayName: "kHumidityBitmap", fileName: "humidity.png", width: 26, height: 22),
]

let projectRoot = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let sourceURL = projectRoot.appendingPathComponent("src/touchscreen/LightbulbFont.cpp")
let outputURL = projectRoot.appendingPathComponent("assets/touchscreen/icons")
let source = try String(contentsOf: sourceURL, encoding: .utf8)
try FileManager.default.createDirectory(at: outputURL, withIntermediateDirectories: true)

func alphaBytes(for icon: IconDefinition) throws -> [UInt8] {
    let escapedName = NSRegularExpression.escapedPattern(for: icon.arrayName)
    let pattern = "constexpr\\s+uint8_t\\s+\(escapedName)\\[\\]\\s*=\\s*\\{([\\s\\S]*?)\\};"
    let arrayRegex = try NSRegularExpression(pattern: pattern)
    let sourceRange = NSRange(source.startIndex..<source.endIndex, in: source)
    guard let match = arrayRegex.firstMatch(in: source, range: sourceRange),
          let bodyRange = Range(match.range(at: 1), in: source) else {
        throw NSError(domain: "BlueSquidIconExport", code: 1,
                      userInfo: [NSLocalizedDescriptionKey: "Could not find \(icon.arrayName)"])
    }

    let body = String(source[bodyRange])
    let byteRegex = try NSRegularExpression(pattern: "0x([0-9A-Fa-f]{2})")
    let bodyRangeNS = NSRange(body.startIndex..<body.endIndex, in: body)
    let packed = byteRegex.matches(in: body, range: bodyRangeNS).compactMap { match -> UInt8? in
        guard let range = Range(match.range(at: 1), in: body) else { return nil }
        return UInt8(body[range], radix: 16)
    }

    var alpha: [UInt8] = []
    alpha.reserveCapacity(icon.width * icon.height)
    let bytesPerRow = (icon.width + 1) / 2
    guard packed.count == bytesPerRow * icon.height else {
        throw NSError(domain: "BlueSquidIconExport", code: 2,
                      userInfo: [NSLocalizedDescriptionKey:
                        "Unexpected byte count for \(icon.arrayName): \(packed.count)"])
    }

    for row in 0..<icon.height {
        for column in 0..<icon.width {
            let byte = packed[row * bytesPerRow + column / 2]
            let nibble = column.isMultiple(of: 2) ? byte >> 4 : byte & 0x0F
            alpha.append(nibble * 17)
        }
    }
    return alpha
}

func writePNG(icon: IconDefinition, alpha: [UInt8]) throws {
    var rgba: [UInt8] = []
    rgba.reserveCapacity(icon.width * icon.height * 4)
    for opacity in alpha {
        rgba.append(255)
        rgba.append(255)
        rgba.append(255)
        rgba.append(opacity)
    }

    guard let provider = CGDataProvider(data: Data(rgba) as CFData),
          let image = CGImage(
            width: icon.width,
            height: icon.height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: icon.width * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
          ) else {
        throw NSError(domain: "BlueSquidIconExport", code: 3,
                      userInfo: [NSLocalizedDescriptionKey: "Could not create \(icon.fileName)"])
    }

    let destinationURL = outputURL.appendingPathComponent(icon.fileName)
    guard let destination = CGImageDestinationCreateWithURL(
        destinationURL as CFURL, "public.png" as CFString, 1, nil
    ) else {
        throw NSError(domain: "BlueSquidIconExport", code: 4,
                      userInfo: [NSLocalizedDescriptionKey: "Could not open \(icon.fileName)"])
    }
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else {
        throw NSError(domain: "BlueSquidIconExport", code: 5,
                      userInfo: [NSLocalizedDescriptionKey: "Could not write \(icon.fileName)"])
    }
}

for icon in icons {
    try writePNG(icon: icon, alpha: alphaBytes(for: icon))
    print("Exported \(icon.fileName) (\(icon.width)x\(icon.height))")
}
