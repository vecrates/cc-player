package com.ccplayer

import android.content.Context
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

    fun release() {
        if (nativeHandle != 0L) {
            nativeRelease(nativeHandle)
            nativeHandle = 0
        }
    }

    fun getCurrentPosition(): Long = nativeGetCurrentPosition(nativeHandle)
    fun getDuration(): Long = nativeGetDuration(nativeHandle)
    fun getState(): State = State.values()[nativeGetState(nativeHandle)]

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
    private external fun nativeRelease(handle: Long)
    private external fun nativeGetCurrentPosition(handle: Long): Long
    private external fun nativeGetDuration(handle: Long): Long
    private external fun nativeGetState(handle: Long): Int
}
