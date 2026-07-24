import UIKit
import GLKit

class PlayerViewController: GLKViewController {

    private let player = CCPlayer()
    private var eaglContext: EAGLContext!

    override func viewDidLoad() {
        super.viewDidLoad()

        eaglContext = EAGLContext(api: .openGLES3)
        view = self.view
        (view as! GLKView).context = eaglContext
        (view as! GLKView).drawableDepthFormat = .formatNone

        player.delegate = self
        player.setSurface(eaglContext)
        player.setDataSource(Bundle.main.path(forResource: "sample", ofType: "mp4") ?? "")
        player.prepareAsync()
    }

    override func glkView(_ view: GLKView, drawIn rect: CGRect) {
        // Render loop handled by C++ render thread
    }

    override func viewWillDisappear(_ animated: Bool) {
        player.pause()
        super.viewWillDisappear(animated)
    }

    deinit {
        player.release()
    }
}

extension PlayerViewController: CCPlayerDelegate {
    func playerDidPrepare(_ sender: Any) {
        player.start()
    }

    func player(_ sender: Any, didFailWithError errorCode: Int) {
        print("Player error: \(errorCode)")
    }

    func player(_ sender: Any, didUpdateProgress currentMs: TimeInterval, duration durationMs: TimeInterval) {
        // Update UI progress bar
    }
}
