import Foundation
import ScreenCaptureKit
import CoreImage
import ImageIO

@available(macOS 13.0, *)
enum NativeScreenshot {
    static func save(_ arguments: CaptureArguments, output: String) async throws {
        guard CGPreflightScreenCaptureAccess() else {
            throw CaptureFailure("Нет разрешения записи экрана. Разрешите Кадролову запись экрана в Системных настройках и перезапустите приложение. Снимок рабочего стола без окон не будет сохранён вместо результата.")
        }
        let destination = URL(fileURLWithPath: output).standardizedFileURL
        guard destination.pathExtension.lowercased() == "png" else { throw CaptureFailure("Модуль снимка сохраняет PNG. Другой формат выберите в редакторе.") }
        guard !FileManager.default.fileExists(atPath: destination.path) else { throw CaptureFailure("Файл снимка уже существует.") }
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        let display: SCDisplay
        if let id = arguments.display {
            guard let selected = content.displays.first(where: { $0.displayID == id }) else { throw CaptureFailure("Выбранный монитор не найден.") }
            display = selected
        } else if let region = arguments.rect {
            guard let selected = content.displays.first(where: { CGDisplayBounds($0.displayID).contains(region) }) else { throw CaptureFailure("Область должна целиком находиться на одном мониторе.") }
            display = selected
        } else {
            guard let selected = content.displays.first(where: { $0.displayID == CGMainDisplayID() }) else { throw CaptureFailure("Основной монитор не найден.") }
            display = selected
        }
        let bounds = CGDisplayBounds(display.displayID)
        var source = try CaptureGeometry.region(arguments.rect, in: bounds)
        let filter: SCContentFilter
        if let id = arguments.window {
            guard let window = content.windows.first(where: { $0.windowID == id }) else { throw CaptureFailure("Выбранное окно больше не доступно.") }
            filter = SCContentFilter(desktopIndependentWindow: window)
            source = CGRect(origin: .zero, size: window.frame.size)
        } else {
            let excluded = content.applications.filter { $0.processID == arguments.excludedPID || $0.processID == getpid() }
            filter = SCContentFilter(display: display, excludingApplications: excluded, exceptingWindows: [])
        }
        let scale = Double(CGDisplayPixelsWide(display.displayID)) / max(1, bounds.width)
        let config = SCStreamConfiguration()
        config.width = arguments.width > 0 ? arguments.width : max(2, Int(source.width * scale))
        config.height = arguments.height > 0 ? arguments.height : max(2, Int(source.height * scale))
        config.sourceRect = source
        config.pixelFormat = kCVPixelFormatType_32BGRA
        config.showsCursor = arguments.cursor
        config.capturesAudio = false
        config.queueDepth = 2
        if #available(macOS 14.0, *) { config.ignoreShadowsSingleWindow = true }
        let image: CGImage
        if #available(macOS 14.0, *) {
            image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: config)
        } else {
            image = try await ScreenshotStream().capture(filter: filter, config: config)
        }
        guard let writer = CGImageDestinationCreateWithURL(destination as CFURL, "public.png" as CFString, 1, nil) else { throw CaptureFailure("Не удалось открыть файл для снимка.") }
        CGImageDestinationAddImage(writer, image, nil)
        guard CGImageDestinationFinalize(writer) else { throw CaptureFailure("Не удалось сохранить PNG. Проверьте папку и свободное место.") }
        emit(["event": "finished", "path": destination.path, "width": image.width, "height": image.height])
    }
}

@available(macOS 13.0, *)
private final class ScreenshotStream: NSObject, SCStreamOutput, SCStreamDelegate, @unchecked Sendable {
    let lock = NSLock()
    var completion: ((Result<CGImage, Error>) -> Void)?
    var timeout: Task<Void, Never>?
    func complete(_ value: Result<CGImage, Error>) {
        lock.lock()
        let callback = completion
        completion = nil
        lock.unlock()
        timeout?.cancel()
        callback?(value)
    }
    func capture(filter: SCContentFilter, config: SCStreamConfiguration) async throws -> CGImage {
        let stream = SCStream(filter: filter, configuration: config, delegate: self)
        try stream.addStreamOutput(self, type: .screen, sampleHandlerQueue: DispatchQueue(label: "ru.kadrolov.screenshot"))
        do {
            let result: CGImage = try await withCheckedThrowingContinuation { continuation in
                completion = { continuation.resume(with: $0) }
                timeout = Task {
                    do { try await Task.sleep(nanoseconds: 10_000_000_000) }
                    catch { return }
                    complete(.failure(CaptureFailure("Экран не передал кадр за 10 секунд. Проверьте разрешение и источник.")))
                }
                Task {
                    do { try await stream.startCapture() }
                    catch { complete(.failure(error)) }
                }
            }
            try? await stream.stopCapture()
            return result
        } catch {
            try? await stream.stopCapture()
            throw error
        }
    }
    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, let buffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        if let attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
           let status = attachments.first?[.status] as? Int, status != SCFrameStatus.complete.rawValue { return }
        let source = CIImage(cvPixelBuffer: buffer)
        guard let image = CIContext(options: [.cacheIntermediates: false]).createCGImage(source, from: source.extent) else { return }
        complete(.success(image))
    }
    func stream(_ stream: SCStream, didStopWithError error: Error) { complete(.failure(error)) }
}
