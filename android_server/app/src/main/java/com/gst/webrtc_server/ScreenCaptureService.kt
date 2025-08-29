package com.gst.webrtc_server

import android.app.Activity
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import java.util.concurrent.Executors

class ScreenCaptureService : Service() {
    private lateinit var mediaProjectionManager: MediaProjectionManager
    private var mediaProjection: MediaProjection? = null
    private var audioRecord: AudioRecord? = null
    private var isRecording = false
    private val recordingExecutor = Executors.newSingleThreadExecutor()

    // Declare your native method (ensure it's loaded if this service runs in a different process,
    // though for this setup it's likely the same process)
    // If your System.loadLibrary is in an Application class or ensure it's loaded before this service needs it.
    private external fun nativeProcessAudio(data: ByteArray, size: Int, timestamp: Long)

    companion object {
        const val ACTION_START = "com.gst.webrtc_server.ACTION_START"
        const val ACTION_STOP = "com.gst.webrtc_server.ACTION_STOP"
        const val EXTRA_RESULT_CODE = "com.gst.webrtc_server.EXTRA_RESULT_CODE"
        const val EXTRA_DATA_INTENT = "com.gst.webrtc_server.EXTRA_DATA_INTENT"

        private const val NOTIFICATION_ID = 123
        private const val NOTIFICATION_CHANNEL_ID = "ScreenCaptureChannel"

        private const val TAG = "ScreenCaptureService"
    }

    override fun onCreate() {
        super.onCreate()

        mediaProjectionManager =
            getSystemService(MEDIA_PROJECTION_SERVICE) as MediaProjectionManager

        createNotificationChannel()
        Log.d(TAG, "Service onCreate")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Log.d(TAG, "onStartCommand: ${intent?.action}")
        if (intent == null) {
            Log.w(
                TAG,
                "Intent is null in onStartCommand, stopping service to be safe."
            )
            stopSelf()
            return START_NOT_STICKY
        }

        when (intent.action) {
            ACTION_START -> {
                val resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, Activity.RESULT_CANCELED)
                val data: Intent? = intent.getParcelableExtra(EXTRA_DATA_INTENT)

                if (resultCode == Activity.RESULT_OK && data != null) {
                    startForegroundServiceWithNotification() // Start foreground before heavy work

                    // It's crucial to get mediaProjection on the main thread or a thread
                    // that has a Looper if the MediaProjectionManager implementation requires it.
                    // Usually, this is fine if onStartCommand is on the main thread.
                    try {
                        mediaProjection =
                            mediaProjectionManager.getMediaProjection(resultCode, data)

                        if (mediaProjection != null) {
                            Log.i(
                                TAG,
                                "MediaProjection obtained. Starting audio recording."
                            )
                            startAudioRecording()
                        } else {
                            Log.e(TAG, "Failed to get MediaProjection.")
                            stopSelf()
                            return START_NOT_STICKY
                        }
                    } catch (e: Exception) {
                        Log.e(
                            TAG,
                            "Error obtaining MediaProjection or starting recording",
                            e
                        )
                        stopSelf()
                        return START_NOT_STICKY
                    }
                } else {
                    Log.e(
                        TAG,
                        "Result code not OK or data is null. Cannot start capture."
                    )
                    stopSelf() // Stop if we can't initialize
                    return START_NOT_STICKY
                }
            }

            ACTION_STOP -> {
                Log.d(TAG, "Action stop received")
                stopCaptureAndService()
            }

            else -> {
                Log.w(TAG, "Unknown action: ${intent.action}")
                stopSelf() // Stop if action is unknown
            }
        }
        return START_STICKY // Or START_NOT_STICKY depending on desired behavior on kill
    }

    private fun startForegroundServiceWithNotification() {
        val notification = createNotification()
        try {
            // ServiceCompat.startForeground ensures compatibility and handles API differences.
            startForeground(
                NOTIFICATION_ID, // Cannot be 0
                notification,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
                } else {
                    0 // Type 0 for versions below Q, as it's not explicitly set this way
                }
            )
            Log.i(TAG, "Service started in foreground.")
        } catch (e: Exception) {
            Log.e(TAG, "Error starting foreground service", e)
            // This can happen if the foregroundServiceType in the manifest doesn't match,
            // or if the specific FGS permission is missing on Android 14+.
            stopSelf() // Stop the service if it cannot start in foreground
        }
    }


    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(
                NOTIFICATION_CHANNEL_ID,
                "Screen Capture Service Channel",
                NotificationManager.IMPORTANCE_DEFAULT // Or IMPORTANCE_LOW to minimize sound/vibration
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(serviceChannel)
        }
    }

    private fun createNotification(): Notification {
        val notificationIntent =
            Intent(this, StreamingActivity::class.java) // Or your main activity
        val pendingIntentFlags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        } else {
            PendingIntent.FLAG_UPDATE_CURRENT
        }
        val pendingIntent =
            PendingIntent.getActivity(this, 0, notificationIntent, pendingIntentFlags)

        // Add a Stop button to the notification
        val stopSelfIntent = Intent(this, ScreenCaptureService::class.java).apply {
            action = ACTION_STOP
        }
        val stopSelfPendingIntentFlags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_CANCEL_CURRENT
        } else {
            PendingIntent.FLAG_CANCEL_CURRENT
        }
        val stopSelfPendingIntent =
            PendingIntent.getService(this, 0, stopSelfIntent, stopSelfPendingIntentFlags)

        return NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle("Capturing Audio")
            .setContentText("Internal audio capture is active.")
            .setSmallIcon(android.R.drawable.ic_media_play) // Replace with your app's icon
            .setContentIntent(pendingIntent)
            .addAction(
                android.R.drawable.ic_media_play,
                "Stop",
                stopSelfPendingIntent
            ) // Example stop action
            .setOngoing(true) // Makes the notification non-dismissible by swipe
            .build()
    }

    private fun startAudioRecording() {
        if (mediaProjection == null) {
            Log.e(TAG, "MediaProjection is null. Cannot start audio recording.")
            stopSelf()
            return
        }
        Log.d(TAG, "Attempting to start audio recording.")

        val config = AudioPlaybackCaptureConfiguration.Builder(mediaProjection!!)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .build()

        val audioFormat = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(44100)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
            .build()

        val bufferSizeInBytes = AudioRecord.getMinBufferSize(
            audioFormat.sampleRate,
            audioFormat.channelMask,
            audioFormat.encoding
        )

        if (bufferSizeInBytes == AudioRecord.ERROR_BAD_VALUE || bufferSizeInBytes == AudioRecord.ERROR) {
            Log.e(TAG, "Invalid buffer size for AudioRecord.")
            stopSelf()
            return
        }

        try {
            audioRecord = AudioRecord.Builder()
                .setAudioFormat(audioFormat)
                .setBufferSizeInBytes(bufferSizeInBytes)
                .setAudioPlaybackCaptureConfig(config)
                .build()

            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                Log.e(TAG, "AudioRecord failed to initialize.")
                audioRecord?.release()
                audioRecord = null
                stopSelf()
                return
            }

            audioRecord?.startRecording()
            isRecording = true
            Log.i(TAG, "Audio recording started successfully.")

            recordingExecutor.execute {
                val audioBuffer = ByteArray(bufferSizeInBytes)
                while (isRecording && audioRecord != null) {
                    val readResult = audioRecord?.read(audioBuffer, 0, audioBuffer.size)
                    if (readResult != null && readResult > 0 && readResult != AudioRecord.ERROR_INVALID_OPERATION) {
                        // Ensure nativeProcessAudio is available and library loaded
                        try {
                            nativeProcessAudio(audioBuffer, readResult, System.nanoTime())
                        } catch (e: UnsatisfiedLinkError) {
                            Log.e(
                                TAG,
                                "Native method error: ${e.message}. Is library loaded?"
                            )
                            // Potentially stop recording if native part is critical
                            // isRecording = false
                        }
                    } else if (readResult != null && readResult < 0) {
                        Log.w(
                            TAG,
                            "AudioRecord read error: $readResult. Stopping recording."
                        )
                        isRecording = false // Stop loop on read error
                    } else if (audioRecord == null || !isRecording) {
                        Log.i(
                            TAG,
                            "AudioRecord became null or no longer recording, exiting loop."
                        )
                        break
                    }
                }
                Log.i(TAG, "Audio recording thread stopped.")
                // Clean up AudioRecord here if the loop exits and isRecording is false
                if (!isRecording) {
                    audioRecord?.stop()
                    audioRecord?.release()
                    audioRecord = null
                    Log.i(
                        TAG,
                        "AudioRecord stopped and released after loop exit."
                    )
                }
            }
        } catch (se: SecurityException) {
            Log.e(
                TAG,
                "SecurityException during AudioRecord setup or start: ${se.message}",
                se
            )
            stopSelf() // Stop service if audio recording can't start due to security
        } catch (e: Exception) {
            Log.e(
                TAG,
                "Exception during audio recording setup or start: ${e.message}",
                e
            )
            stopSelf() // Stop service on other errors
        }
    }

    private fun stopCaptureAndService() {
        Log.d(TAG, "Stopping capture and service.")
        isRecording = false // Signal recording thread to stop

        // Give the recording thread a moment to finish, then release resources
        recordingExecutor.execute { // Ensure this is executed, even if a shutdown is requested.
            if (audioRecord?.recordingState == AudioRecord.RECORDSTATE_RECORDING) {
                audioRecord?.stop()
            }
            audioRecord?.release()
            audioRecord = null
            Log.i(TAG, "AudioRecord released in stopCaptureAndService.")

            mediaProjection?.stop()
            mediaProjection = null
            Log.i(TAG, "MediaProjection stopped in stopCaptureAndService.")

            // Ensure this runs on the main thread if it modifies UI or interacts with other main thread components
            // For stopping service, it's fine from here.
            stopForeground(true) // true to remove notification
            stopSelf() // Stop the service itself
            Log.i(TAG, "Service stopped.")
        }
        // Consider shutting down the executor if the service is truly done
        // recordingExecutor.shutdown()
    }

    override fun onDestroy() {
        Log.d(TAG, "Service onDestroy. Cleaning up.")
        // Ensure cleanup, although stopCaptureAndService should handle most of it.
        if (isRecording || audioRecord != null || mediaProjection != null) {
            Log.w(
                TAG,
                "onDestroy called but resources might still be active. Forcing cleanup."
            )
            stopCaptureAndService() // Last chance cleanup
        }
        if (!recordingExecutor.isShutdown) {
            recordingExecutor.shutdownNow() // Force shutdown if not already
        }
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null // Not a bound service
    }
}