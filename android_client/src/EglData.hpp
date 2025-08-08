/*!
 * @file
 * @brief Encapsulate EGL setup/teardown.
 * @author Rylie Pavlik <rpavlik@collabora.com>
 */

#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <memory>

struct EglData {
    /// Creates an R8G8B8A8 ES3 context
    explicit EglData(ANativeWindow *window);

    /// Calls reset
    ~EglData();

    // do not move
    EglData(const EglData &) = delete;

    // do not move
    EglData(EglData &&) = delete;

    // do not copy
    EglData &operator=(const EglData &) = delete;

    // do not copy
    EglData &operator=(EglData &&) = delete;

    bool isReady() const;

    void makeCurrent() const;

    void makeNotCurrent() const;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig config = nullptr;
};
