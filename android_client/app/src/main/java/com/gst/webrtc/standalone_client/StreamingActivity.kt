// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

package com.gst.webrtc.standalone_client

import android.app.NativeActivity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.PersistableBundle
import android.util.Log

class StreamingActivity : NativeActivity() {
//    private var mView: GLSurfaceView? = null

    override fun onCreate(savedInstanceState: Bundle?, persistentState: PersistableBundle?) {

        System.loadLibrary("gst_webrtc_client")
        Log.i("GstWebrtcClient", "StreamingActivity: loaded gst_webrtc_client")

        System.loadLibrary("gst_webrtc_standalone_client")
        Log.i("GstWebrtcClient", "StreamingActivity: loaded gst_webrtc_standalone_client")

//        mView = GLSurfaceView(application)
//
//        setContentView(mView)

        super.onCreate(savedInstanceState, persistentState)
    }

    companion object {
        init {
            Log.i("GstWebrtcClient", "StreamingActivity: In StreamingActivity static init")
        }
    }
}
