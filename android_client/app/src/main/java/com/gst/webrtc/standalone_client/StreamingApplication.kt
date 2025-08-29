package com.gst.webrtc.standalone_client

import android.app.Application;
import android.util.Log
import org.freedesktop.gstreamer.GStreamer

class StreamingApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        Log.i("GstWebrtcClient", "StreamingApplication: In onCreate")

        System.loadLibrary("gstreamer_android")
        Log.i("GstWebrtcClient", "StreamingApplication: loaded gstreamer_android")

        Log.i("GstWebrtcClient", "StreamingApplication: Calling GStreamer.init")
        GStreamer.init(this)

        Log.i("GstWebrtcClient", "StreamingApplication: Done with GStreamer.init")
    }
}
