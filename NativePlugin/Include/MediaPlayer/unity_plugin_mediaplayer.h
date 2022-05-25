#pragma once
#include <pthread.h>
#include "../mediaplayer/stdefine.h"
#include "stdbool.h"

typedef struct {
    void* bits;
    int stride;
} UnityTexture_Buffer;

typedef struct {
    int vw;
    int vh;
    int rotation_degress;
} MediaInfo;


//用来包装一个播放器的渲染控制
typedef struct {
    // pthread_mutex_t lock;
    // pthread_cond_t  cond;
    // pthread_cond_t  wait_write_cond;
    void* player;
    void* textureID;
    //准备写数据
    bool isWriteBackupData ;
    bool lastReadBackupData;

    int playerID;
    int textureWidth;
    int textureHeight;
    void* dataBuffer;
    void* dataBufferBackup;
} PLAYER_WRAPPER;


int UnityTextureLock(void* _wrapper, int textureWidth,int textureHeight,UnityTexture_Buffer* buffer );
void UnityTextureUnlockAndPost(void* _wrapper);
void UnityPlayerWrapperRelease(void* _wrapper);
int UnityWriteAudioCallback(void* _wrapper,void* audioData,int length,int audioChannel);

void* NativeCreateMediaPlayer (const char* _file,const char* _player_params);
void NativeMediaPlayerSetTexture(void* playerWrapper,void* texture);
void NativeMediaPlayerPlay(void* playerWrapper);
void NativeMediaPlayerStop(void* playerWrapper);
void NativeMediaPlayerPause(void* playerWrapper);
void NativeMediaPlayerSeek(void* playerWrapper,long ms);
long NativeMediaPlayerPosition(void* playerWrapper);
long NativeMediaPlayerLength(void* playerWrapper);
void NativeMeddiaPlayerGetParams(void* wrapper,MediaInfo* mediaInfoPtr);

void UnityPostMessage(void *_wrapper, int32_t msg);



//Create a callback delegate
typedef void(*FuncCallBack)(void *_wrapper, int msg);
static FuncCallBack messageCallbackInstance = NULL;
void RegisterMessageCallback(FuncCallBack cb);