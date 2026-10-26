import Foundation
import CoreGraphics

struct CaptureFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
    init(_ message: String) { self.message = message }
}

/// All SCK streams use the host clock. One anchor and pause offset is shared by every track.
struct PauseTimeline {
    private(set) var anchor: Double?
    private(set) var pausedAt: Double?
    private(set) var pausedDuration = 0.0
    private var resumeBoundary = -Double.infinity
    private var last: [String: Double] = [:]
    var isPaused: Bool { pausedAt != nil }
    var offset: Double? { anchor.map { $0 + pausedDuration } }
    mutating func pause(at time: Double) {
        guard pausedAt == nil else { return }
        pausedAt = time
    }
    mutating func resume(at time: Double) {
        guard let began = pausedAt else { return }
        pausedDuration += max(0, time - began)
        pausedAt = nil
        resumeBoundary = time
    }
    mutating func map(_ timestamp: Double, track: String = "video") -> Double? {
        guard timestamp.isFinite, pausedAt == nil, timestamp >= resumeBoundary else { return nil }
        if anchor == nil {
            guard track == "video" else { return nil }
            anchor = timestamp
        }
        let result = timestamp - anchor! - pausedDuration
        guard result >= 0, result > (last[track] ?? -Double.infinity) else { return nil }
        last[track] = result
        return result
    }
}

enum CaptureGeometry {
    static func region(_ global: CGRect?, in bounds: CGRect) throws -> CGRect {
        guard let global = global else { return CGRect(origin: .zero, size: bounds.size) }
        guard global.width >= 2, global.height >= 2,
              global.minX.isFinite, global.minY.isFinite,
              global.width.isFinite, global.height.isFinite,
              bounds.insetBy(dx: -0.5, dy: -0.5).contains(global) else {
            throw CaptureFailure("Область должна целиком находиться на выбранном мониторе и быть не меньше 2 × 2.")
        }
        return global.offsetBy(dx: -bounds.minX, dy: -bounds.minY)
    }
    static func evenSize(_ value: Int) -> Int { max(2, value / 2 * 2) }
}

struct CaptureArguments {
    var output = ""
    var display: UInt32?
    var window: UInt32?
    var rect: CGRect?
    var fps = 30
    var width = 0
    var height = 0
    var quality = 23
    var audio = "none"
    var cursor = true
    var clicks = false
    var microphone: String?
    var camera = false
    var cameraDevice: String?
    var ocrPath: String?
    var screenshotPath: String?
    var mixAudioPath: String?
    var excludedPID: Int32?
    var listDisplays = false
    var listMicrophones = false
    var listWindows = false
    var checkPermission = false
    var probeContent = false
    var requestPermission = false
    var help = false
    static func parse(_ values: [String]) throws -> CaptureArguments {
        var result = CaptureArguments()
        var i = 0
        while i < values.count {
            let key = values[i]
            if key == "--help" || key == "-h" { result.help = true; i += 1; continue }
            if key == "--list-microphones" { result.listMicrophones = true; i += 1; continue }
            if key == "--list-displays" { result.listDisplays = true; i += 1; continue }
            if key == "--list-windows" { result.listWindows = true; i += 1; continue }
            if key == "--request-permission" { result.requestPermission = true; i += 1; continue }
            if key == "--probe-content" { result.probeContent = true; i += 1; continue }
            if key == "--check-permission" { result.checkPermission = true; i += 1; continue }
            guard i + 1 < values.count else { throw CaptureFailure("Не задано значение \(key).") }
            let value = values[i + 1]
            func integer(_ range: ClosedRange<Int>) throws -> Int {
                guard let n = Int(value), range.contains(n) else { throw CaptureFailure("Недопустимое значение \(key): \(value).") }
                return n
            }
            func flag() throws -> Bool {
                guard value == "0" || value == "1" else { throw CaptureFailure("\(key) принимает только 0 или 1.") }
                return value == "1"
            }
            switch key {
            case "--mix-audio": result.mixAudioPath = value
            case "--screenshot": result.screenshotPath = value
            case "--ocr": result.ocrPath = value
            case "--camera": result.camera = try flag()
            case "--camera-device": result.cameraDevice = value
            case "--output": result.output = value
            case "--display": result.display = UInt32(try integer(1...Int(UInt32.max)))
            case "--window": result.window = UInt32(try integer(1...Int(UInt32.max)))
            case "--fps": result.fps = try integer(1...60)
            case "--width": result.width = try integer(0...16384)
            case "--height": result.height = try integer(0...16384)
            case "--quality": result.quality = try integer(0...51)
            case "--audio":
                guard ["none", "system", "mic", "both"].contains(value) else { throw CaptureFailure("Неизвестный режим звука.") }
                result.audio = value
            case "--cursor": result.cursor = try flag()
            case "--clicks": result.clicks = try flag()
            case "--microphone": result.microphone = value
            case "--exclude-pid": result.excludedPID = Int32(try integer(1...Int(Int32.max)))
            case "--rect":
                let coordinates = value.split(separator: ",").compactMap { Double($0) }
                guard coordinates.count == 4, coordinates.allSatisfy({ $0.isFinite }), coordinates[2] >= 2, coordinates[3] >= 2 else { throw CaptureFailure("Область: x,y,ширина,высота в логических точках.") }
                result.rect = CGRect(x: coordinates[0], y: coordinates[1], width: coordinates[2], height: coordinates[3])
            default: throw CaptureFailure("Неизвестный параметр \(key).")
            }
            i += 2
        }
        guard !result.probeContent || result.checkPermission else { throw CaptureFailure("--probe-content применяется только вместе с --check-permission.") }
        guard (result.width == 0) == (result.height == 0), result.width != 1, result.height != 1 else {
            throw CaptureFailure("Ширина и высота задаются вместе и должны быть не меньше 2.")
        }
        if !result.listMicrophones && !result.listDisplays && !result.listWindows && !result.checkPermission && !result.requestPermission && !result.help && result.ocrPath == nil && result.screenshotPath == nil {
            guard !result.output.isEmpty else { throw CaptureFailure("Укажите --output для сохранения MP4.") }
            guard result.output.lowercased().hasSuffix(".mp4") else { throw CaptureFailure("Нативный модуль сохраняет MP4. Другой контейнер требует преобразования после записи.") }
        }
        return result
    }
}
