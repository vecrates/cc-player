package com.vecrates.ccplayer

import android.content.Context
import android.net.Uri
import android.view.Surface

class CCPlayer(private val context: Context) {

    companion object {
        init {
            System.loadLibrary("ccplayer_jni")
        }
    }

    private var nativeHandle: Long = 0

    enum class State {
        IDLE, INITIALIZED, PREPARED, STARTED, PAUSED, STOPPED, ERROR
    }

    interface Listener {
        fun onPrepared() {}
        fun onCompletion() {}
        fun onError(code: Int) {}
        fun onProgress(currentMs: Long, durationMs: Long) {}
        fun onBufferingUpdate(percent: Int) {}
        fun onSeekComplete() {}
    }

    var listener: Listener? = null

    init {
        nativeHandle = nativeCreate()
    }

    fun setDataSource(path: String) {
        nativeSetDataSource(nativeHandle, path)
    }

    fun setSurface(surface: Surface) {
        nativeSetSurface(nativeHandle, surface)
    }

    fun setSurfaceSize(width: Int, height: Int) {
        nativeSetSurfaceSize(nativeHandle, width, height)
    }

    fun prepare() {
        nativePrepare(nativeHandle)
    }

    fun prepareAsync() {
        nativePrepareAsync(nativeHandle)
    }

    fun start() {
        nativeStart(nativeHandle)
    }

    fun pause() {
        nativePause(nativeHandle)
    }

    fun resume() {
        nativeResume(nativeHandle)
    }

    fun stop() {
        nativeStop(nativeHandle)
    }

    fun seekTo(positionMs: Long) {
        nativeSeekTo(nativeHandle, positionMs)
    }

    /**
     * 设置播放倍速（变速不变调），范围 [0.5, 2.0]，越界自动 clamp。
     * 任意状态可调用，prepare 前预置生效，播放中切换即时生效。
     */
    fun setSpeed(speed: Float) {
        nativeSetSpeed(nativeHandle, speed)
    }

    /** 当前播放倍速（默认 1.0） */
    fun getSpeed(): Float = nativeGetSpeed(nativeHandle)

    fun release() {
        if (nativeHandle != 0L) {
            nativeRelease(nativeHandle)
            nativeHandle = 0
        }
    }

    fun getCurrentPosition(): Long = nativeGetCurrentPosition(nativeHandle)
    fun getDuration(): Long = nativeGetDuration(nativeHandle)
    fun getState(): State = State.entries[nativeGetState(nativeHandle)]

    /** 视频编码宽高（未校正旋转），无视频流返回 (0, 0)，需在 prepare 完成后调用 */
    fun getVideoSize(): Pair<Int, Int> {
        val arr = nativeGetVideoSize(nativeHandle)
        return arr[0] to arr[1]
    }

    /** 视频显示宽高（已应用旋转 metadata），无视频流返回 (0, 0)，需在 prepare 完成后调用 */
    fun getDisplayVideoSize(): Pair<Int, Int> {
        val arr = nativeGetDisplayVideoSize(nativeHandle)
        return arr[0] to arr[1]
    }

    protected fun finalize() {
        release()
    }

    // JNI callbacks from native side
    @Suppress("unused")
    private fun nativeOnPrepared() { listener?.onPrepared() }
    @Suppress("unused")
    private fun nativeOnCompletion() { listener?.onCompletion() }
    @Suppress("unused")
    private fun nativeOnError(code: Int) { listener?.onError(code) }
    @Suppress("unused")
    private fun nativeOnProgress(currentMs: Long, durationMs: Long) { listener?.onProgress(currentMs, durationMs) }
    @Suppress("unused")
    private fun nativeOnSeekComplete() { listener?.onSeekComplete() }

    // 供 native 在 prepare 阶段回调：打开 content:// 并返回 [fd, offset, length]，失败返回 [-1]
    @Suppress("unused")
    private fun nativeOpenContentFd(uri: String): LongArray {
        return try {
            val afd = context.contentResolver.openAssetFileDescriptor(Uri.parse(uri), "r")
            if (afd == null) {
                longArrayOf(-1)
            } else {
                val offset = afd.startOffset
                val length = afd.length
                val fd = afd.parcelFileDescriptor.detachFd()
                afd.close()
                longArrayOf(fd.toLong(), offset, length)
            }
        } catch (e: Exception) {
            longArrayOf(-1)
        }
    }

    // Native methods
    private external fun nativeCreate(): Long
    private external fun nativeSetDataSource(handle: Long, path: String)
    private external fun nativeSetSurface(handle: Long, surface: Surface)
    private external fun nativeSetSurfaceSize(handle: Long, width: Int, height: Int)
    private external fun nativePrepare(handle: Long)
    private external fun nativePrepareAsync(handle: Long)
    private external fun nativeStart(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeResume(handle: Long)
    private external fun nativeStop(handle: Long)
    private external fun nativeSeekTo(handle: Long, positionMs: Long)
    private external fun nativeSetSpeed(handle: Long, speed: Float)
    private external fun nativeGetSpeed(handle: Long): Float
    private external fun nativeRelease(handle: Long)
    private external fun nativeGetCurrentPosition(handle: Long): Long
    private external fun nativeGetDuration(handle: Long): Long
    private external fun nativeGetState(handle: Long): Int
    private external fun nativeGetVideoSize(handle: Long): IntArray
    private external fun nativeGetDisplayVideoSize(handle: Long): IntArray
}
