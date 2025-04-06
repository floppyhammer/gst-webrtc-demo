// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

package com.gst.webrtc_server

import org.freedesktop.gstreamer.GStreamer

import android.app.NativeActivity
import android.os.Bundle
import android.os.PersistableBundle
import android.util.Log

class StreamingActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?, persistentState: PersistableBundle?) {
        System.loadLibrary("gst_webrtc_demo")
        Log.i("GstWebrtcServer", "StreamingActivity: loaded gst_webrtc_demo")

        super.onCreate(savedInstanceState, persistentState)
    }

    companion object {
        init {
            Log.i("GstWebrtcServer", "StreamingActivity: In StreamingActivity static init")
        }
    }
}
