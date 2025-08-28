package com.gst.webrtc_server

import android.Manifest
import org.freedesktop.gstreamer.GStreamer

import android.app.NativeActivity
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Bundle
import android.os.PersistableBundle
import android.util.Log
import androidx.annotation.RequiresPermission

class StreamingActivity : NativeActivity() {
    // In your Activity or Fragment
    private lateinit var mediaProjectionManager: MediaProjectionManager
    private var mediaProjection: MediaProjection? = null
    private var audioRecord: AudioRecord? = null

    override fun onCreate(savedInstanceState: Bundle?, persistentState: PersistableBundle?) {
        System.loadLibrary("gst_webrtc_server_android")
        Log.i("GstWebrtcServer", "StreamingActivity: loaded so")

        super.onCreate(savedInstanceState, persistentState)

        mediaProjectionManager =
            getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
    }

    private fun startInternalAudioCapture() {
        val captureIntent = mediaProjectionManager.createScreenCaptureIntent()
        startActivityForResult(captureIntent, REQUEST_CODE_MEDIA_PROJECTION)
    }

    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_CODE_MEDIA_PROJECTION && resultCode == RESULT_OK) {
            mediaProjection = mediaProjectionManager.getMediaProjection(resultCode, data!!)
            startAudioRecording()
        }
    }

    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    private fun startAudioRecording() {
        val config = AudioPlaybackCaptureConfiguration.Builder(mediaProjection!!)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA) // Capture media playback
            .build()

        val audioFormat = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(44100)
            .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
            .build()

        val bufferSize = AudioRecord.getMinBufferSize(
            audioFormat.sampleRate,
            audioFormat.channelMask,
            audioFormat.encoding
        )

        audioRecord = AudioRecord.Builder()
            .setAudioFormat(audioFormat)
            .setBufferSizeInBytes(bufferSize)
            .setAudioPlaybackCaptureConfig(config)
            .build()

        audioRecord?.startRecording()

        // Start a separate thread to read audio data from audioRecord
        // and process/save it.
    }

    private fun stopAudioRecording() {
        audioRecord?.stop()
        audioRecord?.release()
        audioRecord = null
        mediaProjection?.stop()
        mediaProjection = null
    }

    companion object {
        private const val REQUEST_CODE_MEDIA_PROJECTION = 1001 // Or any other unique integer

        init {
            Log.i("GstWebrtcServer", "StreamingActivity: In StreamingActivity static init")
        }
    }
}
