package com.gst.webrtc_server

import android.Manifest

import android.app.NativeActivity
import android.content.Intent
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.annotation.RequiresPermission
import androidx.core.content.ContextCompat

class StreamingActivity : NativeActivity() {
    private lateinit var mediaProjectionManager: MediaProjectionManager

    companion object {
        private const val REQUEST_CODE_MEDIA_PROJECTION = 1001 // Or any other unique integer
        private const val TAG = "StreamingActivity"
    }

    private var isServiceRunning = false

    override fun onCreate(savedInstanceState: Bundle?) {
        System.loadLibrary("gst_webrtc_server_android")
        Log.i(TAG, "StreamingActivity: loaded so")

        super.onCreate(savedInstanceState)

        mediaProjectionManager =
            getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager

        startInternalAudioCapture()
    }

    private fun startInternalAudioCapture() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            == PackageManager.PERMISSION_GRANTED
        ) {
            // Bring up the ScreenCapture prompt
            val captureIntent = mediaProjectionManager.createScreenCaptureIntent()
            startActivityForResult(captureIntent, REQUEST_CODE_MEDIA_PROJECTION)
        } else {
            Toast.makeText(this, "Required RECORD_AUDIO permission not granted", Toast.LENGTH_SHORT)
                .show()
        }
    }

    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_CODE_MEDIA_PROJECTION && resultCode == RESULT_OK) {
            startScreenCaptureService(resultCode, data)
        }
    }

    // In StreamingActivity
    private fun startScreenCaptureService(resultCode: Int, data: Intent) {
        Log.d(TAG, "Attempting to start ScreenCaptureService.")

        // 1. Create an Intent to start ScreenCaptureService
        val serviceIntent = Intent(this, ScreenCaptureService::class.java).apply {
            // 2. Set the action to tell the service what to do (e.g., start capture)
            action = ScreenCaptureService.ACTION_START

            // 3. Pass the MediaProjection result code and data Intent as extras
            //    The service will use these to obtain the MediaProjection object.
            putExtra(ScreenCaptureService.EXTRA_RESULT_CODE, resultCode)
            putExtra(ScreenCaptureService.EXTRA_DATA_INTENT, data)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(serviceIntent)
            Log.i(TAG, "Called startForegroundService for ScreenCaptureService.")
        } else {
            startService(serviceIntent)
            Log.i(TAG, "Called startService for ScreenCaptureService.")
        }
        isServiceRunning = true

        Log.i(
            TAG,
            "Call to start(Foreground)Service for ScreenCaptureService has been made with action: ${serviceIntent.action}"
        )
    }

    private fun stopScreenCaptureService() {
        val serviceIntent = Intent(this, ScreenCaptureService::class.java).apply {
            // THIS IS WHERE THE ACTION IS SET FOR STOPPING
            action = ScreenCaptureService.ACTION_STOP
        }
        startService(serviceIntent) // Send stop command
        isServiceRunning = false

        Log.i(
            TAG,
            "Stop command sent to ScreenCaptureService with action: ${serviceIntent.action}"
        )
    }
}
