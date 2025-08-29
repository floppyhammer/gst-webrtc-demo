#pragma once

#ifdef __ANDROID__
    #include <EGL/egl.h>
    #include <GLES3/gl3.h>
    #include <GLES3/gl3ext.h>
    #include <jni.h>

struct my_sample {
    GLuint frame_texture_id;
    GLenum frame_texture_target;
};

#endif
