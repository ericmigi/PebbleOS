// Records the IPEVO camera to an .mov for a fixed duration. Bundle id
// com.pebble.grabd so it inherits the already-granted camera TCC permission.
// Usage: GrabVid <out.mov> <seconds>
import AVFoundation
import Foundation

let outPath = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "/tmp/watch.mov"
let secs = CommandLine.arguments.count > 2 ? Double(CommandLine.arguments[2]) ?? 65 : 65
let want = "IPEVO"

let session = AVCaptureSession()
session.sessionPreset = .high
let devices = AVCaptureDevice.DiscoverySession(
  deviceTypes: [.builtInWideAngleCamera, .external],
  mediaType: .video, position: .unspecified).devices
guard let cam = devices.first(where: { $0.localizedName.contains(want) }) ?? devices.first else {
  FileHandle.standardError.write("NO_CAMERA\n".data(using:.utf8)!); exit(2)
}
FileHandle.standardError.write("REC using \(cam.localizedName) -> \(outPath) for \(secs)s\n".data(using:.utf8)!)
guard let input = try? AVCaptureDeviceInput(device: cam), session.canAddInput(input) else { exit(3) }
session.addInput(input)
let movie = AVCaptureMovieFileOutput()
guard session.canAddOutput(movie) else { exit(4) }
session.addOutput(movie)
session.startRunning()

final class Rec: NSObject, AVCaptureFileOutputRecordingDelegate {
  func fileOutput(_ o: AVCaptureFileOutput, didFinishRecordingTo url: URL, from c: [AVCaptureConnection], error: Error?) {
    if let e = error { FileHandle.standardError.write("REC_ERR \(e)\n".data(using:.utf8)!) }
    FileHandle.standardError.write("REC_DONE \(url.path)\n".data(using:.utf8)!)
    CFRunLoopStop(CFRunLoopGetMain())
  }
}
let rec = Rec()
let url = URL(fileURLWithPath: outPath)
try? FileManager.default.removeItem(at: url)
// let exposure settle briefly, then record
DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
  movie.startRecording(to: url, recordingDelegate: rec)
  DispatchQueue.main.asyncAfter(deadline: .now() + secs) { movie.stopRecording() }
}
CFRunLoopRun()
