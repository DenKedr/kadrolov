#!/usr/bin/env swift
// Run from repository root: swift scripts/render-icon.swift
// Original deterministic artwork; AppKit is the only dependency.
import AppKit
import Foundation
let out=URL(fileURLWithPath:FileManager.default.currentDirectoryPath).appendingPathComponent("assets/icons")
try FileManager.default.createDirectory(at:out,withIntermediateDirectories:true)
let orange=NSColor(srgbRed:194/255,green:65/255,blue:12/255,alpha:1), cream=NSColor(srgbRed:1,green:247/255,blue:237/255,alpha:1), dark=NSColor(srgbRed:32/255,green:37/255,blue:43/255,alpha:1)
func path(_ a:[CGFloat],_ close:Bool=false)->NSBezierPath{let p=NSBezierPath();p.move(to:NSPoint(x:a[0],y:a[1]));for i in stride(from:2,to:a.count,by:2){p.line(to:NSPoint(x:a[i],y:a[i+1]))};if close{p.close()};return p}
func png(_ n:Int)->Data{let r=NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:n,pixelsHigh:n,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!;NSGraphicsContext.saveGraphicsState();NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:r);let t=NSAffineTransform();t.translateX(by:0,yBy:CGFloat(n));t.scaleX(by:CGFloat(n)/1024,yBy:-CGFloat(n)/1024);t.concat();orange.setFill();NSBezierPath(roundedRect:NSRect(x:64,y:64,width:896,height:896),xRadius:204,yRadius:204).fill();cream.setStroke();for a:[CGFloat] in [[264,408,264,272,408,272],[616,272,760,272,760,408],[264,592,264,728,408,728],[760,592,760,728,616,728]]{let p=path(a);p.lineWidth=56;p.lineCapStyle = .round;p.lineJoinStyle = .round;p.stroke()};let p=path([466,416,466,720,546,642,607,776,685,740,620,608,732,600],true);p.lineWidth=28;p.lineJoinStyle = .round;cream.setStroke();p.stroke();dark.setFill();p.fill();NSGraphicsContext.restoreGraphicsState();return r.representation(using:.png,properties:[:])!}
for n in [16,32,48,64,128,256,512,1024]{try png(n).write(to:out.appendingPathComponent("kadrolov-\(n).png"))}
let svg="""
<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
<title>Kadrolov application icon</title>
<rect x="64" y="64" width="896" height="896" rx="204" fill="#C2410C"/>
<path d="M264 408V272H408 M616 272H760V408 M264 592V728H408 M760 592V728H616" fill="none" stroke="#FFF7ED" stroke-width="56" stroke-linecap="round" stroke-linejoin="round"/>
<path d="M466 416V720L546 642L607 776L685 740L620 608L732 600Z" fill="#20252B" stroke="#FFF7ED" stroke-width="28" stroke-linejoin="round" paint-order="stroke fill"/>
</svg>
"""
try svg.write(to:out.appendingPathComponent("kadrolov.svg"),atomically:true,encoding:.utf8)
var ico=Data();func u16(_ n:UInt16){var v=n.littleEndian;withUnsafeBytes(of:&v){ico.append(contentsOf:$0)}};func u32(_ n:UInt32){var v=n.littleEndian;withUnsafeBytes(of:&v){ico.append(contentsOf:$0)}}
let sizes=[16,32,48,64,128,256], images=[16,32,48,64,128,256].map{png($0)};u16(0);u16(1);u16(6);var offset=102
for (i,n) in sizes.enumerated(){ico.append(contentsOf:[n==256 ? 0:UInt8(n),n==256 ? 0:UInt8(n),0,0]);u16(1);u16(32);u32(UInt32(images[i].count));u32(UInt32(offset));offset += images[i].count};for d in images{ico.append(d)};try ico.write(to:out.appendingPathComponent("kadrolov.ico"))
let iconset=out.appendingPathComponent("Kadrolov.iconset");try FileManager.default.createDirectory(at:iconset,withIntermediateDirectories:true)
for n in [16,32,128,256,512]{try png(n).write(to:iconset.appendingPathComponent("icon_\(n)x\(n).png"));try png(n*2).write(to:iconset.appendingPathComponent("icon_\(n)x\(n)@2x.png"))}
let p=Process();p.executableURL=URL(fileURLWithPath:"/usr/bin/iconutil");p.arguments=["-c","icns",iconset.path,"-o",out.appendingPathComponent("kadrolov.icns").path];try p.run();p.waitUntilExit();guard p.terminationStatus==0 else{fatalError("iconutil failed")};print("Created icons at \(out.path)")
