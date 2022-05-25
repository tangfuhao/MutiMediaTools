#ifndef __MEDIAPLAYER_ADEV_H__
#define __MEDIAPLAYER_ADEV_H__


#include "mediaplayer.h"
#include "ffrender.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADEV_SAMPLE_RATE  48000
#define ADEV_SAMPLE_PER_CALL  1024
#define ADEV_CLOSE       (1 << 0)
#define ADEV_PAUSE       (1 << 1)
#define ADEV_COMPLETED  (1 << 2)

//++ adev context common members
#define ADEV_COMMON_MEMBERS \
    int64_t    *ppts;       \
    int16_t    *bufcur;     \
    int         bufnum;     \
    int         buflen;     \
    int         head;       \
    int         tail;       \
    int         status;     \
    CMNVARS    *cmnvars;
//-- adev context common members

typedef struct {
    int16_t *data;
    int32_t  size;
} AUDIOBUF;



typedef struct {
    ADEV_COMMON_MEMBERS
} ADEV_COMMON_CTXT;


typedef struct {
    ADEV_COMMON_MEMBERS

    uint8_t   *pWaveBuf;
    AUDIOBUF  *pWaveHdr;
    int        curnum;

    //++ for audio render thread
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    // pthread_t  thread;
    //-- for audio render thread

} ADEV_CONTEXT;

void* adev_create  (int type, int bufnum, int buflen, CMNVARS *cmnvars);
void  adev_destroy (void *ctxt);
void  adev_write   (void *ctxt, uint8_t *buf, int len, int64_t pts);
void  adev_pause   (void *ctxt, int pause);
void  adev_reset   (void *ctxt);
void  adev_setparam(void *ctxt, int id, void *param);
void  adev_getparam(void *ctxt, int id, void *param);

#ifdef __cplusplus
}
#endif

#endif

