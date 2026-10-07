// Prints the CGWindowID of the BounceShot editor window (title "BOUNCE"), for screencapture -l.
import CoreGraphics
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
for w in list where (w[kCGWindowOwnerName as String] as? String) == "BounceShot" && (w[kCGWindowName as String] as? String) == "BOUNCE" {
    print(w[kCGWindowNumber as String]!); break
}
