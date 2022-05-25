#ifndef __MEDIAPLAYER_VDEV_H__
#define __MEDIAPLAYER_VDEV_H__


#include <pthread.h>
#include "mediaplayer.h"
#include "ffrender.h"


#ifdef __cplusplus
extern "C" {
#endif


#define VDEV_CLOSE      (1 << 0)
#define VDEV_PAUSE      (1 << 1)
#define VDEV_COMPLETED  (1 << 2)
#define VDEV_CLEAR      (1 << 3)
#define VDEV_RENDER      (1 << 4)

//++ vdev context common members
#define VDEV_COMMON_MEMBERS        \
    int         bufnum;            \
    int         pixfmt;            \
    int         vw, vh, vm;        \
    RECT        rrect;             \
    RECT        vrect;             \
                                   \
    void       *surface;           \
    int64_t    *ppts;              \
                                   \
    int         head;              \
    int         tail;              \
    int         size;              \
                                   \
    pthread_mutex_t mutex;         \
    pthread_cond_t  cond;          \
                                   \
    CMNVARS    *cmnvars;           \
    int         tickavdiff;        \
    int         tickframe;         \
    int         ticksleep;         \
    int64_t     ticklast;          \
                                   \
    int         speed;             \
    int         status;            \
    pthread_t   thread;            \
                                   \
    int         completed_counter; \
    int64_t     completed_apts;    \
    int64_t     completed_vpts;    \
    void       *bbox_list;         \
    int (*lock    )(void *ctxt, uint8_t *buffer[8], int linesize[8], int64_t pts); \
    void (*unlock  )(void *ctxt);  \
    void (*setrect )(void *ctxt, int x, int y, int w, int h); \
    void (*setparam)(void *ctxt, int id, void *param);        \
    void (*getparam)(void *ctxt, int id, void *param);        \
    void (*destroy )(void *ctxt);
//-- vdev context common members

#define VDEV_WIN32__MEMBERS \
    HPEN        hbboxpen; \
    HDC         hoverlay; \
    HBITMAP     hoverbmp; \
    BYTE       *poverlay; \
    RECTOVERLAY overlay_rects[8];


typedef struct {
    VDEV_COMMON_MEMBERS
} VDEV_COMMON_CTXT;

typedef struct {
    // common members
    VDEV_COMMON_MEMBERS
    void* win;
} VDEVCTXT;


void* vdev_create  (int type, void *surface, int bufnum, int w, int h, int ftime, CMNVARS *cmnvars);
void  vdev_destroy (void *ctxt);
int  vdev_lock    (void *ctxt, uint8_t *buffer[8], int linesize[8], int64_t pts);
void  vdev_unlock  (void *ctxt);
void  vdev_setrect (void *ctxt, int x, int y, int w, int h);
void  vdev_pause   (void *ctxt, int pause);
void  vdev_reset   (void *ctxt);
void  vdev_setparam(void *ctxt, int id, void *param);
void  vdev_getparam(void *ctxt, int id, void *param);

void* vdev_unity_create(void *surface, int bufnum);

// internal helper function
void  vdev_avsync_and_complete(void *ctxt);

#ifdef __cplusplus
}
#endif

#endif



