#pragma once

#define LOG_TAG "GstWebrtcDemo"

#ifndef ALOGV
#ifdef __ANDROID__
#include <android/log.h>

#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define ALOGD(...) printf(__VA_ARGS__);printf("\n")
#define ALOGI(...) printf(__VA_ARGS__);printf("\n")
#define ALOGW(...) printf(__VA_ARGS__);printf("\n")
#define ALOGE(...) printf(__VA_ARGS__);printf("\n")
#endif
#endif
