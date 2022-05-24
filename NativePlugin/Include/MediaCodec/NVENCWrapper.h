#pragma once
#include <fstream>
#include "../Utils/Logger.h"
#include "NvEncoder/NvEncoderGL.h"
#include "MediaEncodeConfig.h"
#include "OpenGLConfig.h"

class NvEncoderGL;


struct NalCacheData
{
    void* cacheCurrentData = NULL;
    //总大小
    int cacaheDataLength = 0;
    //当前占用
     int cacaheDataIndex = 0;
};






class NVENCWrapper
{
private:
    static int ID_GEN;
    
    //编码器核心
    NvEncoderGL* enc;
    //当前帧Index
    int nFrame = 0;
    //帧缓存
    unsigned int fbo;
    //顶点
    unsigned int quad_vertexbuffer;
    //shader
    unsigned int quad_programID;
    //shader输入纹理ID
    unsigned int textureSamplerID;
    GLuint sourceTex;

    //manual free
    MediaEncodeConfig* media_config;
    OpenGLConfig* opengl_config;

    // data callback
    // void (*DataCallBack)(void * data, int data_size);


    //cache nal data 
    NalCacheData*  NalCacheDatas  = NULL;
    int nalCahceWriteIndex = 0;
    int nalCacheReadIndex = 0;
    bool flag_meta_fore_idr = false;
    


    NV_ENC_INITIALIZE_PARAMS* initializeParams;
public:
    int myID;
    bool runing;

    pthread_t tids;
    pthread_mutex_t count_mutex;
    pthread_cond_t count_threshold_cv;


    pthread_mutex_t event_mutex;
    pthread_cond_t event_threshold_cv;

    pthread_mutex_t update_cache_mutex;

    int event_code;




private:    
    void handlePreload(bool update_config = false);
    void handleEncodeTexture();
    void handleCodecProcess();
    void handleCodecForceIDRProcess();
    void handleReleaseResource();
    void handleStopEncode();

    void preloadOpenglResource();


    int CoutSpanSize(int targetValue);
    void PushDataFromMediaEncoder(void* data,int count );


    bool ConfigForRecording();
    bool ConfigForLowLatency(int nWidth,int nHeight);
public:
    NVENCWrapper(/* args */);
    ~NVENCWrapper();

    //通过预设的参数  加载
    bool UpdateConfig(MediaEncodeConfig* config);
    void EncodeTexture(void* texture,bool forceIDR = false);
    void StopEncode();
    void LoopQueue();
    int GetNalData(void* nal_data);


};


