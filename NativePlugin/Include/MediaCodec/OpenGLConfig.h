#pragma once
#include <GL/glx.h>
//OpenGL的配置
typedef struct {
    GLXContext majorGLXContext;
    GLXFBConfig fbconfig;
    Display* dpy;
} OpenGLConfig;