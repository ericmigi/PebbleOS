// Final camera daemon (bundle id com.pebble.grabd). Holds the IPEVO open via
// AVCaptureVideoDataOutput (does NOT wedge the camera like MovieFileOutput).
//   touch /tmp/grab_request              -> writes one JPEG to /tmp/watch.jpg
//   echo <seconds> > /tmp/burst_request  -> writes ~8fps JPEGs to /tmp/burst/NNNN.jpg
import AVFoundation
import CoreImage
import Foundation
import AppKit

let want = "IPEVO"
let reqPath = "/tmp/grab_request"
let burstReq = "/tmp/burst_request"
let outPath = "/tmp/watch.jpg"
let burstDir = "/tmp/burst"

let session = AVCaptureSession()
session.sessionPreset = .high
let devices = AVCaptureDevice.DiscoverySession(
  deviceTypes: [.builtInWideAngleCamera, .external],
  mediaType: .video, position: .unspecified).devices
guard let cam = devices.first(where: { $0.localizedName.contains(want) }) ?? devices.first else {
  print("NO_CAMERA"); exit(2)
}
print("DAEMON2 using \(cam.localizedName)")
guard let input = try? AVCaptureDeviceInput(device: cam), session.canAddInput(input) else { exit(3) }
session.addInput(input)
let out = AVCaptureVideoDataOutput()
out.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
out.alwaysDiscardsLateVideoFrames = true

final class D: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
  var latest: CGImage?
  let ctx = CIContext()
  var bursting = false
  var burstEnd = 0.0
  var burstN = 0
  var lastWrite = 0.0
  func captureOutput(_ o: AVCaptureOutput, didOutput s: CMSampleBuffer, from c: AVCaptureConnection) {
    guard let pb = CMSampleBufferGetImageBuffer(s) else { return }
    let ci = CIImage(cvPixelBuffer: pb)
    guard let cg = ctx.createCGImage(ci, from: ci.extent) else { return }
    latest = cg
    if bursting {
      let now = ProcessInfo.processInfo.systemUptime
      if now >= burstEnd { bursting = false; print("BURST_DONE \(burstN)"); return }
      if now - lastWrite >= 0.125 {   // ~8 fps
        lastWrite = now
        let p = String(format: "%@/%04d.jpg", burstDir, burstN); burstN += 1
        save(cg, p)
      }
    }
  }
  func save(_ cg: CGImage, _ path: String) {
    let rep = NSBitmapImageRep(cgImage: cg)
    if let d = rep.representation(using: .jpeg, properties: [.compressionFactor: 0.9]) {
      try? d.write(to: URL(fileURLWithPath: path))
    }
  }
}
let d = D()
out.setSampleBufferDelegate(d, queue: DispatchQueue(label: "cap"))
guard session.canAddOutput(out) else { exit(4) }
session.addOutput(out)
session.startRunning()

let fm = FileManager.default
Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { _ in
  if fm.fileExists(atPath: reqPath) {
    if let cg = d.latest { d.save(cg, outPath); print("GRABBED") } else { print("NO_FRAME") }
    try? fm.removeItem(atPath: reqPath)
  }
  if let secsStr = try? String(contentsOfFile: burstReq, encoding: .utf8),
     let secs = Double(secsStr.trimmingCharacters(in: .whitespacesAndNewlines)) {
    try? fm.removeItem(atPath: burstReq)
    try? fm.removeItem(atPath: burstDir)
    try? fm.createDirectory(atPath: burstDir, withIntermediateDirectories: true)
    d.burstN = 0; d.lastWrite = 0
    d.burstEnd = ProcessInfo.processInfo.systemUptime + secs
    d.bursting = true
    print("BURST_START \(secs)s")
  }
}
RunLoop.main.run()
