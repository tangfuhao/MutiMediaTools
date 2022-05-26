//unity虚拟音频设备
#include "mediaplayer/adev.h"
#include <unistd.h>
#include <pthread.h>
#include <logger.h>



#define DEF_ADEV_BUF_NUM  3
#define DEF_ADEV_BUF_LEN  2048








void* adev_create(int type, int bufnum, int buflen, CMNVARS *cmnvars)
{
    ADEV_CONTEXT *ctxt = NULL;
    int           i;


    DO_USE_VAR(type);
    bufnum = bufnum ? bufnum : DEF_ADEV_BUF_NUM;
    buflen = buflen ? buflen : DEF_ADEV_BUF_LEN;

    // allocate adev context
// ctxt = (ADEV_CONTEXT*)calloc(1, sizeof(ADEV_CONTEXT) + bufnum * sizeof(int64_t) + bufnum * sizeof(AUDIOBUF));
    
    size_t allocSize = sizeof(ADEV_CONTEXT) + bufnum * sizeof(int64_t) + bufnum * (sizeof(AUDIOBUF) + buflen);
    ctxt = (ADEV_CONTEXT*)calloc(1, allocSize);
    // log_print("申请内存 地址:%d, 长度:%d",ctxt,allocSize);
    if (!ctxt) return NULL;
    ctxt->bufnum   = bufnum;
    ctxt->buflen   = buflen;
    ctxt->ppts     = (int64_t *)((uint8_t*)ctxt + sizeof(ADEV_CONTEXT));
    ctxt->pWaveHdr = (AUDIOBUF*)((uint8_t*)ctxt->ppts + bufnum * sizeof(int64_t));
    ctxt->cmnvars  = cmnvars;



    // // new buffer
    // jbyteArray local_audio_buffer = env->NewByteArray(bufnum * buflen);
    // ctxt->audio_buffer = (jbyteArray)env->NewGlobalRef(local_audio_buffer);
    // ctxt->pWaveBuf     = (uint8_t  *)env->GetByteArrayElements(ctxt->audio_buffer, 0);
    // env->DeleteLocalRef(local_audio_buffer);

    // init wavebuf
    // for (i=0; i<bufnum; i++) {
    //     ctxt->pWaveHdr[i].data = (int16_t*)(ctxt->pWaveBuf + i * buflen);
    //     ctxt->pWaveHdr[i].size = buflen;
    // }

    // init wavebuf
    void *pwavbuf = (void*)(ctxt->pWaveHdr + bufnum);
    // log_print("pwavbuf内存 地址:%d",pwavbuf);
    for (i=0; i<bufnum; i++) {
        void* dataPtr = pwavbuf + i * buflen;
        ctxt->pWaveHdr[i].data         = (int16_t*)dataPtr;
        ctxt->pWaveHdr[i].size = buflen;
        // void* xxx = (void*)ctxt->pWaveHdr[i].data;
        // log_print("数据%d,  地址1:%d, 地址2:%d,地址3:%d,长度:%d",dataPtr,i,ctxt->pWaveHdr[i].data,xxx,buflen);
    }
    // create mutex & cond
    pthread_mutex_init(&ctxt->adev_lock, NULL);
    pthread_cond_init (&ctxt->cond, NULL);

    // // create audio rendering thread
    // pthread_create(&ctxt->thread, NULL, audio_render_thread_proc, ctxt);
    return ctxt;
}

void adev_destroy(void *ctxt)
{
    if (!ctxt) return;

    ADEV_CONTEXT *c = (ADEV_CONTEXT*)ctxt;

    // make audio rendering thread safely exit
    pthread_mutex_lock(&c->adev_lock);
    c->status |= ADEV_CLOSE;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->adev_lock);
    // pthread_join(c->thread, NULL);

    // close mutex & cond
    pthread_mutex_destroy(&c->adev_lock);
    pthread_cond_destroy (&c->cond);


    // free adev
    free(c);
}

void adev_write(void *ctxt, uint8_t *buf, int len, int64_t pts)
{
    if (!ctxt) return;
    ADEV_CONTEXT *c = (ADEV_CONTEXT*)ctxt;
    // log_print("c->curnum : %d",c->curnum);
    // log_print("c->bufnum : %d",c->bufnum);
    // log_print("len : %d",len);
    // log_print("数据%d,  地址:%d, 长度:%d",c->tail,c->pWaveHdr[c->tail].data,c->pWaveHdr[c->tail].size);
    // log_print("我在");
    pthread_mutex_lock(&c->adev_lock);
    // log_print("我ok了");

    while (c->curnum == c->bufnum && (c->status & ADEV_CLOSE) == 0)  {
        // log_print("音频设备 等待");
        pthread_cond_wait(&c->cond, &c->adev_lock);
        // log_print("音频设备  结束 等待");
    }
    if (c->curnum < c->bufnum) {
        void* dst = (void*)c->pWaveHdr[c->tail].data;
        void* src = (void*)buf;
        size_t copySize =  MIN(c->pWaveHdr[c->tail].size, len);
        memcpy(dst, src,copySize);
        c->curnum++; 
        c->ppts[c->tail] = pts; 
        if (++c->tail == c->bufnum) 
            c->tail = 0;
        pthread_cond_signal(&c->cond);
    }
    pthread_mutex_unlock(&c->adev_lock);
}

void adev_pause(void *ctxt, int pause)
{
    if (!ctxt) return;
    // JNIEnv *env = get_jni_env();
    ADEV_CONTEXT *c = (ADEV_CONTEXT*)ctxt;
    if (pause) {
        c->status |=  ADEV_PAUSE;
    } else {
        c->status &= ~ADEV_PAUSE;
    }
}

void adev_reset(void *ctxt)
{
    if (!ctxt) return;
    ADEV_CONTEXT *c = (ADEV_CONTEXT*)ctxt;
    pthread_mutex_lock(&c->adev_lock);
    c->head = c->tail = c->curnum = c->status = 0;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->adev_lock);
}

void adev_setparam(void *ctxt, int id, void *param)
{
    if (!ctxt) return;
    ADEV_CONTEXT *c = (ADEV_CONTEXT*)ctxt;
    switch (id) {
    case PARAM_RENDER_STOP:
        pthread_mutex_lock(&c->adev_lock);
        c->status |= ADEV_CLOSE;
        pthread_cond_signal(&c->cond);
        pthread_mutex_unlock(&c->adev_lock);
        break;
    case PARAM_ADEV_RENDER_COMPLETED:
        pthread_mutex_lock(&c->adev_lock);
        c->status |= ADEV_COMPLETED;
        pthread_cond_signal(&c->cond);
        pthread_mutex_unlock(&c->adev_lock);
        break;
    }
}

void adev_getparam(void *ctxt, int id, void *param) {}
