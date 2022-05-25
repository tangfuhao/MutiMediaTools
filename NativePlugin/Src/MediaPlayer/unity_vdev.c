//unity虚拟显示设备
#include "libavformat/avformat.h"
#include "../mediaplayer/vdev.h"
#include "unity_plugin_mediaplayer.h"





#define DEF_WIN_PIX_FMT  TEXTURE_FORMAT_RGBX_8888
// #define VDEV_ANDROID_UPDATE_WIN  (1 << 31)




int unity_pixfmt_to_ffmpeg_pixfmt(int fmt)
{
    switch (fmt) {
    case TEXTURE_FORMAT_RGB_565:   return AV_PIX_FMT_RGB565;
    case TEXTURE_FORMAT_RGBX_8888: return AV_PIX_FMT_RGB32 ;
    default:                      return 0;
    }
}

static int vdev_unity_lock(void *ctxt, uint8_t *buffer[8], int linesize[8], int64_t pts)
{
    VDEVCTXT *c = (VDEVCTXT*)ctxt;
    // if (c->status & VDEV_ANDROID_UPDATE_WIN) {
    //     if (c->win    ) { ANativeWindow_release(c->win); c->win = NULL; }
    //     if (c->surface) c->win = ANativeWindow_fromSurface(get_jni_env(), (jobject)c->surface);
    //     if (c->win    ) ANativeWindow_setBuffersGeometry(c->win, c->vw, c->vh, DEF_WIN_PIX_FMT);
    //     c->status &= ~VDEV_ANDROID_UPDATE_WIN;
    // }
    if (c->win) {
        UnityTexture_Buffer winbuf;
        if (0 == UnityTextureLock(c->win,c->vw,c->vh, &winbuf)) {
            buffer  [0] = (uint8_t*)winbuf.bits;
            linesize[0] = winbuf.stride * 4;
            linesize[6] = c->vw;
            linesize[7] = c->vh;
        }else{
            return -1;
        }
    }
    c->cmnvars->vpts = pts;
    return 0;
}

static void vdev_unity_unlock(void *ctxt)
{
    VDEVCTXT *c = (VDEVCTXT*)ctxt;
    if (c->win) {
        UnityTextureUnlockAndPost(c->win);
        if(c->status & VDEV_RENDER){
            c->status &= ~VDEV_RENDER;
            //通知 渲染数据完成
            player_send_message(c->win,MSG_IMAGE_RENDER,NULL);
        }
    }
    vdev_avsync_and_complete(c);
}

static void vdev_unity_setparam(void *ctxt, int id, void *param)
{
    VDEVCTXT *c = (VDEVCTXT*)ctxt;
    switch (id) {
    case PARAM_RENDER_VDEV_WIN:
        c->surface = param;
        // c->status |= VDEV_ANDROID_UPDATE_WIN;
        break;
    case PARAM_RENDER_STOP:
        if (c->win) UnityPlayerWrapperRelease(c->win);
        break;
    }
}

static void vdev_unity_destroy(void *ctxt)
{
    VDEVCTXT *c = (VDEVCTXT*)ctxt;
    if (c->win) UnityPlayerWrapperRelease(c->win);
    free(ctxt);
}

void* vdev_unity_create(void *player_wrapper, int bufnum)
{
    VDEVCTXT *ctxt = (VDEVCTXT*)calloc(1, sizeof(VDEVCTXT));
    if (!ctxt) return NULL;
    // init vdev context
    ctxt->pixfmt  = unity_pixfmt_to_ffmpeg_pixfmt(DEF_WIN_PIX_FMT);
    ctxt->lock    = vdev_unity_lock;
    ctxt->unlock  = vdev_unity_unlock;
    ctxt->setparam= vdev_unity_setparam;
    ctxt->destroy = vdev_unity_destroy;
    ctxt->win = player_wrapper;
    // ctxt->status |= VDEV_ANDROID_UPDATE_WIN;
    return ctxt;
}