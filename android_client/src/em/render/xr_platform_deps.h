// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief  Includes required by openxr_platform.h
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @ingroup em_client
 */

#ifdef __ANDROID__
    #include <EGL/egl.h>
    #include <jni.h>
#endif

#include <GLES3/gl3.h>
