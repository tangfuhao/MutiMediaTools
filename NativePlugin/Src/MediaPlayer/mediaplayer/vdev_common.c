#include "vdev.h"
#include "libavutil/log.h"
#include "libavutil/time.h"

#define COMPLETED_COUNTER  10

static void vdev_setup_vrect(VDEV_COMMON_CTXT *vdev)
{
    int rw = vdev->rrect.right - vdev->rrect.left, rh = vdev->rrect.bottom - vdev->rrect.top, vw, vh;
    if (vdev->vm == VIDEO_MODE_LETTERBOX) {
        if (rw * vdev->vh < rh * vdev->vw) {
            vw = rw; vh = vw * vdev->vh / vdev->vw;
        } else {
            vh = rh; vw = vh * vdev->vw / vdev->vh;
        }
    } else { vw = rw; vh = rh; }
    vdev->vrect.left  = (rw - vw) / 2;
    vdev->vrect.top   = (rh - vh) / 2;
    vdev->vrect.right = vdev->vrect.left + vw;
    vdev->vrect.bottom= vdev->vrect.top  + vh;
    vdev->status |= VDEV_CLEAR;
}


void* vdev_create(int type, void *player_wrapper, int bufnum, int w, int h, int ftime, CMNVARS *cmnvars)
{
    VDEV_COMMON_CTXT *c = NULL;
#ifdef __linux__
    c = (VDEV_COMMON_CTXT*)vdev_unity_create(player_wrapper, bufnum);
    if (!c) return NULL;
    c->tickavdiff=-ftime * 2; // 2 should equals to (DEF_ADEV_BUF_NUM - 1)
#endif
    c->vw          = MAX(w, 1);
    c->vh          = MAX(h, 1);
    c->rrect.right = MAX(w, 1);
    c->rrect.bottom= MAX(h, 1);
    c->vrect.right = MAX(w, 1);
    c->vrect.bottom= MAX(h, 1);
    c->tickframe   = ftime;
    c->ticksleep   = ftime;
    c->cmnvars     = cmnvars;
    c->status |= VDEV_RENDER;
    return c;
}

void vdev_destroy(void *ctxt)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;

    //++ rendering thread safely exit
    if (c->thread) {
        pthread_mutex_lock(&c->mutex);
        c->status = VDEV_CLOSE;
        pthread_cond_signal(&c->cond);
        pthread_mutex_unlock(&c->mutex);
        pthread_join(c->thread, NULL);
    }
    //-- rendering thread safely exit

    if (c->destroy) c->destroy(c);
}

int vdev_lock(void *ctxt, uint8_t *buffer[8], int linesize[8], int64_t pts)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    if (c->lock) 
        return c->lock(c, buffer, linesize, pts);
    return -1;
}

void vdev_unlock(void *ctxt)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    if (c->unlock) c->unlock(c);
}

void vdev_setrect(void *ctxt, int x, int y, int w, int h)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    w = w > 1 ? w : 1;
    h = h > 1 ? h : 1;
    pthread_mutex_lock(&c->mutex);
    c->rrect.left  = x;     c->rrect.top    = y;
    c->rrect.right = x + w; c->rrect.bottom = y + h;
    vdev_setup_vrect(c);
    pthread_mutex_unlock(&c->mutex);
    if (c->setrect) c->setrect(c, x, y, w, h);
}

void vdev_pause(void *ctxt, int pause)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    if (!ctxt) return;
    if (pause) c->status |=  VDEV_PAUSE;
    else       c->status &= ~VDEV_PAUSE;
}

void vdev_reset(void *ctxt)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    c->status |= VDEV_RENDER;
    if (!c) return;
}

void vdev_setparam(void *ctxt, int id, void *param)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    if (!ctxt) return;
    switch (id) {
    case PARAM_VIDEO_MODE:
        pthread_mutex_lock(&c->mutex);
        c->vm = *(int*)param;
        vdev_setup_vrect(c);
        pthread_mutex_unlock(&c->mutex);
        break;
    case PARAM_PLAY_SPEED_VALUE:
        if (param) c->speed = *(int*)param;
        break;
    case PARAM_AVSYNC_TIME_DIFF:
        if (param) c->tickavdiff = *(int*)param;
        break;
    case PARAM_VDEV_SET_BBOX:
        c->bbox_list = param;
        break;
    }
    if (c->setparam) c->setparam(c, id, param);
}

void vdev_getparam(void *ctxt, int id, void *param)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    if (!ctxt || !param) return;
    switch (id) {
    case PARAM_VIDEO_MODE          : *(int *)param = c->vm;         break;
    case PARAM_PLAY_SPEED_VALUE    : *(int *)param = c->speed;      break;
    case PARAM_AVSYNC_TIME_DIFF    : *(int *)param = c->tickavdiff; break;
#ifdef WIN32
    case PARAM_VDEV_GET_OVERLAY_HDC: *(HDC *)param = c->hoverlay;   break;
#endif
    case PARAM_VDEV_GET_VRECT      : *(RECT*)param = c->vrect;      break;
    }
    if (c->getparam) c->getparam(c, id, param);
}

void vdev_avsync_and_complete(void *ctxt)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    int     tickframe, tickdiff, scdiff, avdiff = -1;
    int64_t tickcur, sysclock;

    if (!(c->status & VDEV_PAUSE)) {
        //++ play completed ++//
        if (c->completed_apts != c->cmnvars->apts || c->completed_vpts != c->cmnvars->vpts) {
            c->completed_apts = c->cmnvars->apts;
            c->completed_vpts = c->cmnvars->vpts;
            c->completed_counter = 0;
            c->status &=~VDEV_COMPLETED;
        } else if (!c->cmnvars->apktn && !c->cmnvars->apktn && ++c->completed_counter == COMPLETED_COUNTER) {
            c->status |= VDEV_COMPLETED;
            player_send_message(c->cmnvars->winmsg, MSG_PLAY_COMPLETED, 0);
        }
        //-- play completed --//

        //++ frame rate & av sync control ++//
        tickframe   = 100 * c->tickframe / c->speed;
        tickcur     = av_gettime_relative() / 1000;
        tickdiff    = (int)(tickcur - c->ticklast);
        c->ticklast = tickcur;

        sysclock= c->cmnvars->start_pts + (tickcur - c->cmnvars->start_tick) * c->speed / 100;
        scdiff  = (int)(sysclock - c->cmnvars->vpts - c->tickavdiff); // diff between system clock and video pts
        avdiff  = (int)(c->cmnvars->apts  - c->cmnvars->vpts - c->tickavdiff); // diff between audio and video pts
        avdiff  = c->cmnvars->apts <= 0 ? scdiff : avdiff; // if apts is invalid, sync video to system clock

        if (tickdiff - tickframe >  5) c->ticksleep--;
        if (tickdiff - tickframe < -5) c->ticksleep++;
        if (c->cmnvars->vpts >= 0) {
            if      (avdiff >  500) c->ticksleep -= 3;
            else if (avdiff >  50 ) c->ticksleep -= 2;
            else if (avdiff >  30 ) c->ticksleep -= 1;
            else if (avdiff < -500) c->ticksleep += 3;
            else if (avdiff < -50 ) c->ticksleep += 2;
            else if (avdiff < -30 ) c->ticksleep += 1;
        }
        if (c->ticksleep < 0) c->ticksleep = 0;
        //-- frame rate & av sync control --//
    } else {
        c->ticksleep = c->tickframe;
    }

    if (c->ticksleep > 0 && c->cmnvars->init_params->avts_syncmode != AVSYNC_MODE_LIVE_SYNC0) av_usleep(c->ticksleep * 1000);
    av_log(NULL, AV_LOG_INFO, "d: %3d, s: %3d\n", avdiff, c->ticksleep);
}

