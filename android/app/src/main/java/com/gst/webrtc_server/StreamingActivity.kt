package com.gst.webrtc_server

import org.freedesktop.gstreamer.GStreamer

import android.app.NativeActivity
import android.os.Bundle
import android.os.PersistableBundle
import android.util.Log

class StreamingActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?, persistentState: PersistableBundle?) {
        System.loadLibrary("gst_webrtc_server_android")
        Log.i("GstWebrtcServer", "StreamingActivity: loaded so")

        super.onCreate(savedInstanceState, persistentState)
    }

    companion object {
        init {
            Log.i("GstWebrtcServer", "StreamingActivity: In StreamingActivity static init")
        }
    }
}
