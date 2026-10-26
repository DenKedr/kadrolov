import AppKit
import AVFoundation
import AudioToolbox
import CoreAudio
import CoreMedia
import ScreenCaptureKit
import Vision
import ImageIO
import CoreImage

private let outputLock = NSLock()
func emit(_ payload: [String: Any]) {
    guard let bytes = try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys]) else { return }
    outputLock.lock()
    FileHandle.standardOutput.write(bytes)
    FileHandle.standardOutput.write(Data([10]))
    outputLock.unlock()
}
func hostSeconds() -> Double { CMClockGetTime(CMClockGetHostTimeClock()).seconds }

@available(macOS 13.0, *)
// Mutable recording state is confined to queue after prepare() finishes; teardown drains it.
final class CaptureSession: NSObject, SCStreamOutput, SCStreamDelegate, AVCaptureVideoDataOutputSampleBufferDelegate, @unchecked Sendable {
    let arguments: CaptureArguments
    let queue = DispatchQueue(label: "ru.kadrolov.capture.samples", qos: .userInitiated)
    var timeline = PauseTimeline()
    var stream: SCStream?
    var writer: AVAssetWriter?
    var video: AVAssetWriterInput?
    var systemAudio: AVAssetWriterInput?
    var microphone: AVAssetWriterInput?
    var engine: AVAudioEngine?
    var cameraSession: AVCaptureSession?
    var cameraFrame: CVPixelBuffer?
    var cameraFrames = 0
    var cameraObservers: [NSObjectProtocol] = []
    var compositePool: CVPixelBufferPool?
    lazy var imageContext = CIContext(options: [.cacheIntermediates: false])
    var lastVideo: CMSampleBuffer?
    var lastVideoTime = 0.0
    var outputWidth = 0, outputHeight = 0
    var frames = 0, audioSamples = 0, microphoneSamples = 0
    var stopping = false, fatalMessage: String?
    var finish: (() -> Void)?
    private let completionLock = NSLock()
    private var didComplete = false

    func finishOnce() {
        completionLock.lock()
        guard !didComplete else { completionLock.unlock(); return }
        didComplete = true
        completionLock.unlock()
        finish?()
    }

    init(_ arguments: CaptureArguments) { self.arguments = arguments }

    func prepare() async throws {
        guard CGPreflightScreenCaptureAccess() else {
            throw CaptureFailure("Нет разрешения записи экрана. Разрешите Кадролову запись экрана в Системных настройках → Конфиденциальность и безопасность → Запись экрана и системного аудио, затем перезапустите приложение.")
        }
        if arguments.clicks {
            if #available(macOS 15.0, *) {} else {
                throw CaptureFailure("Подсветка кликов нативным модулем доступна с macOS 15. Выключите её перед записью на macOS 13/14.")
            }
        }
        let needsMic = arguments.audio == "mic" || arguments.audio == "both"
        if needsMic, let id = arguments.microphone {
            guard let device = AVCaptureDevice(uniqueID: id), device.isConnected, device.hasMediaType(.audio) else { throw CaptureFailure("Выбранный микрофон отключён. Подключите его или выберите другое устройство.") }
        }
        if needsMic {
            let allowed: Bool
            switch AVCaptureDevice.authorizationStatus(for: .audio) {
            case .authorized: allowed = true
            case .notDetermined: allowed = await AVCaptureDevice.requestAccess(for: .audio)
            default: allowed = false
            }
            guard allowed else { throw CaptureFailure("Нет разрешения микрофона. Разрешите доступ в Системных настройках или выберите запись без микрофона.") }
        }
        if arguments.camera {
            let allowed: Bool
            switch AVCaptureDevice.authorizationStatus(for: .video) {
            case .authorized: allowed = true
            case .notDetermined: allowed = await AVCaptureDevice.requestAccess(for: .video)
            default: allowed = false
            }
            guard allowed else { throw CaptureFailure("Нет разрешения камеры. Разрешите доступ или выключите камеру перед записью.") }
        }
        let target = URL(fileURLWithPath: arguments.output).standardizedFileURL
        guard !FileManager.default.fileExists(atPath: target.path) else { throw CaptureFailure("Файл уже существует. Выберите другое имя, чтобы не перезаписать запись.") }
        let parent = target.deletingLastPathComponent()
        guard FileManager.default.fileExists(atPath: parent.path), FileManager.default.isWritableFile(atPath: parent.path) else {
            throw CaptureFailure("Папка сохранения недоступна для записи.")
        }
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        let display: SCDisplay
        if let id = arguments.display {
            guard let found = content.displays.first(where: { $0.displayID == id }) else { throw CaptureFailure("Выбранный монитор больше не доступен.") }
            display = found
        } else if let rect = arguments.rect {
            guard let found = content.displays.first(where: { CGDisplayBounds($0.displayID).contains(rect) }) else { throw CaptureFailure("Область должна целиком находиться на одном подключённом мониторе.") }
            display = found
        } else {
            guard let found = content.displays.first(where: { $0.displayID == CGMainDisplayID() }) else { throw CaptureFailure("Основной монитор не найден.") }
            display = found
        }
        let bounds = CGDisplayBounds(display.displayID)
        var source = try CaptureGeometry.region(arguments.rect, in: bounds)
        let filter: SCContentFilter
        if let windowID = arguments.window {
            guard let selected = content.windows.first(where: { $0.windowID == windowID }) else { throw CaptureFailure("Выбранное окно больше не доступно.") }
            filter = SCContentFilter(desktopIndependentWindow: selected)
            source = CGRect(origin: .zero, size: selected.frame.size)
        } else {
            let excluded = content.applications.filter { $0.processID == arguments.excludedPID || $0.processID == getpid() }
            filter = SCContentFilter(display: display, excludingApplications: excluded, exceptingWindows: [])
        }
        let scale = Double(CGDisplayPixelsWide(display.displayID)) / max(1, bounds.width)
        outputWidth = CaptureGeometry.evenSize(arguments.width == 0 ? Int(source.width * scale) : arguments.width)
        outputHeight = CaptureGeometry.evenSize(arguments.height == 0 ? Int(source.height * scale) : arguments.height)
        guard outputWidth <= 16384 && outputHeight <= 16384 else { throw CaptureFailure("Размер кадра превышает 16384 пикселя.") }
        let config = SCStreamConfiguration()
        config.width = outputWidth
        config.height = outputHeight
        config.sourceRect = source
        config.minimumFrameInterval = CMTime(value: 1, timescale: CMTimeScale(arguments.fps))
        config.pixelFormat = kCVPixelFormatType_32BGRA
        config.queueDepth = 3
        config.showsCursor = arguments.cursor
        config.capturesAudio = arguments.audio == "system" || arguments.audio == "both"
        config.excludesCurrentProcessAudio = true
        config.sampleRate = 48000
        config.channelCount = 2
        if #available(macOS 15.0, *) {
            config.showMouseClicks = arguments.clicks
            config.captureMicrophone = needsMic
            config.microphoneCaptureDeviceID = arguments.microphone
        }
        let fileWriter = try AVAssetWriter(outputURL: target, fileType: .mp4)
        fileWriter.shouldOptimizeForNetworkUse = false // local files; avoid costly end-of-recording rewrite
        fileWriter.movieFragmentInterval = CMTime(seconds: 2, preferredTimescale: 600) // retain recoverable fragments after interruption
        let factor = pow(2.0, Double(23 - arguments.quality) / 6.0)
        let bitRate = min(120_000_000, max(500_000, Int(Double(outputWidth * outputHeight * arguments.fps) * 0.10 * factor)))
        let videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: outputWidth, AVVideoHeightKey: outputHeight,
            AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: bitRate,
                AVVideoMaxKeyFrameIntervalKey: arguments.fps * 2,
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel]
        ])
        videoInput.expectsMediaDataInRealTime = true
        guard fileWriter.canAdd(videoInput) else { throw CaptureFailure("H.264 недоступен для выбранного размера кадра.") }
        fileWriter.add(videoInput)
        video = videoInput
        if config.capturesAudio {
            systemAudio = try addAudio(to: fileWriter, channels: 2, title: "Звук компьютера")
        }
        if needsMic { microphone = try addAudio(to: fileWriter, channels: 1, title: "Микрофон") }
        guard fileWriter.startWriting() else { throw fileWriter.error ?? CaptureFailure("Не удалось начать запись файла.") }
        fileWriter.startSession(atSourceTime: .zero)
        writer = fileWriter
        let capture = SCStream(filter: filter, configuration: config, delegate: self)
        try capture.addStreamOutput(self, type: .screen, sampleHandlerQueue: queue)
        if config.capturesAudio { try capture.addStreamOutput(self, type: .audio, sampleHandlerQueue: queue) }
        if needsMic {
            if #available(macOS 15.0, *) {
                try capture.addStreamOutput(self, type: .microphone, sampleHandlerQueue: queue)
            } else {
                try prepareLegacyMicrophone()
            }
        }
        if arguments.camera { try prepareCamera() }
        stream = capture
        do { try await capture.startCapture() }
        catch {
            writer?.cancelWriting()
            engine?.stop()
            cameraSession?.stopRunning()
            throw error
        }
        emit(["event": "started", "path": target.path, "width": outputWidth, "height": outputHeight,
              "display": display.displayID, "audioTracks": (systemAudio == nil ? 0 : 1) + (microphone == nil ? 0 : 1),
              "cursor": arguments.cursor, "clicks": arguments.clicks, "camera": arguments.camera])
    }

    func addAudio(to writer: AVAssetWriter, channels: Int, title: String) throws -> AVAssetWriterInput {
        let input = AVAssetWriterInput(mediaType: .audio, outputSettings: [AVFormatIDKey: kAudioFormatMPEG4AAC,
            AVSampleRateKey: 48000, AVNumberOfChannelsKey: channels, AVEncoderBitRateKey: channels == 1 ? 96000 : 192000])
        input.expectsMediaDataInRealTime = true
        let label = AVMutableMetadataItem()
        label.identifier = .commonIdentifierTitle
        label.value = title as NSString
        input.metadata = [label]
        guard writer.canAdd(input) else { throw CaptureFailure("Не удалось создать звуковую дорожку.") }
        writer.add(input)
        return input
    }

    func prepareCamera() throws {
        let devices = AVCaptureDevice.DiscoverySession(deviceTypes: [.builtInWideAngleCamera, .externalUnknown], mediaType: .video, position: .unspecified).devices
        let device: AVCaptureDevice?
        if let selected = arguments.cameraDevice { device = devices.first(where: { $0.uniqueID == selected }) }
        else { device = AVCaptureDevice.default(for: .video) }
        guard let device else { throw CaptureFailure("Выбранная камера не найдена.") }
        let capture = AVCaptureSession()
        capture.beginConfiguration()
        if capture.canSetSessionPreset(.medium) { capture.sessionPreset = .medium }
        let input = try AVCaptureDeviceInput(device: device)
        guard capture.canAddInput(input) else { throw CaptureFailure("Не удалось открыть камеру.") }
        capture.addInput(input)
        let output = AVCaptureVideoDataOutput()
        output.alwaysDiscardsLateVideoFrames = true
        output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
        output.setSampleBufferDelegate(self, queue: queue)
        guard capture.canAddOutput(output) else { throw CaptureFailure("Не удалось получить изображение камеры.") }
        capture.addOutput(output)
        capture.commitConfiguration()
        var pool: CVPixelBufferPool?
        let attributes: [String: Any] = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: outputWidth, kCVPixelBufferHeightKey as String: outputHeight,
            kCVPixelBufferIOSurfacePropertiesKey as String: [:]]
        guard CVPixelBufferPoolCreate(kCFAllocatorDefault, nil, attributes as CFDictionary, &pool) == kCVReturnSuccess else {
            throw CaptureFailure("Не удалось подготовить кадры камеры.")
        }
        compositePool = pool
        cameraSession = capture
        for name in [Notification.Name.AVCaptureSessionRuntimeError, Notification.Name.AVCaptureSessionWasInterrupted] {
            let observer = NotificationCenter.default.addObserver(forName: name, object: capture, queue: nil) { [weak self] _ in
                guard let self else { return }
                self.queue.async {
                    if !self.stopping { self.fail("Камера отключилась или её захват был прерван. Доступная часть видео сохранена.") }
                }
            }
            cameraObservers.append(observer)
        }
        capture.startRunning()
    }

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard !stopping, let frame = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        cameraFrame = frame
        cameraFrames += 1
    }

    func compositingCamera(_ sample: CMSampleBuffer) -> CMSampleBuffer? {
        guard let cameraFrame, let original = CMSampleBufferGetImageBuffer(sample), let compositePool else { return sample }
        let background = CIImage(cvPixelBuffer: original)
        let foreground = CIImage(cvPixelBuffer: cameraFrame)
        let margin = max(8.0, Double(outputWidth) * 0.015)
        let cameraWidth = max(64.0, Double(outputWidth) * 0.23)
        let cameraHeight = cameraWidth * 0.75
        let target = CGRect(x: Double(outputWidth) - cameraWidth - margin, y: margin, width: cameraWidth, height: cameraHeight)
        let scale = max(target.width / foreground.extent.width, target.height / foreground.extent.height)
        var overlay = foreground.transformed(by: CGAffineTransform(scaleX: scale, y: scale))
        overlay = overlay.transformed(by: CGAffineTransform(translationX: target.midX - overlay.extent.midX, y: target.midY - overlay.extent.midY)).cropped(to: target)
        if let mask = CIFilter(name: "CIRoundedRectangleGenerator", parameters: ["inputExtent": CIVector(cgRect: target), "inputRadius": margin, "inputColor": CIColor.white])?.outputImage {
            overlay = overlay.applyingFilter("CIBlendWithMask", parameters: [kCIInputBackgroundImageKey: CIImage.empty(), kCIInputMaskImageKey: mask])
        }
        let combined = overlay.composited(over: background)
        var pixel: CVPixelBuffer?
        guard CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, compositePool, &pixel) == kCVReturnSuccess, let pixel else { return nil }
        imageContext.render(combined, to: pixel)
        var format: CMVideoFormatDescription?
        guard CMVideoFormatDescriptionCreateForImageBuffer(allocator: kCFAllocatorDefault, imageBuffer: pixel, formatDescriptionOut: &format) == noErr, let format else { return nil }
        var timing = CMSampleTimingInfo()
        guard CMSampleBufferGetSampleTimingInfo(sample, at: 0, timingInfoOut: &timing) == noErr else { return nil }
        var result: CMSampleBuffer?
        guard CMSampleBufferCreateReadyWithImageBuffer(allocator: kCFAllocatorDefault, imageBuffer: pixel, formatDescription: format, sampleTiming: &timing, sampleBufferOut: &result) == noErr else { return nil }
        return result
    }

    func prepareLegacyMicrophone() throws {
        let micEngine = AVAudioEngine()
        let input = micEngine.inputNode
        if let requested = arguments.microphone {
            var property = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
            var size: UInt32 = 0
            guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &property, 0, nil, &size) == noErr else { throw CaptureFailure("Не удалось получить список звуковых устройств.") }
            var devices = [AudioDeviceID](repeating: 0, count: Int(size) / MemoryLayout<AudioDeviceID>.size)
            guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &property, 0, nil, &size, &devices) == noErr else { throw CaptureFailure("Не удалось прочитать звуковые устройства.") }
            var selected: AudioDeviceID?
            for device in devices {
                var uid: Unmanaged<CFString>?
                var bytes = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
                var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyDeviceUID, mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
                if AudioObjectGetPropertyData(device, &address, 0, nil, &bytes, &uid) == noErr, let value = uid?.takeRetainedValue(), value as String == requested { selected = device; break }
            }
            guard var selected, let unit = input.audioUnit else { throw CaptureFailure("Выбранный микрофон больше не доступен.") }
            guard AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &selected, UInt32(MemoryLayout<AudioDeviceID>.size)) == noErr else { throw CaptureFailure("Операционная система не разрешила выбрать этот микрофон.") }
        }
        let format = input.outputFormat(forBus: 0)
        guard format.sampleRate > 0, format.channelCount > 0 else { throw CaptureFailure("Микрофон не передаёт звук.") }
        input.installTap(onBus: 0, bufferSize: 1024, format: format) { [weak self] buffer, when in
            guard let self, when.isHostTimeValid, let sample = Self.audioSample(buffer, hostTime: when.hostTime) else { return }
            self.queue.async { self.accept(sample, kind: "microphone") }
        }
        micEngine.prepare()
        try micEngine.start()
        engine = micEngine
    }

    static func audioSample(_ buffer: AVAudioPCMBuffer, hostTime: UInt64) -> CMSampleBuffer? {
        var description: CMAudioFormatDescription?
        guard CMAudioFormatDescriptionCreate(allocator: kCFAllocatorDefault, asbd: buffer.format.streamDescription,
              layoutSize: 0, layout: nil, magicCookieSize: 0, magicCookie: nil, extensions: nil, formatDescriptionOut: &description) == noErr,
              let description else { return nil }
        var timing = CMSampleTimingInfo(duration: CMTime(value: 1, timescale: CMTimeScale(buffer.format.sampleRate)),
            presentationTimeStamp: CMTime(seconds: AVAudioTime.seconds(forHostTime: hostTime), preferredTimescale: 1_000_000_000),
            decodeTimeStamp: .invalid)
        var sample: CMSampleBuffer?
        guard CMSampleBufferCreate(allocator: kCFAllocatorDefault, dataBuffer: nil, dataReady: false,
            makeDataReadyCallback: nil, refcon: nil, formatDescription: description,
            sampleCount: CMItemCount(buffer.frameLength), sampleTimingEntryCount: 1, sampleTimingArray: &timing,
            sampleSizeEntryCount: 0, sampleSizeArray: nil, sampleBufferOut: &sample) == noErr,
            let sample else { return nil }
        guard CMSampleBufferSetDataBufferFromAudioBufferList(sample, blockBufferAllocator: kCFAllocatorDefault,
            blockBufferMemoryAllocator: kCFAllocatorDefault, flags: 0, bufferList: buffer.audioBufferList) == noErr else { return nil }
        CMSampleBufferSetDataReady(sample)
        return sample
    }

    func stream(_ stream: SCStream, didOutputSampleBuffer sample: CMSampleBuffer, of type: SCStreamOutputType) {
        switch type {
        case .screen:
            guard CMSampleBufferIsValid(sample), CMSampleBufferGetImageBuffer(sample) != nil else { return }
            if let attachments = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
               let status = attachments.first?[.status] as? Int, status != SCFrameStatus.complete.rawValue { return }
            accept(sample, kind: "video")
        case .audio: accept(sample, kind: "system")
        default:
            if #available(macOS 15.0, *), type == .microphone { accept(sample, kind: "microphone") }
        }
    }

    func accept(_ sample: CMSampleBuffer, kind: String) {
        guard !stopping, fatalMessage == nil, CMSampleBufferIsValid(sample) else { return }
        guard let writer, writer.status == .writing else {
            if let error = writer?.error { fail(error.localizedDescription) }
            return
        }
        let input = kind == "video" ? video : (kind == "system" ? systemAudio : microphone)
        guard let input, input.isReadyForMoreMediaData else { return }
        let prepared: CMSampleBuffer
        if kind == "video" && arguments.camera {
            guard let composed = compositingCamera(sample) else { fail("Не удалось наложить камеру на видео."); return }
            prepared = composed
        } else { prepared = sample }
        let sourceTime = CMSampleBufferGetPresentationTimeStamp(prepared).seconds
        guard let mediaTime = timeline.map(sourceTime, track: kind), let offset = timeline.offset,
              let retimed = Self.retime(prepared, subtracting: offset) else { return }
        if !input.append(retimed) {
            fail(writer.error?.localizedDescription ?? "Ошибка записи дорожки \(kind).")
            return
        }
        if kind == "video" { frames += 1; lastVideo = prepared; lastVideoTime = mediaTime }
        else if kind == "system" { audioSamples += 1 }
        else { microphoneSamples += 1 }
    }

    static func retime(_ sample: CMSampleBuffer, subtracting offset: Double) -> CMSampleBuffer? {
        var count = 0
        guard CMSampleBufferGetSampleTimingInfoArray(sample, entryCount: 0, arrayToFill: nil, entriesNeededOut: &count) == noErr,
              count > 0 else { return nil }
        var info = [CMSampleTimingInfo](repeating: CMSampleTimingInfo(duration: .invalid, presentationTimeStamp: .invalid, decodeTimeStamp: .invalid), count: count)
        guard CMSampleBufferGetSampleTimingInfoArray(sample, entryCount: count, arrayToFill: &info, entriesNeededOut: &count) == noErr else { return nil }
        let shift = CMTime(seconds: offset, preferredTimescale: 1_000_000_000)
        for i in info.indices {
            if info[i].presentationTimeStamp.isValid { info[i].presentationTimeStamp = CMTimeSubtract(info[i].presentationTimeStamp, shift) }
            if info[i].decodeTimeStamp.isValid { info[i].decodeTimeStamp = CMTimeSubtract(info[i].decodeTimeStamp, shift) }
        }
        var copy: CMSampleBuffer?
        guard CMSampleBufferCreateCopyWithNewTiming(allocator: kCFAllocatorDefault, sampleBuffer: sample,
            sampleTimingEntryCount: count, sampleTimingArray: &info, sampleBufferOut: &copy) == noErr else { return nil }
        return copy
    }

    func pause() {
        queue.async {
            guard !self.stopping, !self.timeline.isPaused else { return }
            self.timeline.pause(at: hostSeconds())
            emit(["event": "paused"])
        }
    }
    func resume() {
        queue.async {
            guard !self.stopping, self.timeline.isPaused else { return }
            self.timeline.resume(at: hostSeconds())
            emit(["event": "resumed"])
        }
    }
    func fail(_ message: String) {
        guard fatalMessage == nil else { return }
        fatalMessage = message
        emit(["event": "error", "message": message])
        Task { await self.stop() }
    }
    func stream(_ stream: SCStream, didStopWithError error: Error) {
        queue.async { self.fail("Источник захвата остановлен: \(error.localizedDescription)") }
    }

    func stop() async {
        let endHostTime = hostSeconds()
        let shouldStop: Bool = await withCheckedContinuation { continuation in
            queue.async {
                if self.stopping { continuation.resume(returning: false); return }
                self.stopping = true
                continuation.resume(returning: true)
            }
        }
        guard shouldStop else { return }
        if let engine { engine.inputNode.removeTap(onBus: 0); engine.stop() }
        cameraSession?.stopRunning()
        cameraObservers.forEach { NotificationCenter.default.removeObserver($0) }
        cameraObservers.removeAll()
        if let stream { try? await stream.stopCapture() }
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            queue.async {
                // Extend a static desktop to the actual stop time; otherwise a single unchanged frame
                // would create a nearly zero-length movie. Preserve the recorded pause duration.
                if let original = self.lastVideo, let video = self.video, video.isReadyForMoreMediaData,
                   let writer = self.writer, writer.status == .writing {
                    let end = self.timeline.isPaused ? self.timeline.pausedAt! : endHostTime
                    if self.timeline.isPaused { self.timeline.resume(at: end) }
                    if let mapped = self.timeline.map(end, track: "video"), mapped > self.lastVideoTime,
                       let frame = Self.retime(original, subtracting: CMSampleBufferGetPresentationTimeStamp(original).seconds - mapped) {
                        if video.append(frame) { self.lastVideoTime = mapped }
                    }
                }
                self.video?.markAsFinished()
                self.systemAudio?.markAsFinished()
                self.microphone?.markAsFinished()
                continuation.resume()
            }
        }
        guard let writer else { finishOnce(); return }
        if frames == 0 {
            writer.cancelWriting()
            emit(["event": "error", "message": fatalMessage ?? "Запись не получила ни одного кадра. Проверьте разрешение и источник."])
            finishOnce()
            return
        }
        if fatalMessage == nil {
            if (arguments.audio == "system" || arguments.audio == "both") && audioSamples == 0 {
                fatalMessage = "Источник не передал ни одного буфера системного звука. Видеофайл сохранён, но запись со звуком не подтверждена."
            } else if (arguments.audio == "mic" || arguments.audio == "both") && microphoneSamples == 0 {
                fatalMessage = "Микрофон не передал ни одного буфера звука. Видеофайл сохранён, но запись с микрофоном не подтверждена."
            }
        }
        if arguments.camera && cameraFrames == 0 && fatalMessage == nil {
            fatalMessage = "Камера не передала ни одного кадра. Запись с камерой не подтверждена."
        }
        await writer.finishWriting()
        if writer.status == .completed && fatalMessage == nil {
            let tracks = (audioSamples > 0 ? 1 : 0) + (microphoneSamples > 0 ? 1 : 0)
            emit(["event": "finished", "path": arguments.output, "frames": frames,
                  "audioTracks": tracks, "duration": lastVideoTime, "systemAudioSamples": audioSamples,
                  "microphoneSamples": microphoneSamples, "cameraFrames": cameraFrames])
        } else {
            emit(["event": "error", "message": fatalMessage ?? writer.error?.localizedDescription ?? "Не удалось завершить MP4.", "path": arguments.output])
        }
        finishOnce()
    }
}

@main
struct CaptureMain {
    static var session: CaptureSession?
    static var source: DispatchSourceSignal?
    static var exitCode: Int32 = 0
    static func main() async {
        do {
            let options = try CaptureArguments.parse(Array(CommandLine.arguments.dropFirst()))
            if options.listMicrophones {
                let devices: [AVCaptureDevice]
                if #available(macOS 14.0, *) {
                    devices = AVCaptureDevice.DiscoverySession(deviceTypes: [.microphone], mediaType: .audio, position: .unspecified).devices
                } else {
                    devices = AVCaptureDevice.DiscoverySession(deviceTypes: [.builtInMicrophone, .externalUnknown], mediaType: .audio, position: .unspecified).devices
                }
                emit(["microphones": devices.map { ["id": $0.uniqueID, "name": $0.localizedName] }])
                return
            }
            if let input = options.mixAudioPath {
                try await NativeAudioMixer.mix(input: input, output: options.output)
                return
            }
            if let output = options.screenshotPath {
                guard #available(macOS 13.0, *) else { throw CaptureFailure("Снимок экрана требует macOS 13 или новее.") }
                try await NativeScreenshot.save(options, output: output)
                return
            }
            if let input = options.ocrPath {
                let request = VNRecognizeTextRequest()
                request.recognitionLevel = .accurate
                request.usesLanguageCorrection = true
                let supported = try request.supportedRecognitionLanguages()
                request.recognitionLanguages = ["ru-RU", "en-US"].filter { supported.contains($0) }
                guard !request.recognitionLanguages.isEmpty else { throw CaptureFailure("На этой macOS нет подходящего языка распознавания.") }
                let source = URL(fileURLWithPath: input)
                guard let imageSource = CGImageSourceCreateWithURL(source as CFURL, nil), let image = CGImageSourceCreateImageAtIndex(imageSource, 0, nil) else { throw CaptureFailure("Не удалось открыть изображение для распознавания.") }
                try VNImageRequestHandler(cgImage: image).perform([request])
                let text = (request.results ?? []).compactMap { $0.topCandidates(1).first?.string }.joined(separator: "\n")
                emit(["event": "ocr", "text": text, "languages": request.recognitionLanguages])
                return
            }
            if options.help {
                print("kadrolov-capture --output FILE.mp4 [--display ID] [--window ID] [--rect x,y,w,h] [--fps 1...60] [--width W --height H] [--quality 0...51] [--audio none|system|mic|both] [--cursor 0|1] [--clicks 0|1] [--microphone DEVICE_ID] [--exclude-pid PID]\nstdin JSON lines: {\"command\":\"pause\"}, resume, stop. macOS 13+; native click highlights/mac microphone selection macOS 15+. --ocr IMAGE runs offline Vision OCR; --camera 0|1 --camera-device ID overlays webcam.")
                return
            }
            if options.requestPermission {
                emit(["event": "permission", "screenRecording": CGRequestScreenCaptureAccess(), "restartRequired": true])
                return
            }
            if options.checkPermission {
                emit(["event": "permission", "screenRecording": CGPreflightScreenCaptureAccess(),
                      "microphone": AVCaptureDevice.authorizationStatus(for: .audio) == .authorized,
                      "nativeClicks": ProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 15,
                      "camera": AVCaptureDevice.authorizationStatus(for: .video) == .authorized])
                return
            }
            if options.listDisplays {
                var count: UInt32 = 0
                CGGetActiveDisplayList(0, nil, &count)
                var identifiers = [CGDirectDisplayID](repeating: 0, count: Int(count))
                CGGetActiveDisplayList(count, &identifiers, &count)
                let screens: [[String: Any]] = identifiers.prefix(Int(count)).map { id in
                    let bounds = CGDisplayBounds(id)
                    let name = NSScreen.screens.first { ($0.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value == id }?.localizedName ?? "Монитор"
                    return ["id": id, "name": name, "x": bounds.minX, "y": bounds.minY,
                            "width": bounds.width, "height": bounds.height,
                            "pixelWidth": CGDisplayPixelsWide(id), "pixelHeight": CGDisplayPixelsHigh(id),
                            "main": id == CGMainDisplayID()]
                }
                emit(["event": "displays", "displays": screens])
                return
            }
            guard #available(macOS 13.0, *) else { throw CaptureFailure("Модуль записи требует macOS 13 или новее.") }
            if options.listWindows {
                guard CGPreflightScreenCaptureAccess() else { throw CaptureFailure("Нет разрешения записи экрана.") }
                let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
                let windows: [[String: Any]] = content.windows.filter { $0.windowLayer == 0 && $0.frame.width > 1 && $0.frame.height > 1 }.map {
                    ["id": $0.windowID, "title": $0.title ?? "", "application": $0.owningApplication?.applicationName ?? "",
                     "pid": $0.owningApplication?.processID ?? 0,
                     "x": $0.frame.minX, "y": $0.frame.minY, "width": $0.frame.width, "height": $0.frame.height]
                }
                emit(["event": "windows", "windows": windows])
                return
            }
            let current = CaptureSession(options)
            session = current
            await withCheckedContinuation { (done: CheckedContinuation<Void, Never>) in
                current.finish = { done.resume() }
                Task {
                    do {
                        try await current.prepare()
                        startInput(current)
                    } catch {
                        current.writer?.cancelWriting()
                        emit(["event": "error", "message": error.localizedDescription])
                        exitCode = 1
                        current.finishOnce()
                    }
                }
            }
            if current.fatalMessage != nil || current.frames == 0 { exitCode = 1 }
            exit(exitCode)
        } catch {
            emit(["event": "error", "message": error.localizedDescription])
            exit(1)
        }
    }
    @available(macOS 13.0, *)
    static func startInput(_ session: CaptureSession) {
        signal(SIGTERM, SIG_IGN)
        let term = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .global())
        term.setEventHandler { Task { await session.stop() } }
        term.resume()
        source = term
        DispatchQueue.global(qos: .utility).async {
            while let line = readLine() {
                guard let bytes = line.data(using: .utf8),
                      let request = (try? JSONSerialization.jsonObject(with: bytes)) as? [String: Any],
                      let command = request["command"] as? String else {
                    emit(["event": "warning", "message": "Ожидалась JSON-команда pause, resume или stop."])
                    continue
                }
                switch command {
                case "pause": session.pause()
                case "resume": session.resume()
                case "stop": Task { await session.stop() }; return
                default: emit(["event": "warning", "message": "Неизвестная команда."])
                }
            }
            // A closed controller must not leave an invisible capture running.
            Task { await session.stop() }
        }
    }
}
