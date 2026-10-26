import AppKit
let image = NSImage(size: NSSize(width: 1100, height: 250))
image.lockFocus()
NSColor.white.setFill()
NSRect(x: 0, y: 0, width: 1100, height: 250).fill()
let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 70), .foregroundColor: NSColor.black]
("Кадролов 123" as NSString).draw(at: NSPoint(x: 30, y: 140), withAttributes: attrs)
("Screen capture" as NSString).draw(at: NSPoint(x: 30, y: 40), withAttributes: attrs)
image.unlockFocus()
let bitmap = NSBitmapImageRep(data: image.tiffRepresentation!)!
try bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
