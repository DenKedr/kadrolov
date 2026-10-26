import AVFoundation
import Foundation

/// Offline AAC mixing with compressed H.264 passthrough: no network/FFmpeg and no extra video generation loss.
@available(macOS 13.0, *)
enum NativeAudioMixer {
    static func mix(input: String, output: String) async throws {
        let source = URL(fileURLWithPath: input).standardizedFileURL
        let destination = URL(fileURLWithPath: output).standardizedFileURL
        guard source != destination, !output.isEmpty, destination.pathExtension.lowercased() == "mp4" else {
            throw CaptureFailure("Для смешивания выберите отдельный выходной MP4.")
        }
        guard FileManager.default.isReadableFile(atPath: source.path), !FileManager.default.fileExists(atPath: destination.path) else {
            throw CaptureFailure("Исходная запись недоступна или конечный файл уже существует.")
        }
        let asset = AVURLAsset(url: source)
        let videos = try await asset.loadTracks(withMediaType: .video)
        let audios = try await asset.loadTracks(withMediaType: .audio)
        guard videos.count == 1, audios.count >= 2 else { throw CaptureFailure("Для смешивания нужны одна видеодорожка и минимум две звуковые дорожки.") }
        let formats = try await videos[0].load(.formatDescriptions)
        guard let videoFormat = formats.first else { throw CaptureFailure("Формат видеодорожки не найден.") }
        let reader = try AVAssetReader(asset: asset)
        let videoOutput = AVAssetReaderTrackOutput(track: videos[0], outputSettings: nil)
        videoOutput.alwaysCopiesSampleData = false
        let pcmSettings: [String: Any] = [AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: 48000, AVNumberOfChannelsKey: 2,
            AVLinearPCMBitDepthKey: 32, AVLinearPCMIsFloatKey: true,
            AVLinearPCMIsNonInterleaved: false]
        let audioOutput = AVAssetReaderAudioMixOutput(audioTracks: audios, audioSettings: pcmSettings)
        let mix = AVMutableAudioMix()
        mix.inputParameters = audios.map { track in
            let parameter = AVMutableAudioMixInputParameters(track: track)
            parameter.setVolume(1.0 / Float(audios.count), at: .zero)
            return parameter
        }
        audioOutput.audioMix = mix
        guard reader.canAdd(videoOutput), reader.canAdd(audioOutput) else { throw CaptureFailure("Не удалось открыть дорожки для смешивания.") }
        reader.add(videoOutput)
        reader.add(audioOutput)
        let writer = try AVAssetWriter(outputURL: destination, fileType: .mp4)
        let videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: nil, sourceFormatHint: videoFormat)
        let audioInput = AVAssetWriterInput(mediaType: .audio, outputSettings: [AVFormatIDKey: kAudioFormatMPEG4AAC,
            AVSampleRateKey: 48000, AVNumberOfChannelsKey: 2, AVEncoderBitRateKey: 192000])
        videoInput.expectsMediaDataInRealTime = false
        audioInput.expectsMediaDataInRealTime = false
        guard writer.canAdd(videoInput), writer.canAdd(audioInput) else { throw CaptureFailure("Не удалось создать смешанную запись.") }
        writer.add(videoInput)
        writer.add(audioInput)
        guard writer.startWriting(), reader.startReading() else { throw writer.error ?? reader.error ?? CaptureFailure("Не удалось начать смешивание дорожек.") }
        writer.startSession(atSourceTime: .zero)
        do {
            try transfer(reader, writer: writer, outputs: [videoOutput, audioOutput], inputs: [videoInput, audioInput])
            await writer.finishWriting()
            guard writer.status == .completed else { throw writer.error ?? CaptureFailure("Не удалось сохранить смешанную запись.") }
            emit(["event": "finished", "path": destination.path, "audioTracks": 1, "videoPassthrough": true])
        } catch {
            reader.cancelReading()
            writer.cancelWriting()
            throw error
        }
    }

    private static func transfer(_ reader: AVAssetReader, writer: AVAssetWriter,
                                  outputs: [AVAssetReaderOutput], inputs: [AVAssetWriterInput]) throws {
        var completed = [Bool](repeating: false, count: inputs.count)
        var lastProgress = Date()
        while completed.contains(false) {
            if writer.status == .failed || reader.status == .failed {
                throw writer.error ?? reader.error ?? CaptureFailure("Ошибка обработки дорожек.")
            }
            var progressed = false
            for index in inputs.indices where !completed[index] && inputs[index].isReadyForMoreMediaData {
                try autoreleasepool {
                    if let sample = outputs[index].copyNextSampleBuffer() {
                        guard inputs[index].append(sample) else { throw writer.error ?? CaptureFailure("Не удалось записать дорожку.") }
                    } else {
                        inputs[index].markAsFinished()
                        completed[index] = true
                    }
                }
                progressed = true
            }
            if progressed { lastProgress = Date() }
            else {
                guard Date().timeIntervalSince(lastProgress) < 30 else { throw CaptureFailure("Обработка дорожек не отвечает. Исходная запись сохранена.") }
                Thread.sleep(forTimeInterval: 0.002)
            }
        }
        guard reader.status != .failed else { throw reader.error ?? CaptureFailure("Не удалось прочитать запись.") }
    }
}
