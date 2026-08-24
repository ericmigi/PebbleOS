import AVFoundation
import CoreImage
import Foundation
import AppKit

let outPath = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "/tmp/watch.jpg"
let want = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "IPEVO"

let session = AVCaptureSession()
session.sessionPreset = .high

let devices = AVCaptureDevice.DiscoverySession(
  deviceTypes: [.builtInWideAngleCamera, .externalUnknown],
  mediaType: .video, position: .unspecified).devices

guard let cam = devices.first(where: { $0.localizedName.contains(want) }) ?? devices.first else {
  FileHandle.standardError.write("NO_CAMERA found=\(devices.map{$0.localizedName})\n".data(using:.utf8)!)
  exit(2)
}
FileHandle.standardError.write("USING \(cam.localizedName)\n".data(using:.utf8)!)

guard let input = try? AVCaptureDeviceInput(device: cam), session.canAddInput(input) else {
  FileHandle.standardError.write("NO_INPUT\n".data(using:.utf8)!); exit(3)
}
session.addInput(input)

let out = AVCaptureVideoDataOutput()
out.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
let q = DispatchQueue(label: "cap")

final class Grabber: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
  var done = false
  let path: String
  var frames = 0
  init(_ p: String){ path = p }
  func captureOutput(_ o: AVCaptureOutput, didOutput s: CMSampleBuffer, from c: AVCaptureConnection) {
    frames += 1
    if frames < 8 { return }          // let exposure settle
    if done { return }; done = true
    guard let pb = CMSampleBufferGetImageBuffer(s) else { return }
    let ci = CIImage(cvPixelBuffer: pb)
    let ctx = CIContext()
    guard let cg = ctx.createCGImage(ci, from: ci.extent) else { return }
    let rep = NSBitmapImageRep(cgImage: cg)
    if let d = rep.representation(using: .jpeg, properties: [.compressionFactor: 0.9]) {
      try? d.write(to: URL(fileURLWithPath: path))
      FileHandle.standardError.write("WROTE \(path) \(cg.width)x\(cg.height)\n".data(using:.utf8)!)
    }
    CFRunLoopStop(CFRunLoopGetMain())
  }
}
let g = Grabber(outPath)
out.setSampleBufferDelegate(g, queue: q)
guard session.canAddOutput(out) else { FileHandle.standardError.write("NO_OUTPUT\n".data(using:.utf8)!); exit(4) }
session.addOutput(out)
session.startRunning()

// timeout
DispatchQueue.global().asyncAfter(deadline: .now() + 8) {
  if !g.done { FileHandle.standardError.write("TIMEOUT frames=\(g.frames)\n".data(using:.utf8)!); exit(5) }
}
CFRunLoopRun()
session.stopRunning()
