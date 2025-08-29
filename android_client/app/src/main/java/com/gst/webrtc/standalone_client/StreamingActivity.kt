package com.gst.webrtc.standalone_client

import android.app.NativeActivity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.PersistableBundle
import android.util.Log
import android.view.WindowManager

class StreamingActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?, persistentState: PersistableBundle?) {
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        System.loadLibrary("gst_webrtc_client")
        Log.i("GstWebrtcClient", "StreamingActivity: loaded gst_webrtc_client")

        System.loadLibrary("gst_webrtc_standalone_client")
        Log.i("GstWebrtcClient", "StreamingActivity: loaded gst_webrtc_standalone_client")

        super.onCreate(savedInstanceState, persistentState)
    }

    companion object {
        init {
            Log.i("GstWebrtcClient", "StreamingActivity: In StreamingActivity static init")
        }
    }
}
