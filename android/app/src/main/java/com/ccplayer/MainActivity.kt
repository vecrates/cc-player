package com.ccplayer

import android.os.Bundle
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.appcompat.app.AppCompatActivity
import java.io.File
import java.io.FileOutputStream

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "MainActivity"
        private const val ASSET_VIDEO = "sample.mp4"
    }

    private lateinit var player: CCPlayer
    private lateinit var surfaceView: SurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        setContentView(surfaceView)

        player = CCPlayer(this)
        player.listener = object : CCPlayer.Listener {
            override fun onPrepared() {
                Log.e(TAG, "onPrepared:")
                player.start()
            }

            override fun onError(code: Int) {
                Log.d(TAG, "Error: $code")
            }

            override fun onProgress(currentMs: Long, durationMs: Long) {
            }
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        player.setSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.d(TAG, "Surface changed: $width x $height")
        player.setSurfaceSize(width, height)
        val path = intent.getStringExtra("video_path") ?: copyAssetToCache(ASSET_VIDEO)
        if (path != null) {
            player.setDataSource(path)
            player.prepareAsync()
        } else {
            Log.e(TAG, "Failed to copy asset to cache")
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {}

    override fun onPause() {
        super.onPause()
        player.pause()
    }

    override fun onResume() {
        super.onResume()
        if (player.getState() == CCPlayer.State.PAUSED) {
            player.resume()
        }
    }

    override fun onDestroy() {
        player.release()
        super.onDestroy()
    }

    private fun copyAssetToCache(assetName: String): String? {
        val cacheFile = File(cacheDir, assetName)
        if (cacheFile.exists() && cacheFile.length() > 0) {
            Log.d(TAG, "Using cached $assetName (${cacheFile.length()} bytes)")
            return cacheFile.absolutePath
        }
        return try {
            Log.d(TAG, "Copying $assetName from assets to cache...")
            assets.open(assetName).use { input ->
                FileOutputStream(cacheFile).use { output ->
                    input.copyTo(output, 8192)
                }
            }
            Log.d(TAG, "Copied ${cacheFile.length()} bytes")
            cacheFile.absolutePath
        } catch (e: Exception) {
            Log.e(TAG, "Failed to copy $assetName", e)
            null
        }
    }
}
