#include "unity_plugin_mediaplayer.h"
#include "../mediaplayer/mediaplayer.h"
#include "../mediaplayer/adev.h"
#include "Unity/IUnityInterface.h"
#include "Unity/IUnityGraphics.h"
#include <GL/gl.h>
#include <stdlib.h>
#include"logger.h"


PLAYER_WRAPPER*  gloabal_player_wrappers[64];
int  gloabal_player_wrappers_length = 0;


int UnityWriteAudio(void *param,void* audioData,int length,int audioChannel){
    int result = 2;
    ADEV_CONTEXT *c = (ADEV_CONTEXT*)param;
    if(c && !(c->status & ADEV_CLOSE) && (c->status & ADEV_PAUSE) == 0 && !(c->status & ADEV_COMPLETED)){
        pthread_mutex_lock(&c->adev_lock);


        if (c->curnum > 0 &&(c->status & ADEV_CLOSE) == 0 && (c->status & ADEV_COMPLETED) == 0 ) {
            // log_print("UnityWriteAudio 数据块长度：%d,拷贝数据:%d",length,c->pWaveHdr[c->tail].size);
            memcpy(audioData , c->pWaveHdr[c->head].data    , c->pWaveHdr[c->tail].size);
            c->curnum--; 
            c->bufcur = c->pWaveHdr[c->head].data;
            c->cmnvars->apts = c->ppts[c->head];
            
            if (++c->head == c->bufnum) c->head = 0;
            result = 0;
            pthread_cond_signal(&c->cond);
        }else{
           if((c->status & ADEV_CLOSE) == 0 ){
                result=4;
            }else if((c->status & ADEV_PAUSE) == 0 ){
                result=3;
            }else  if(c->curnum == 0){
                result = 2;
            }
        }

        pthread_mutex_unlock(&c->adev_lock);
    }else{
        if((c->status & ADEV_CLOSE) == 0 ){
            result=4;
        }else if((c->status & ADEV_PAUSE) == 0 ){
            result=3;
        }else  if(c->curnum == 0){
            result = 2;
        }
    }


    return result;


}


//0=有效 
//1= 设备未初始化
//2=没有新的数据
//3=设备暂停
//4=设备停止
//
int UnityWriteAudioCallback(void* _wrapper,void* audioData,int length,int audioChannel){
    PLAYER_WRAPPER* wrapper = (PLAYER_WRAPPER*)_wrapper;
    if(wrapper == NULL) return 1;

    void* hplayer = wrapper->player;
    if(hplayer){
        int asteam_id;
        player_getparam(hplayer, PARAM_ASTEAM_ID, &asteam_id);
        if(asteam_id != -1){
            void* adevContext;
            player_getparam(hplayer, PARAM_ADEV_GET_CONTEXT, &adevContext);
            
            return UnityWriteAudio(adevContext,audioData,length,audioChannel);
        }
    }
    return 1;
}






long NativeMediaPlayerPosition(void* playerWrapper){
    PLAYER_WRAPPER* wrapper = (PLAYER_WRAPPER*)playerWrapper;
    if(wrapper == NULL) return -1;

    void* hplayer = wrapper->player;
    if(hplayer){
        int64_t ms;
        player_getparam(hplayer, PARAM_MEDIA_POSITION, &ms);
        return ms;
    }

    return -1;

}


long NativeMediaPlayerLength(void* playerWrapper){
    PLAYER_WRAPPER* wrapper = (PLAYER_WRAPPER*)playerWrapper;
    if(wrapper == NULL) return -1;

    void* hplayer = wrapper->player;
    if(hplayer){
        int64_t ms;
        player_getparam(hplayer, PARAM_MEDIA_DURATION, &ms);
        return ms;
    }

    return -1;
}







//解开锁 结束
void UnityPlayerWrapperRelease(void* _wrapper){
    // PLAYER_WRAPPER* wrapper = (PLAYER_WRAPPER*)_wrapper;
    // pthread_mutex_lock(&wrapper->lock);
    // if(wrapper->isPrepareWriteData){
    //     wrapper->isPrepareWriteData  = 0; 
    //     pthread_cond_signal(&wrapper->cond);
    // } 
    // pthread_mutex_unlock(&wrapper->lock);
}



/**
 * 锁定需要渲染的纹理
 * ret =0,成功 ret !=0,失败
 **/
int UnityTextureLock(void* _wrapper, int textureWidth,int textureHeight,UnityTexture_Buffer *buffer ){
    PLAYER_WRAPPER* wrapper = (PLAYER_WRAPPER*)_wrapper;
    if(wrapper == NULL) return -1;

        //更新buffer
    const int rowPitch = textureWidth;
    if(wrapper->dataBuffer == NULL || wrapper->textureWidth != textureWidth || wrapper->textureHeight != textureHeight){
        if(wrapper->dataBuffer){
            free(wrapper->dataBuffer);
            free(wrapper->dataBufferBackup);
        }

        
        wrapper->dataBuffer = malloc(rowPitch * textureHeight  * 4);
        // memset(wrapper->dataBuffer,0,rowPitch * textureHeight  * 4);
        wrapper->dataBufferBackup = malloc(rowPitch * textureHeight  * 4);
        // memset(wrapper->dataBufferBackup,0,rowPitch * textureHeight  * 4);
        wrapper->textureWidth = textureWidth;
        wrapper->textureHeight = textureHeight;
    }

    
    void* useData = wrapper->isWriteBackupData?wrapper->dataBufferBackup:wrapper->dataBuffer;


    //映射
    buffer->stride = rowPitch;
    buffer->bits = useData;

    return 0;
}

//释放缓存数据块
void UnityTextureUnlockAndPost(void* _wrapper){
    PLAYER_WRAPPER* wrapper = (PLAYER_WRAPPER*)_wrapper;
    if(wrapper == NULL) return;
    //通知写入完成,翻转缓存
    wrapper->isWriteBackupData = !wrapper->isWriteBackupData;
}




//---------------------------------- 接口 --------------------------------------------

//创建播放器
void* NativeCreateMediaPlayer (const char* _file,const char* _player_params){

    char * file = strdup(_file);
    char * player_params = strdup(_player_params);


    PLAYER_WRAPPER* player_wrapper =  (PLAYER_WRAPPER*)calloc(1, sizeof(PLAYER_WRAPPER));
    if(player_wrapper == NULL) return;
    // pthread_mutex_init(&player_wrapper->lock, NULL);
    // pthread_cond_init (&player_wrapper->cond, NULL);
    // pthread_cond_init (&player_wrapper->wait_write_cond, NULL);
    
    gloabal_player_wrappers[gloabal_player_wrappers_length++] = player_wrapper;
    printf("#=#===     create mediaplayer index:%d \n ",gloabal_player_wrappers_length);

    PLAYER_INIT_PARAMS playerparams;
    player_load_params(&playerparams, player_params);
    void* hplayer = player_open(file, player_wrapper, &playerparams);

    player_wrapper->player = hplayer;
    
    return player_wrapper;
}

void NativeMediaPlayerSetTexture(void* playerWrapper,void* texture){
    PLAYER_WRAPPER* player_wrapper = (PLAYER_WRAPPER*)playerWrapper;
    if(player_wrapper == NULL) return;
    player_wrapper->textureID = texture;
}

void NativeMediaPlayerPlay(void* playerWrapper){
    PLAYER_WRAPPER* player_wrapper = (PLAYER_WRAPPER*)playerWrapper;
    if(player_wrapper == NULL) return;

    void* hplayer = player_wrapper->player;
    player_play(hplayer);
}

void NativeMediaPlayerStop(void* playerWrapper){
    PLAYER_WRAPPER* player_wrapper = (PLAYER_WRAPPER*)playerWrapper;
    if(player_wrapper == NULL) return;

    void* hplayer = player_wrapper->player;
    player_close((void*)hplayer);


    // //释放 player wrapper
    // pthread_mutex_destroy(&player_wrapper->lock);
    // pthread_cond_destroy (&player_wrapper->cond);
    // pthread_cond_destroy (&player_wrapper->wait_write_cond);

    if(player_wrapper->dataBuffer){
        free(player_wrapper->dataBuffer);
        free(player_wrapper->dataBufferBackup);
        free(player_wrapper);
    }
}

void NativeMediaPlayerPause(void* playerWrapper){
    PLAYER_WRAPPER* player_wrapper = (PLAYER_WRAPPER*)playerWrapper;
    if(player_wrapper == NULL) return;

    void* hplayer = player_wrapper->player;
    player_pause((void*)hplayer);
}

void NativeMediaPlayerSeek(void* playerWrapper,long ms){
    PLAYER_WRAPPER* player_wrapper = (PLAYER_WRAPPER*)playerWrapper;
    if(player_wrapper == NULL) return;

    void* hplayer = player_wrapper->player;
    player_seek((void*)hplayer, ms, 0);
}





//获取当前媒体的信息
void NativeMeddiaPlayerGetParams(void* wrapper,MediaInfo* mediaInfo){
    PLAYER_WRAPPER* player_wrapper = (PLAYER_WRAPPER*)wrapper;
    if(player_wrapper == NULL) return;

    void* hplayer = player_wrapper->player;
    player_getparam(hplayer, PARAM_VIDEO_WIDTH , &mediaInfo->vw);
    player_getparam(hplayer, PARAM_VIDEO_HEIGHT, &mediaInfo->vh);

    PLAYER_INIT_PARAMS params;
    player_getparam(hplayer, PARAM_PLAYER_INIT_PARAMS, &params);
    // log_print("2角度是：%d",params.video_rotate);
    mediaInfo->rotation_degress =  params.video_rotate;
}


//推到消息队列
void UnityPostMessage(void *_wrapper, int32_t msg){
    if(msg == MSG_PLAY_COMPLETED){
        PLAYER_WRAPPER* player_wrapper = (PLAYER_WRAPPER*)_wrapper;
        if(player_wrapper == NULL) return;

        void* hplayer = player_wrapper->player;
        //如果收到vdev的完成，那么让adev也完成
        player_setparam(hplayer,PARAM_ADEV_RENDER_COMPLETED,NULL);
        player_seek(hplayer,0,0);
    }

    if (messageCallbackInstance != NULL){
        messageCallbackInstance(_wrapper,msg);
    }
}


//Create a callback delegate
void RegisterMessageCallback(FuncCallBack cb) {
    messageCallbackInstance = cb;
}



//----------------------------UNITY RENDERING CALLBACK-----------------------------

PLAYER_WRAPPER* findPlayer(int eventID){
    int playerID = eventID & ~0x1;
    // printf("#=#===     findPlayerl    wrappers_length:%d  eventID:%d    playerID:%d \n ",gloabal_player_wrappers_length,eventID,playerID);
    for (size_t i = 0; i < gloabal_player_wrappers_length; i++)
    {
        PLAYER_WRAPPER*  wrapperStruct = gloabal_player_wrappers[i];
        if(wrapperStruct->playerID ==playerID ) return wrapperStruct;
    }
    return NULL;
}

// Plugin function to handle a specific rendering event
static void UNITY_INTERFACE_API OnRenderEventFlushBackBuffer(int eventID)
{
    bool isRuntimePlayer = eventID & ((int)1);
    
    PLAYER_WRAPPER* player_wrapper = findPlayer(eventID);
    //检查，是否准备写数据
    if(player_wrapper == NULL || player_wrapper->textureID == NULL) return;

    
    

    // if(player_wrapper->lastReadBackupData == player_wrapper->isWriteBackupData){
    //     log_print("视频获取  跳过一帧");
    //     return;
    // }
    // log_print("拷贝  渲染一帧");
    void* useData = player_wrapper->isWriteBackupData?player_wrapper->dataBuffer:player_wrapper->dataBufferBackup;
    player_wrapper->lastReadBackupData = player_wrapper->isWriteBackupData; 
    
    int textureWidth = player_wrapper->textureWidth;
    int textureHeight = player_wrapper->textureHeight;
    void* textureHandle = player_wrapper->textureID;
    GLuint gltex = (GLuint)(size_t)(textureHandle);
	// Update texture data, and free the memory buffer
	glBindTexture(GL_TEXTURE_2D, gltex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, textureWidth, textureHeight, GL_RGBA, GL_UNSIGNED_BYTE, useData);


    // pthread_mutex_unlock(&player_wrapper->lock);

}



// Freely defined function to pass a callback to plugin-specific scripts
UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFlushBackBufferFunc(int id,void* _wrapper)
{
    PLAYER_WRAPPER* player_wrapper = (PLAYER_WRAPPER*)_wrapper;
    if(player_wrapper->playerID < 2){
        // printf("#=#===     push call  1 id:%d  playerID:%d \n ",id,player_wrapper->playerID);
        int playerID = id & ~0x1;
        player_wrapper->playerID = playerID;
        // printf("#=#===     push call  2  playerID:%d \n ",player_wrapper->playerID);

    }
    return OnRenderEventFlushBackBuffer;
}