package com.ccplayer

import android.annotation.SuppressLint
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.View
import android.view.WindowManager
import android.widget.SeekBar
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.PopupMenu
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.ccplayer.databinding.ActivityPlayerBinding
import com.vecrates.ccplayer.CCPlayer
import androidx.core.view.isVisible

class PlayerActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private lateinit var binding: ActivityPlayerBinding
    private lateinit var player: CCPlayer

    @Volatile private var prepared = false
    @Volatile private var started = false
    @Volatile private var surfaceReady = false
    @Volatile private var isSeeking = false
    @Volatile private var userPaused = false
    @Volatile private var pausedByLifecycle = false
    @Volatile private var isPlaying = false
    @Volatile private var durationMs = 0L

    private val handler = Handler(Looper.getMainLooper())
    private val hidePanelRunnable = Runnable { hidePanel() }
    private val speedValues = floatArrayOf(0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        binding = ActivityPlayerBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.surfaceView.holder.addCallback(this)
        setupControlPanel()

        player = CCPlayer(this)
        player.listener = object : CCPlayer.Listener {
            override fun onPrepared() {
                prepared = true
                runOnUiThread {
                    binding.progressBar.visibility = View.GONE
                    // 若锁方向不会导致屏幕旋转，则 surface 已就绪，可直接 start；
                    // 若会导致旋转，等新的 surfaceChanged 再 start。
                    val willRotate = applyOrientation()
                    if (!willRotate) {
                        maybeStart()
                    }
                }
            }

            override fun onError(code: Int) {
                runOnUiThread { finish() }
            }

            override fun onProgress(currentMs: Long, durationMs: Long) {
                runOnUiThread {
                    if (durationMs > 0 && this@PlayerActivity.durationMs != durationMs) {
                        this@PlayerActivity.durationMs = durationMs
                        binding.seekBar.max = durationMs.toInt()
                        binding.seekBar.isEnabled = true
                        binding.tvDuration.text = formatTime(durationMs)
                    }
                    if (!isSeeking) {
                        binding.seekBar.progress = currentMs.toInt()
                        binding.tvCurrentTime.text = formatTime(currentMs)
                    }
                }
            }
        }

        val uri = intent.data
        if (uri == null) {
            finish()
            return
        }
        // 数据源抽象：content:// 由 native 层在 prepare 时经 opener 回调直接读取，无需先拷贝到本地
        player.setDataSource(uri.toString())
        player.prepareAsync()
    }

    /** 根据视频显示宽高锁定横/竖屏，并返回该锁定是否会导致屏幕方向变化 */
    private fun applyOrientation(): Boolean {
        val (w, h) = player.getDisplayVideoSize()
        if (w > 0 && h > 0) {
            val targetLandscape = w >= h
            val currentLandscape =
                resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
            requestedOrientation = if (targetLandscape) {
                ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            } else {
                ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
            }
            hideSystemBars()
            return currentLandscape != targetLandscape
        }
        hideSystemBars()
        return false
    }

    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    private fun maybeStart() {
        if (prepared && !started && surfaceReady) {
            started = true
            isPlaying = true
            player.start()
            syncPlayPauseButton()
        }
    }

    // ---------- 播放控制 UI ----------

    @SuppressLint("ClickableViewAccessibility")
    private fun setupControlPanel() {
        binding.btnPlayPause.setOnClickListener { onPlayPauseClicked() }
        binding.btnSpeed.setOnClickListener { showSpeedMenu() }
        binding.playerRoot.setOnTouchListener { _, e ->
            if (e.action == MotionEvent.ACTION_DOWN) togglePanel()
            false
        }
        binding.seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    binding.tvCurrentTime.text = formatTime(progress.toLong())
                    player.seekTo(progress.toLong())
                    showPanel()
                }
            }

            override fun onStartTrackingTouch(sb: SeekBar) {
                isSeeking = true
                showPanel()
            }

            override fun onStopTrackingTouch(sb: SeekBar) {
                isSeeking = false
                player.seekTo(sb.progress.toLong())
                showPanel()
            }
        })
        binding.seekBar.isEnabled = false
        showPanel()
    }

    private fun onPlayPauseClicked() {
        when (player.getState()) {
            CCPlayer.State.STARTED -> {
                player.pause()
                userPaused = true
                isPlaying = false
            }
            CCPlayer.State.PAUSED -> {
                player.resume()
                userPaused = false
                isPlaying = true
            }
            CCPlayer.State.PREPARED -> {
                player.start()
                userPaused = false
                isPlaying = true
            }
            else -> {}
        }
        syncPlayPauseButton()
        showPanel()
    }

    private fun showSpeedMenu() {
        val current = player.getSpeed()
        val popup = PopupMenu(this, binding.btnSpeed)
        speedValues.forEachIndexed { index, speed ->
            val label = (if (speed == current) "✓ " else "") + formatSpeed(speed)
            popup.menu.add(0, index, index, label)
        }
        popup.setOnMenuItemClickListener { item ->
            val speed = speedValues[item.itemId]
            player.setSpeed(speed)
            binding.btnSpeed.text = formatSpeed(speed)
            showPanel()
            true
        }
        popup.show()
    }

    private fun syncPlayPauseButton() {
        binding.btnPlayPause.setImageResource(
            if (isPlaying) R.drawable.ic_pause else R.drawable.ic_play
        )
    }

    private fun showPanel() {
        binding.controlPanel.visibility = View.VISIBLE
        handler.removeCallbacks(hidePanelRunnable)
        handler.postDelayed(hidePanelRunnable, 3000L)
    }

    private fun hidePanel() {
        binding.controlPanel.visibility = View.GONE
    }

    private fun togglePanel() {
        if (binding.controlPanel.isVisible) {
            handler.removeCallbacks(hidePanelRunnable)
            hidePanel()
        } else {
            showPanel()
        }
    }

    private fun formatTime(ms: Long): String {
        val totalSec = ms / 1000
        val h = totalSec / 3600
        val m = (totalSec % 3600) / 60
        val s = totalSec % 60
        return if (h > 0) String.format("%d:%02d:%02d", h, m, s)
        else String.format("%02d:%02d", m, s)
    }

    private fun formatSpeed(speed: Float): String {
        val v = if (speed == speed.toInt().toFloat()) speed.toInt().toString() else speed.toString()
        return v + "x"
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        player.setSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        player.setSurfaceSize(width, height)
        surfaceReady = true
        maybeStart()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
    }

    override fun onPause() {
        super.onPause()
        if (prepared && !userPaused && player.getState() == CCPlayer.State.STARTED) {
            player.pause()
            pausedByLifecycle = true
            isPlaying = false
            syncPlayPauseButton()
        }
    }

    override fun onResume() {
        super.onResume()
        if (prepared && pausedByLifecycle) {
            player.resume()
            pausedByLifecycle = false
            isPlaying = true
            syncPlayPauseButton()
        }
    }

    override fun onDestroy() {
        player.release()
        super.onDestroy()
    }
}
