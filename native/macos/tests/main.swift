import Foundation
import CoreGraphics

func check(_ value: @autoclosure () -> Bool, _ message: String) {
    if !value() { fputs("FAIL: \(message)\n", stderr); exit(1) }
}
var clock = PauseTimeline()
check(clock.map(100) == 0, "first frame is timeline zero")
check(clock.map(102) == 2, "recording advances")
clock.pause(at: 103)
check(clock.map(104) == nil, "paused frames are discarded")
clock.resume(at: 108)
check(clock.map(107.9) == nil, "delayed frames from pause are discarded")
check(clock.map(109) == 4, "pause duration removed")
clock.pause(at: 110)
clock.pause(at: 112)
clock.resume(at: 115)
check(clock.map(116) == 6, "double pause does not reset pause boundary")
check(clock.map(116, track: "audio") == 6, "tracks share same anchor")
check(clock.map(115.5, track: "audio") == nil, "out of order audio discarded")
let bounds = CGRect(x: -1440, y: 200, width: 1440, height: 900)
let area = try CaptureGeometry.region(CGRect(x: -1340, y: 250, width: 800, height: 600), in: bounds)
check(area.origin.x == 100 && area.origin.y == 50, "negative monitor origin converted to local")
do { _ = try CaptureGeometry.region(CGRect(x: -1500, y: 250, width: 800, height: 600), in: bounds); check(false,"partial region rejected") } catch {}
check(CaptureGeometry.evenSize(1279) == 1278, "encoder width rounds to even")
check(CaptureGeometry.evenSize(1) == 2, "minimum codec dimension")
do { _ = try CaptureArguments.parse(["--output","test.mp4","--fps","0"]); check(false,"zero FPS rejected") } catch {}
do { _ = try CaptureArguments.parse(["--output","test.mp4","--audio","invalid"]); check(false,"invalid audio rejected") } catch {}
do { _ = try CaptureArguments.parse(["--output","test.mp4","--unknown","x"]); check(false,"unknown args rejected") } catch {}
let args = try CaptureArguments.parse(["--output","/tmp/test.mp4","--audio","system","--cursor","0","--clicks","1","--rect","-100,50,800,600"])
check(args.audio == "system" && !args.cursor && args.clicks && args.rect!.minX == -100, "CLI options retained")
let screenshotArgs = try CaptureArguments.parse(["--screenshot", "/tmp/test.png", "--cursor", "0"])
check(screenshotArgs.screenshotPath != nil && !screenshotArgs.cursor, "screenshot mode without record output")
let ocrArgs = try CaptureArguments.parse(["--ocr", "/tmp/test.png"])
check(ocrArgs.ocrPath != nil, "offline OCR mode without record output")
let mixArgs = try CaptureArguments.parse(["--mix-audio", "/tmp/raw.mp4", "--output", "/tmp/mixed.mp4"])
check(mixArgs.mixAudioPath != nil, "native mix mode")
do { _ = try CaptureArguments.parse(["--screenshot", "/tmp/test.png", "--width", "1", "--height", "1"]); check(false,"invalid screenshot dimensions rejected") } catch {}
print("PASS: pause timeline, delayed/out-of-order samples, geometry and CLI validation")
