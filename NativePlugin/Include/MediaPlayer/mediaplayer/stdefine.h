#ifndef __STDEFINE_H__
#define __STDEFINE_H__


#ifdef __linux__
#include <inttypes.h>


enum TEXTURE_FORMAT{
    TEXTURE_FORMAT_RGBX_8888,
    TEXTURE_FORMAT_RGB_565
};


typedef struct {
    long left;
    long top;
    long right;
    long bottom;
} RECT;

#endif




#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define DO_USE_VAR(a) do { a = a; } while (0)

#endif


