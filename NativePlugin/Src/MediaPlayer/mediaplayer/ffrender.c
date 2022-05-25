#include <pthread.h>
#include "mediaplayer.h"
#include "ffrender.h"

#include "adev.h"
#include "vdev.h"



#include "libavutil/time.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#include "logger.h"




// 内部类型定义
typedef struct
{
    uint8_t           *adev_buf_data ;
    uint8_t           *adev_buf_cur  ;
    int                adev_buf_size ;
    int                adev_buf_avail;

    void              *surface;
    AVRational         frmrate;

    // cmnvars & adev & vdev
    CMNVARS           *cmnvars;
    void              *adev;
    void              *vdev;

    // swresampler & swscaler
    struct SwrContext *swr_context;
    struct SwsContext *sws_context;

    // playback speed
    int                cur_speed_type;
    int                cur_speed_value;
    int                new_speed_type;
    int                new_speed_value;

    int                swr_src_format;
    int                swr_src_samprate;
    int                swr_src_chlayout;

    int                sws_src_pixfmt;
    int                sws_src_width;
    int                sws_src_height;
    int                sws_dst_pixfmt;
    int                sws_dst_width;
    int                sws_dst_height;

    int                cur_video_w;
    int                cur_video_h;
    RECT               cur_src_rect;
    RECT               new_src_rect;

    /* software volume */
    #define SW_VOLUME_MINDB  -30
    #define SW_VOLUME_MAXDB  +12
    int                vol_scaler[256];
    int                vol_zerodb;
    int                vol_curvol;

    // render status
    #define RENDER_CLOSE                  (1 << 0)
    #define RENDER_PAUSE                  (1 << 1)
    #define RENDER_SNAPSHOT               (1 << 2)  // take snapshot
    #define RENDER_STEPFORWARD            (1 << 3)  // step forward
    #define RENDER_DEFINITION_EVAL        (1 << 4)
    // #define JUMP_FROM_VIDEO_RENDER        (1 << 5)

    int                status;
    float              definitionval;


} RENDER;


static int swvol_scaler_init(int *scaler, int mindb, int maxdb)
{
    double tabdb[256];
    double tabf [256];
    int    z, i;

    for (i=0; i<256; i++) {
        tabdb[i]  = mindb + (maxdb - mindb) * i / 256.0;
        tabf [i]  = pow(10.0, tabdb[i] / 20.0);
        scaler[i] = (int)((1 << 14) * tabf[i]); // Q14 fix point
    }

    z = -mindb * 256 / (maxdb - mindb);
    z = MAX(z, 0  );
    z = MIN(z, 255);
    scaler[0] = 0;        // mute
    scaler[z] = (1 << 14);// 0db
    return z;
}

static void swvol_scaler_run(int16_t *buf, int n, int multiplier)
{
    if (multiplier > (1 << 14)) {
        int32_t v;
        while (n--) {
            v = ((int32_t)*buf * multiplier) >> 14;
            v = MAX(v,-0x7fff);
            v = MIN(v, 0x7fff);
            *buf++ = (int16_t)v;
        }
    } else if (multiplier < (1 << 14)) {
        while (n--) { *buf = ((int32_t)*buf * multiplier) >> 14; buf++; }
    }
}

static void render_setspeed(RENDER *render, int speed)
{
    if (speed <= 0) return;
    vdev_setparam(render->vdev, PARAM_PLAY_SPEED_VALUE, &speed); // set vdev playback speed
    render->new_speed_value = speed; // set speed_value_new to triger swr_context re-create
}


void* render_open(int adevtype, int vdevtype, void *player_wrapper, struct AVRational frate, int w, int h, CMNVARS *cmnvars)
{
    RENDER  *render = (RENDER*)calloc(1, sizeof(RENDER));
    if (!render) {
        av_log(NULL, AV_LOG_ERROR, "failed to allocate render context !\n");
        exit(0);
    }

    render->frmrate = frate;
    render->cmnvars = cmnvars;

    render->adev_buf_avail = render->adev_buf_size = (int)ADEV_SAMPLE_PER_CALL * 4;
    render->adev_buf_cur   = render->adev_buf_data = malloc(render->adev_buf_size);

    // init for cmnvars
    render->adev = adev_create(adevtype, 3, render->adev_buf_size, cmnvars);
    render->vdev = vdev_create(vdevtype, player_wrapper, 0, w, h, 1000 * frate.den / frate.num, cmnvars);




    // set default playback speed
    render_setspeed(render, 100);

    // init software volume scaler
    render->vol_zerodb = swvol_scaler_init(render->vol_scaler, SW_VOLUME_MINDB, SW_VOLUME_MAXDB);
    render->vol_curvol = render->vol_zerodb;

    // setup default swscale_type
    if (render->cmnvars->init_params->swscale_type == 0) render->cmnvars->init_params->swscale_type = SWS_FAST_BILINEAR;
    return render;
}

void render_close(void *hrender)
{
    RENDER *render = (RENDER*)hrender;

    // wait visual effect thread exit
    render->status = RENDER_CLOSE;

    //++ audio ++//
    // destroy adev
    adev_destroy(render->adev);

    // free swr context
    swr_free(&render->swr_context);
    //-- audio --//

    //++ video ++//
    // destroy vdev
    vdev_destroy(render->vdev);

    // free sws context
    if (render->sws_context) {
        sws_freeContext(render->sws_context);
    }

    // free context
    free(render->adev_buf_data);
    free(render);
}


static int render_audio_swresample(RENDER *render, AVFrame *audio)
{
    int num_samp;

    //++ do resample audio data ++//
    num_samp = swr_convert(render->swr_context,
        (uint8_t**)&render->adev_buf_cur, render->adev_buf_avail / 4,
        (const uint8_t**)audio->extended_data, audio->nb_samples);
    audio->extended_data    = NULL;
    audio->nb_samples       = 0;
    render->adev_buf_avail -= num_samp * 4;
    render->adev_buf_cur   += num_samp * 4;
    //-- do resample audio data --//

    if (render->adev_buf_avail == 0) {
        swvol_scaler_run((int16_t*)render->adev_buf_data, render->adev_buf_size / sizeof(int16_t), render->vol_scaler[render->vol_curvol]);
        audio->pts += 5 * render->cur_speed_value * render->adev_buf_size / (2 * ADEV_SAMPLE_RATE);
        adev_write(render->adev, render->adev_buf_data, render->adev_buf_size, audio->pts);
        render->adev_buf_avail = render->adev_buf_size;
        render->adev_buf_cur   = render->adev_buf_data;
    }
    return num_samp;
}

void render_audio(void *hrender, AVFrame *audio)
{
    RENDER *render  = (RENDER*)hrender;
    int     samprate, sampnum;
    if (!hrender) return;

    if (render->cmnvars->init_params->avts_syncmode != AVSYNC_MODE_FILE && render->cmnvars->apktn > render->cmnvars->init_params->audio_bufpktn) return;
    do {
        if (  render->swr_src_format != audio->format || render->swr_src_samprate != audio->sample_rate || render->swr_src_chlayout != audio->channel_layout
           || render->cur_speed_type != render->new_speed_type || render->cur_speed_value != render->new_speed_value) {

            render->swr_src_format   = (int)audio->format;
            render->swr_src_samprate = (int)audio->sample_rate;
            render->swr_src_chlayout = (int)audio->channel_layout;
            render->cur_speed_type   = render->new_speed_type ;
            render->cur_speed_value  = render->new_speed_value;
            samprate = render->cur_speed_type ? ADEV_SAMPLE_RATE : (int)(ADEV_SAMPLE_RATE * 100.0 / render->cur_speed_value);

            if (render->swr_context) 
                swr_free(&render->swr_context);

            render->swr_context = swr_alloc_set_opts(NULL, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, samprate,
                render->swr_src_chlayout, render->swr_src_format, render->swr_src_samprate, 0, NULL);

                
            swr_init(render->swr_context);
        }



        {
            sampnum = render_audio_swresample(render, audio);
        }
    } while (sampnum);
}

static float definition_evaluation(uint8_t *img, int w, int h, int stride)
{
    uint8_t *cur, *pre, *nxt;
    int     i, j, l;
    int64_t s = 0;

    if (!img || !w || !h || !stride) return 0;
    pre = img + 1;
    cur = img + 1 + stride * 1;
    nxt = img + 1 + stride * 2;

    for (i=1; i<h-1; i++) {
        for (j=1; j<w-1; j++) {
            l  = 1 * pre[-1] +  4 * pre[0] + 1 * pre[1];
            l += 4 * cur[-1] - 20 * cur[0] + 4 * cur[1];
            l += 1 * nxt[-1] +  4 * nxt[0] + 1 * nxt[1];
            s += abs(l);
            pre++; cur++; nxt++;
        }
        pre += stride - (w - 2);
        cur += stride - (w - 2);
        nxt += stride - (w - 2);
    }
    return (float)s / ((w - 2) * (h - 2));
}

static void render_setup_srcrect(RENDER *render, AVFrame *video, AVFrame *srcpic)
{
    srcpic->pts    = video->pts;
    srcpic->format = video->format;
    srcpic->width  = render->cur_src_rect.right  - render->cur_src_rect.left;
    srcpic->height = render->cur_src_rect.bottom - render->cur_src_rect.top;
    memcpy(srcpic->data    , video->data    , sizeof(srcpic->data    ));
    memcpy(srcpic->linesize, video->linesize, sizeof(srcpic->linesize));
    switch (video->format) {
    case AV_PIX_FMT_YUV420P:
        srcpic->data[0] += render->cur_src_rect.top * video->linesize[0] + render->cur_src_rect.left;
        srcpic->data[1] +=(render->cur_src_rect.top / 2) * video->linesize[1] + (render->cur_src_rect.left / 2);
        srcpic->data[2] +=(render->cur_src_rect.top / 2) * video->linesize[2] + (render->cur_src_rect.left / 2);
        break;
    case AV_PIX_FMT_NV21:
    case AV_PIX_FMT_NV12:
        srcpic->data[0] += render->cur_src_rect.top * video->linesize[0] + render->cur_src_rect.left;
        srcpic->data[1] += (render->cur_src_rect.top / 2) * video->linesize[1] + (render->cur_src_rect.left / 2) * 2;
        break;
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_BGRA:
        srcpic->data[0] += render->cur_src_rect.top * video->linesize[0] + render->cur_src_rect.left * sizeof(uint32_t);
        break;
    }
}

void render_video(void *hrender, AVFrame *video)
{

    RENDER *render = (RENDER*)hrender;
    if (!hrender) return;
    if (render->status & RENDER_DEFINITION_EVAL) {
        render->definitionval =  definition_evaluation(video->data[0], video->width, video->height, video->linesize[0]);
        render->status       &= ~RENDER_DEFINITION_EVAL;
    }

    if (render->cmnvars->init_params->avts_syncmode != AVSYNC_MODE_FILE && render->cmnvars->vpktn > render->cmnvars->init_params->video_bufpktn) return;

    int isRendered = 0;
    do {
        if(isRendered > 0){
            av_usleep(20*1000);
            // log_print("视频线程 === avframe已经使用过了");
            continue;
        }

        VDEV_COMMON_CTXT *vdev = (VDEV_COMMON_CTXT*)render->vdev;
        AVFrame lockedpic = *video, srcpic, dstpic = {{0}};
        if (render->cur_video_w != video->width || render->cur_video_h != video->height) {
            render->cur_video_w = render->new_src_rect.right  = video->width ;
            render->cur_video_h = render->new_src_rect.bottom = video->height;
        }
        if (memcmp(&render->cur_src_rect, &render->new_src_rect, sizeof(RECT)) != 0) {
            render->cur_src_rect.left  = MIN(render->new_src_rect.left  , video->width );
            render->cur_src_rect.top   = MIN(render->new_src_rect.top   , video->height);
            render->cur_src_rect.right = MIN(render->new_src_rect.right , video->width );
            render->cur_src_rect.bottom= MIN(render->new_src_rect.bottom, video->height);
            render->new_src_rect       = render->cur_src_rect;
            vdev->vw = MAX(render->cur_src_rect.right - render->cur_src_rect.left, 1); vdev->vh = MAX(render->cur_src_rect.bottom - render->cur_src_rect.top, 1);
            vdev_setparam(vdev, PARAM_VIDEO_MODE, &vdev->vm);
        }


        // log_print("渲染一帧");
        render_setup_srcrect(render, &lockedpic, &srcpic);
        if(vdev_lock(render->vdev, dstpic.data, dstpic.linesize, srcpic.pts) != 0) return;
        if (dstpic.data[0] && srcpic.format != -1 && srcpic.pts != -1) {
            
            //源的参数变了，重新生成SwsContext
            if (  render->sws_src_pixfmt != srcpic.format || render->sws_src_width != srcpic.width || render->sws_src_height != srcpic.height
               || render->sws_dst_pixfmt != vdev->pixfmt  || render->sws_dst_width != dstpic.linesize[6] || render->sws_dst_height != dstpic.linesize[7]) {
                // log_print("render_video == 重新生成SwsContext ");

                render->sws_src_pixfmt = srcpic.format;
                render->sws_src_width  = srcpic.width;
                render->sws_src_height = srcpic.height;
                render->sws_dst_pixfmt = vdev->pixfmt;
                render->sws_dst_width  = dstpic.linesize[6];
                render->sws_dst_height = dstpic.linesize[7];

                if (render->sws_context) 
                    sws_freeContext(render->sws_context);

                render->sws_context = sws_getContext(render->sws_src_width, render->sws_src_height, render->sws_src_pixfmt,
                    render->sws_dst_width, render->sws_dst_height, render->sws_dst_pixfmt, render->cmnvars->init_params->swscale_type, 0, 0, 0);
            }


            if (render->sws_context) {
                sws_scale(render->sws_context, (const uint8_t**)srcpic.data, srcpic.linesize, 0, render->sws_src_height, dstpic.data, dstpic.linesize);
            }

            isRendered = 1;

        }
        vdev_unlock(render->vdev);
        



    } while ((render->status & RENDER_PAUSE) && !(render->status & RENDER_STEPFORWARD) );
    // log_print("视频 编码 退出循环");
    // clear step forward flag
    render->status &= ~RENDER_STEPFORWARD;
}

void render_setrect(void *hrender, int type, int x, int y, int w, int h)
{
    RENDER *render = (RENDER*)hrender;
    if (!hrender) return;
    switch (type) {
    case 0: vdev_setrect(render->vdev, x, y, w, h); break;
    }
}

void render_pause(void *hrender, int pause)
{
    
    RENDER *render = (RENDER*)hrender;
    if (!hrender) return;
    if (pause) render->status |= RENDER_PAUSE;
    else       render->status &=~RENDER_PAUSE;
    adev_pause(render->adev, pause);
    vdev_pause(render->vdev, pause);
    render->cmnvars->start_tick= av_gettime_relative() / 1000;
    render->cmnvars->start_pts = MAX(render->cmnvars->apts, render->cmnvars->vpts);
}

void render_reset2(void *hrender){
    RENDER *render = (RENDER*)hrender;
    if (!hrender) return;
    vdev_reset(render->vdev);
}

void render_reset(void *hrender)
{
    RENDER *render = (RENDER*)hrender;
    if (!hrender) return;
    adev_reset(render->adev);
    vdev_reset(render->vdev);
}



void render_setparam(void *hrender, int id, void *param)
{
    RENDER *render = (RENDER*)hrender;
    if (!hrender) return;
    switch (id) {
    case PARAM_AUDIO_VOLUME:
        {
            int vol = *(int*)param;
            vol += render->vol_zerodb;
            vol  = MAX(vol, 0  );
            vol  = MIN(vol, 255);
            render->vol_curvol = vol;
        }
        break;
    case PARAM_PLAY_SPEED_VALUE: render_setspeed(render, *(int*)param);  break;
    case PARAM_PLAY_SPEED_TYPE : render->new_speed_type = *(int*)param;  break;
    case PARAM_VIDEO_MODE:
    case PARAM_AVSYNC_TIME_DIFF:
    case PARAM_VDEV_POST_SURFACE:
    case PARAM_VDEV_D3D_ROTATE:
    case PARAM_VDEV_SET_OVERLAY_RECT:
        vdev_setparam(render->vdev, id, param);
        break;
    case PARAM_RENDER_STEPFORWARD:
        render->status |= RENDER_STEPFORWARD;
        break;
    case PARAM_CLEAR_RENDER_STEPFORWARD:
        render->status &= ~RENDER_STEPFORWARD;
        break;
    case PARAM_RENDER_VDEV_WIN:

        break;
    case PARAM_RENDER_SOURCE_RECT:
        if (param) render->new_src_rect = *(RECT*)param;
        if (render->new_src_rect.right == 0 && render->new_src_rect.bottom == 0) {
            render->cur_video_w = render->cur_video_h = 0;
        }
        break;
    case PARAM_RENDER_STOP:
        render->status = RENDER_CLOSE;
        adev_setparam(render->adev, id, param);
        vdev_setparam(render->vdev, id, param);
        break;
    case PARAM_ADEV_RENDER_COMPLETED:
        adev_setparam(render->adev, id, param);
        break;
    }
}

void render_getparam(void *hrender, int id, void *param)
{
    RENDER           *render = (RENDER*)hrender;
    VDEV_COMMON_CTXT *vdev   = render ? (VDEV_COMMON_CTXT*)render->vdev : NULL;
    if (!hrender) return;
    switch (id)
    {
    case PARAM_MEDIA_POSITION:
        if (vdev && vdev->status & VDEV_COMPLETED) {
            *(int64_t*)param = -1; // means completed
        } else {
            *(int64_t*)param = MAX(render->cmnvars->apts, render->cmnvars->vpts);
        }
        break;
    case PARAM_AUDIO_VOLUME    : *(int*)param = render->vol_curvol - render->vol_zerodb; break;
    case PARAM_PLAY_SPEED_VALUE: *(int*)param = render->cur_speed_value; break;
    case PARAM_PLAY_SPEED_TYPE : *(int*)param = render->cur_speed_type;  break;
    case PARAM_VIDEO_MODE:
    case PARAM_AVSYNC_TIME_DIFF:
    case PARAM_VDEV_GET_D3DDEV:
    case PARAM_VDEV_D3D_ROTATE:
    case PARAM_VDEV_GET_OVERLAY_HDC:
    case PARAM_VDEV_GET_VRECT:
        vdev_getparam(vdev, id, param);
        break;
    case PARAM_ADEV_GET_CONTEXT: *(void**)param = render->adev; break;
    case PARAM_VDEV_GET_CONTEXT: *(void**)param = render->vdev; break;
    case PARAM_DEFINITION_VALUE:
        *(float*)param  = render->definitionval;
        render->status |= RENDER_DEFINITION_EVAL;
        break;
    case PARAM_RENDER_SOURCE_RECT:
        *(RECT*)param = render->cur_src_rect;
        break;
    }
}

