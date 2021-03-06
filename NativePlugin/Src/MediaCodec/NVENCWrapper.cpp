#include "NVENCWrapper.h"
#include "../Utils/NvEncoderCLIOptions.h"
#include "../Utils/NvCodecUtils.h"
#include <pthread.h>

#ifdef __linux
#include "GraphicsUtils.h"
#include "RenderAPI.h"
#include "shader.hpp"
#endif
//#include <FreeImage.h>

#define VertexShaderCode \
"#version 330 core\n\
// Input vertex data, different for all executions of this shader.\n\
layout(location = 0) in vec3 vertexPosition;\n\
// Output data ; will be interpolated for each fragment.\n\
out vec2 UV;\n\
void main(){\
	gl_Position =  vec4(vertexPosition.x,-vertexPosition.y,vertexPosition.z,1.0);\n\
	UV = (vertexPosition.xy+vec2(1,1))/2.0;\n\
}"

#define FragmentShaderCode \
"#version 330 core\n\
// Ouput data\n\
layout(location = 0) out vec4 color;\n\
uniform sampler2D textureSampler;\n\
in vec2 UV;\n\
void main(){\n\
	color = texture2D(textureSampler, UV);\n\
    color.rgb = pow(color.rgb, vec3(1.0/2.2));\n\
}"


simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();


#ifdef __linux
extern RenderAPI* s_CurrentAPI;
#endif

namespace NVENCWrapperParams{
    const int INIT = 0;
    const int ENCODE = 1;
    const int CLOSE = 2;
    const int ENCODE_FORCE_IDR = 3;
    const int UPDATE_CONFIG = 4;
}

int NVENCWrapper::ID_GEN = 0;

void* ExcuteCodecThread(void *wrapper){
    NVENCWrapper* NvidiaWrapper = (NVENCWrapper*)wrapper;
    //1.make true thread running
    pthread_mutex_lock(&(NvidiaWrapper->count_mutex));
    NvidiaWrapper->runing = true; 
    pthread_cond_signal(&(NvidiaWrapper->count_threshold_cv));
    pthread_mutex_unlock(&(NvidiaWrapper->count_mutex));

    NvidiaWrapper->LoopQueue();
    pthread_exit(NULL);
    LOG(INFO) << "ExcuteCodecThread: pthread_exit ";
    return NULL;
}


NVENCWrapper::NVENCWrapper(/* args */) {
    runing = false;
    enc = NULL;
    media_config = NULL;
    initializeParams = NULL;
    myID = ID_GEN++;
    #ifdef __linux
    opengl_config = NULL;
    #else
    pixelData = NULL;
    #endif

    
    pthread_mutex_init(&count_mutex, NULL);
    pthread_cond_init (&count_threshold_cv, NULL);
    pthread_mutex_init(&event_mutex, NULL);
    pthread_cond_init (&event_threshold_cv, NULL);
    pthread_mutex_init(&update_cache_mutex, NULL);
    

    pthread_mutex_lock(&count_mutex);
    /* ?????????????????????????????????????????????????????? */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    //?????????????????????
    int pthread_ret = pthread_create(&tids, &attr, ExcuteCodecThread, (void *)this);
    if (pthread_ret)
    {
        std::cout << "NVENCWrapper pthread_create error: error_code=" << pthread_ret << std::endl;
    }else{
        //????????????
        pthread_cond_wait(&count_threshold_cv, &count_mutex);
    }
    pthread_mutex_unlock(&count_mutex);
}

NVENCWrapper::~NVENCWrapper() {
    LOG(INFO) << "NVENCWrapper:  deconstuct";
	if(media_config != NULL){
        free(media_config);
        media_config = NULL;
    }

    if(initializeParams != NULL){
        free(initializeParams->encodeConfig);
        free(initializeParams);
        initializeParams = NULL;
    }

    pthread_mutex_destroy(&count_mutex);
    pthread_mutex_destroy(&event_mutex);
    pthread_mutex_destroy(&update_cache_mutex);
    
    pthread_cond_destroy(&count_threshold_cv);
    pthread_cond_destroy(&event_threshold_cv);


    if(NalCacheDatas){
        for (size_t i = 0; i < 5; i++)
        {
            NalCacheData* data = NalCacheDatas+i;
            if(data->cacheCurrentData ){
                free(data->cacheCurrentData);
                data->cacheCurrentData = NULL;
            }
        }

        free(NalCacheDatas);
        NalCacheDatas = NULL;
    }
}

void NVENCWrapper::LoopQueue(){
    pthread_mutex_lock(&(this->event_mutex));
    //2.wait new events
    while (this->runing)
    {
        pthread_cond_wait(&(this->event_threshold_cv), &(this->event_mutex));
        //get event 
        switch (this->event_code)
        {
            case NVENCWrapperParams::INIT:
                LOG(INFO) << "?????????id:" << this->myID << "?????????";
                this->handlePreload(false);
                break;
            case NVENCWrapperParams::UPDATE_CONFIG:
                LOG(INFO) << "?????????id:" << this->myID << "????????????";
                this->handlePreload(true);
                break;
            case NVENCWrapperParams::ENCODE:
                this->handleEncodeTexture();
                this->handleCodecProcess();
                break;
            case NVENCWrapperParams::ENCODE_FORCE_IDR:
                this->handleEncodeTexture();
                this->handleCodecForceIDRProcess();
                break;
            case NVENCWrapperParams::CLOSE:
                LOG(INFO) << "???????????????id:" << this->myID ;
                this->handleStopEncode();
                this->handleReleaseResource();
                LOG(INFO) << "???????????????id:" << this->myID << "??????";
                break;        
            default:
                break;
        }
    }
    pthread_mutex_unlock(&(this->event_mutex));
}


void NVENCWrapper::EncodeTexture(void* texture,bool forceIDR){
    #ifdef __linux
    sourceTex = (GLuint)(size_t)(texture);
    #else
    LOG(INFO) << "NVENCWrapper:  EncodeTexture";
    if(this->pixelData == NULL){
        LOG(INFO) << "??????????????????:" << enc->GetFrameSize();
        this->pixelData = malloc(enc->GetFrameSize());
    }
    // memset(this->pixelData,100,enc->GetFrameSize());
    memcpy(this->pixelData,texture,enc->GetFrameSize());
    #endif

    //????????????
    pthread_mutex_lock(&(event_mutex));
    if(forceIDR || flag_meta_fore_idr){
        flag_meta_fore_idr = false;
        event_code = NVENCWrapperParams::ENCODE_FORCE_IDR;
    }else{
        event_code = NVENCWrapperParams::ENCODE;
    }
    
    pthread_cond_signal(&(event_threshold_cv));
    pthread_mutex_unlock(&(event_mutex));  
}

void NVENCWrapper::StopEncode() {
    //??????????????????
    LOG(INFO) << "?????????ID:" << myID << "????????????";
    pthread_mutex_lock(&(event_mutex));
    event_code = NVENCWrapperParams::CLOSE;
    runing = false;
    pthread_cond_signal(&(event_threshold_cv));
    pthread_mutex_unlock(&(event_mutex));  

    LOG(INFO) << "?????????ID:" << myID << "??????????????????";
    //?????????????????????
    pthread_join(tids,NULL);
    LOG(INFO) << "?????????ID:" << myID << "??????????????????";


    
}

bool NVENCWrapper::UpdateConfig(MediaEncodeConfig* config){
    LOG(INFO) << "====    UpdateConfig  bitRate:  "<< config->bitRate;
    if(config == NULL) return false;
    #ifdef __linux
    if(s_CurrentAPI->openglConfig == NULL) return false;
    #endif
    //????????????
    if(this->media_config){
        memcpy(this->media_config,config,sizeof(MediaEncodeConfig));

        //????????????
        pthread_mutex_lock(&(event_mutex));
        event_code = NVENCWrapperParams::UPDATE_CONFIG;
        pthread_cond_signal(&(event_threshold_cv));
        pthread_mutex_unlock(&(event_mutex));
    }else{
        void* temp = malloc(sizeof(MediaEncodeConfig));
        memcpy(temp,config,sizeof(MediaEncodeConfig));
        
        // LOG(INFO) << "add checck   1:" <<  &*(this->media_config) << " 2:" <<  &*config << " 3:" << temp;
        this->media_config = (MediaEncodeConfig*)temp;

        #ifdef __linux
        // LOG(INFO) << "add checck   2:" <<  &*(this->media_config) << " 2:" <<  &*config;
        this->opengl_config = (OpenGLConfig*)s_CurrentAPI->openglConfig;
        #endif
        // {
        //     std::stringstream params;
        //     params << "inputWidth:" << config->inputWidth
        //                     << "inputHeight:" << config->inputHeight
        //                     << "codecWidth:" << config->codecWidth
        //                     << "codecHeight:" << config->codecHeight
        //                     << "frameRate:" << config->frameRate
        //                     << "bitRate:" << config->bitRate
        //                     << "recordType:" << config->recordType;

        //     LOG(INFO) << "====  config 1 ======  " <<  params.str().c_str();
        // }

        // {
        //     std::stringstream params;
        //     params << "inputWidth:" << media_config->inputWidth
        //                     << "inputHeight:" << media_config->inputHeight
        //                     << "codecWidth:" << media_config->codecWidth
        //                     << "codecHeight:" << media_config->codecHeight
        //                     << "frameRate:" << media_config->frameRate
        //                     << "bitRate:" << media_config->bitRate
        //                     << "recordType:" << media_config->recordType;

        //     LOG(INFO) << "====  config  2 ======  " <<  params.str().c_str();
        // }


        //????????????
        pthread_mutex_lock(&(event_mutex));
        event_code = NVENCWrapperParams::INIT;
        pthread_cond_signal(&(event_threshold_cv));
        pthread_mutex_unlock(&(event_mutex));  
    }

    return true;
}

int NVENCWrapper::GetNalData(void* nal_data){
    // LOG(INFO) << "NVENCWrapper:  GetNalData";
    if(NalCacheDatas == NULL){
        NalCacheDatas  = (NalCacheData*)calloc(5, sizeof(NalCacheData) );
    }
    assert(nalCahceWriteIndex < 5);
    assert(nalCahceWriteIndex < 5);
    // LOG(INFO) << "====    encodeJpegFromScreenPixel  start  nalCacheReadIndex:  "<<nalCacheReadIndex;
    int h264Length = 0;
    pthread_mutex_lock(&update_cache_mutex);
    if(nalCacheReadIndex == nalCahceWriteIndex) {
        LOG(INFO) << "====    encodeJpegFromScreenPixel  no data    nalCacheReadIndex: "<<nalCacheReadIndex<<",nalCahceWriteIndex:"<<nalCahceWriteIndex;
        pthread_mutex_unlock(&update_cache_mutex);
        return h264Length;
    }

    NalCacheData* nalCacheData = NalCacheDatas+nalCacheReadIndex;
    // LOG(INFO) << "====    encodeJpegFromScreenPixel  nalCacheData address:" << nalCacheData;
    nalCacheReadIndex = nalCacheReadIndex == 4? 0: nalCacheReadIndex + 1;

    h264Length = nalCacheData->cacaheDataIndex;
    memcpy(nal_data,nalCacheData->cacheCurrentData,nalCacheData->cacaheDataIndex);


    // LOG(INFO) << "====    encodeJpegFromScreenPixel  finish  h264Length :"<<h264Length << ",nalCacheReadIndex: "<<nalCacheReadIndex<<",nalCahceWriteIndex:"<<nalCahceWriteIndex;

    pthread_mutex_unlock(&update_cache_mutex);
    return h264Length;
}

 void NVENCWrapper::PushDataFromMediaEncoder(void* data,int count ){
    // LOG(INFO) << "====    pushDataFromMediaEncoder  start  count:"<<count;
    if(NalCacheDatas == NULL){
        NalCacheDatas  = (NalCacheData*)calloc(5, sizeof(NalCacheData) );
    }

    assert(nalCahceWriteIndex < 5);
    assert(nalCahceWriteIndex < 5);

    pthread_mutex_lock(&update_cache_mutex);

    int nextWriteIndex = nalCahceWriteIndex == 4 ? 0 : nalCahceWriteIndex+1;

    //clean array
    if(nextWriteIndex == nalCacheReadIndex){
        LOG(INFO) << "====    pushDataFromMediaEncoder  clean array  .nextWriteIndex:"<<nextWriteIndex<<",nalCacheReadIndex:"<<nalCacheReadIndex<<",nalCahceWriteIndex:"<<nalCahceWriteIndex;
        flag_meta_fore_idr = true;
        nalCahceWriteIndex = nextWriteIndex;
        pthread_mutex_unlock(&update_cache_mutex);
        return;
    }


     NalCacheData* nalCacheData = NalCacheDatas+nalCahceWriteIndex;
    //  LOG(INFO) << "====    pushDataFromMediaEncoder  nalCahceWriteIndex:" << nalCahceWriteIndex <<", nalCacheData address:" << nalCacheData;
     nalCahceWriteIndex = nextWriteIndex;

    //??????
    if(nalCacheData->cacheCurrentData == NULL ||  nalCacheData->cacaheDataLength < count ){
        free(nalCacheData->cacheCurrentData);
        nalCacheData->cacheCurrentData = NULL;
        nalCacheData->cacaheDataLength = CoutSpanSize(count);
        nalCacheData->cacheCurrentData = malloc(nalCacheData->cacaheDataLength);
    }

    nalCacheData->cacaheDataIndex = count;
    memcpy(nalCacheData->cacheCurrentData,data,nalCacheData->cacaheDataIndex);
    // LOG(INFO) << "====    pushDataFromMediaEncoder  finish  ,nalCacheReadIndex:"<<nalCacheReadIndex<<",nalCahceWriteIndex:"<<nalCahceWriteIndex<<",cacaheDataIndex:"<<nalCacheData->cacaheDataIndex;
    pthread_mutex_unlock(&update_cache_mutex);
    

}

////////////////////////////////////////////////////////////////////////////////////////////////////////

bool NVENCWrapper::ConfigForRecording(){
     LOG(INFO) << "????????????????????????";
    std::stringstream params;
    params << " -profile main" 
        << " -bitrate " << media_config->bitRate 
        << " -fps " << media_config->frameRate 
        << " -bf 0"
        << " -vbvbufsize " << media_config->bitRate   * 4
        << " -lookahead 0";
    NvEncoderInitParam encodeCLIOptions = NvEncoderInitParam(params.str().c_str());
    NvEncoderInitParam *encodeCLIOptionsPtr = &encodeCLIOptions;
    enc->CreateDefaultEncoderParams(initializeParams, encodeCLIOptionsPtr->GetEncodeGUID(),
        encodeCLIOptionsPtr->GetPresetGUID(),encodeCLIOptionsPtr->GetTuningInfo());
    encodeCLIOptionsPtr->SetInitParams(initializeParams, NV_ENC_BUFFER_FORMAT_ABGR);
    
    LOG(INFO) << "NativeOpenNVENC: CreateEncoder  ";

    return true;
}

bool NVENCWrapper::ConfigForLowLatency(int nWidth,int nHeight){
    LOG(INFO) << "????????????????????????";
    NV_ENC_CONFIG* encodeConfig = initializeParams->encodeConfig;

    std::stringstream params;
    params << " -profile baseline" 
        << " -codec h264"
        << " -fps " << media_config->frameRate 
        << " -rc cbr"
        << " -tuninginfo lowlatency";
    NvEncoderInitParam encodeCLIOptions = NvEncoderInitParam(params.str().c_str(),NULL,true);
    NvEncoderInitParam *encodeCLIOptionsPtr = &encodeCLIOptions;
    enc->CreateDefaultEncoderParams(initializeParams, encodeCLIOptionsPtr->GetEncodeGUID(), encodeCLIOptionsPtr->GetPresetGUID(),
                encodeCLIOptionsPtr->GetTuningInfo());
    
    encodeConfig->gopLength = 30 * 5 ;
    encodeConfig->frameIntervalP = 1;

    if (encodeCLIOptionsPtr->IsCodecH264())
    {
        encodeConfig->encodeCodecConfig.h264Config.idrPeriod = 30 * 5;
    }
    else
    {
        encodeConfig->encodeCodecConfig.hevcConfig.idrPeriod = 30 * 5;
    }

    

    encodeConfig->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encodeConfig->rcParams.multiPass = NV_ENC_TWO_PASS_FULL_RESOLUTION;
    encodeConfig->rcParams.averageBitRate = media_config->bitRate > 0 ? media_config->bitRate :  (static_cast<unsigned int>(15.0f * nWidth * nHeight) / (1280 * 720)) * 100000;
    encodeConfig->rcParams.vbvBufferSize = (encodeConfig->rcParams.averageBitRate * initializeParams->frameRateDen / initializeParams->frameRateNum) * 5;
    encodeConfig->rcParams.maxBitRate = encodeConfig->rcParams.averageBitRate;
    encodeConfig->rcParams.vbvInitialDelay = encodeConfig->rcParams.vbvBufferSize;

    encodeCLIOptionsPtr->SetInitParams(initializeParams, NV_ENC_BUFFER_FORMAT_ABGR);
    return true;
}

void NVENCWrapper::handlePreload(bool update_config){
    {
        //test output config
        LOG(INFO) << "add checck   3:" <<  &*(this->media_config) ;
        std::stringstream params;
        params << "inputWidth:" << media_config->inputWidth
                        << "inputHeight:" << media_config->inputHeight
                        << "codecWidth:" << media_config->codecWidth
                        << "codecHeight:" << media_config->codecHeight
                        << "frameRate:" << media_config->frameRate
                        << "bitRate:" << media_config->bitRate
                        << "recordType:" << media_config->recordType
                        << "update_config:" << update_config;

        LOG(INFO) << "====  config ======  " <<  params.str().c_str();
    }


    int nWidth = media_config->codecWidth;
    int nHeight = media_config->codecHeight;
    LOG(INFO) << "====  config ======  nWidth:" << nWidth << " nHeight:" << nHeight; 


    if(update_config){
        assert(enc);
        assert(initializeParams);
        if(initializeParams->encodeWidth != nWidth || initializeParams->encodeHeight != nHeight  ){
            LOG(INFO) << "====    ??????????????????:";
            initializeParams->encodeWidth = nWidth;
            initializeParams->encodeHeight = nHeight;

            NV_ENC_RECONFIGURE_PARAMS reconfigureParams = { NV_ENC_RECONFIGURE_PARAMS_VER };
            memcpy(&reconfigureParams.reInitEncodeParams, initializeParams, sizeof(NV_ENC_INITIALIZE_PARAMS));

            NV_ENC_CONFIG reInitCodecConfig = { NV_ENC_CONFIG_VER };
            memcpy(&reInitCodecConfig, initializeParams->encodeConfig, sizeof(NV_ENC_CONFIG));

            reconfigureParams.reInitEncodeParams.encodeConfig = &reInitCodecConfig;

            enc->Reconfigure(&reconfigureParams);
        } 
    }else{
        assert(initializeParams == NULL);

        #ifdef __linux
        //??????opengl ??????
        SetupGLXResourcesWithShareContext(opengl_config->majorGLXContext,opengl_config->dpy,opengl_config->fbconfig,nWidth,nHeight);
        //????????????
        preloadOpenglResource();
        #endif

        initializeParams = (NV_ENC_INITIALIZE_PARAMS*)malloc(sizeof(NV_ENC_INITIALIZE_PARAMS));
        *initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
        NV_ENC_CONFIG* encodeConfig = (NV_ENC_CONFIG*)malloc(sizeof(NV_ENC_CONFIG)); 
        *encodeConfig = { NV_ENC_CONFIG_VER };
        initializeParams->encodeConfig = encodeConfig;

        if(enc == NULL ){
            #ifdef __linux
                enc = new NvEncoderGL(nWidth, nHeight, NV_ENC_BUFFER_FORMAT_ABGR);
            #else

            ck(cuInit(0));
            int nGpu = 0;
            int iGpu = 0;
            ck(cuDeviceGetCount(&nGpu));
            if (iGpu < 0 || iGpu >= nGpu)
            {
                std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
                return ;
            }
            CUdevice cuDevice = 0;
            ck(cuDeviceGet(&cuDevice, iGpu));
            char szDeviceName[80];
            ck(cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice));
            std::cout << "GPU in use: " << szDeviceName << std::endl;
            CUcontext cuContext = NULL;
            ck(cuCtxCreate(&cuContext, 0, cuDevice));
            enc = new NvEncoderCuda(cuContext,nWidth, nHeight, NV_ENC_BUFFER_FORMAT_ABGR,0);


            #endif
        }

        //????????????
        media_config->recordType == 0 ? ConfigForRecording() : ConfigForLowLatency(nWidth,nHeight);
        enc->CreateEncoder(initializeParams);


        {
            //test output config
            std::stringstream params;
            params << "encodeWidth:" << initializeParams->encodeWidth
                            << "encodeHeight:" << initializeParams->encodeHeight
                            << "frameRateNum:" << initializeParams->frameRateNum
                            << "frameRateDen:" << initializeParams->frameRateDen;

            LOG(INFO) << "====  initializeParams ======  " <<  params.str().c_str();
        }
    }




    
}

void NVENCWrapper::preloadOpenglResource(){
    #ifdef __linux
    //??????FBO
    glGenFramebuffers(1,&fbo);
    // ???????????????
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // ????????????????????????????????????????????????
    GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, DrawBuffers); // "1" ????????????????????????


    // ?????????????????????
    static const GLfloat g_quad_vertex_buffer_data[] = { 
		-1.0f, -1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,
		 1.0f,  1.0f, 0.0f,
	};

    
    glGenBuffers(1, &quad_vertexbuffer);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vertexbuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_quad_vertex_buffer_data), g_quad_vertex_buffer_data, GL_STATIC_DRAW);


    // ????????????????????????
    std::string vertexShaderCode = VertexShaderCode;
    std::string fragmentShaderCode = FragmentShaderCode;
    // LOG(INFO) << "vertexShaderCode: "<<vertexShaderCode;
    // LOG(INFO) << "fragmentShaderCode: "<<fragmentShaderCode;
    quad_programID = LoadShaderStrings( vertexShaderCode, fragmentShaderCode );


    // quad_programID = LoadShaders("/home/fu/Downloads/Passthrough.vertexshader","/home/fu/Downloads/SimpleTexture.fragmentshader");
    // ??????????????????id
	textureSamplerID = glGetUniformLocation(quad_programID, "textureSampler");
    #endif
}

void NVENCWrapper::handleEncodeTexture() {
    #ifdef __linux
	const NvEncInputFrame* encoderInputFrame = enc->GetNextInputFrame();
    NV_ENC_INPUT_RESOURCE_OPENGL_TEX *pResource = (NV_ENC_INPUT_RESOURCE_OPENGL_TEX *)encoderInputFrame->inputPtr;


    // ???????????????
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // ??????????????????????????????
    glBindTexture(pResource->target, pResource->texture);
    // ?????????????????? attachement #0
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, pResource->target,pResource->texture, 0);
    glBindTexture(pResource->target, 0);


    // ??????
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return;

    //????????????
    glViewport(0,0,media_config->codecWidth,media_config->codecHeight);

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    // Use our shader
    glUseProgram(quad_programID);

    // ???????????????????????????
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vertexbuffer);
    glVertexAttribPointer(
        0,                  // attribute 0. No particular reason for 0, but must match the layout in the shader.
        3,                  // size
        GL_FLOAT,           // type
        GL_FALSE,           // normalized?
        0,                  // stride
        (void*)0            // array buffer offset
    );

    // ????????????????????????????????????
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);
    glUniform1i(textureSamplerID, 0);

    // ????????????
    glDrawArrays(GL_TRIANGLES, 0, 6); // 2*3 indices starting at 0 -> 2 triangles
    glDisableVertexAttribArray(0);

    // ??????FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // ????????????
    glBindTexture(GL_TEXTURE_2D,0);

    //??????
    glFinish();
    #else
    
    const NvEncInputFrame* encoderInputFrame = enc->GetNextInputFrame();
    NvEncoderCuda::CopyToDeviceFrame(enc->m_cuContext,
        pixelData,
        0, 
        (CUdeviceptr)encoderInputFrame->inputPtr,
        (int)encoderInputFrame->pitch,
        enc->GetEncodeWidth(),
        enc->GetEncodeHeight(),
        CU_MEMORYTYPE_HOST, 
        encoderInputFrame->bufferFormat,
        encoderInputFrame->chromaOffsets,
        encoderInputFrame->numChromaPlanes);

    #endif
}

void NVENCWrapper::handleCodecProcess() {
    // LOG(INFO) << "?????????ID:" << myID << "Normal ??????";
    std::vector<std::vector<uint8_t>> vPacket;
#ifdef __linux 
    enc->EncodeFrame(vPacket);
#else
    NV_ENC_PIC_PARAMS picParams = {NV_ENC_PIC_PARAMS_VER};
    picParams.encodePicFlags = 0;
    enc->EncodeFrame(vPacket,&picParams);
#endif
    nFrame += (int)vPacket.size();

    for (std::vector<uint8_t> &packet : vPacket)
    {
        uint8_t* videoData = packet.data();
        int count = packet.size();
        
        // if(DataCallBack){
        //     DataCallBack(videoData,count);
        // }
        PushDataFromMediaEncoder(videoData,count);

    }
}

void NVENCWrapper::handleCodecForceIDRProcess() {
    LOG(INFO) << "?????????ID:" << myID << "IDR ??????";
    std::vector<std::vector<uint8_t>> vPacket;

    NV_ENC_PIC_PARAMS picParams = { NV_ENC_PIC_PARAMS_VER };
    picParams.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
    enc->EncodeFrame(vPacket,&picParams);
    nFrame += (int)vPacket.size();

    for (std::vector<uint8_t> &packet : vPacket)
    {
        uint8_t* videoData = packet.data();
        int count = packet.size();
        
        // if(DataCallBack){
        //     DataCallBack(videoData,count);
        // }

        PushDataFromMediaEncoder(videoData,count);
    }
}

void NVENCWrapper::handleStopEncode() {
    if(enc != NULL ){
        LOG(INFO) << "?????????ID:" << myID << "?????????...";
        std::vector<std::vector<uint8_t>> vPacket;
        enc->EndEncode(vPacket);
        enc->DestroyEncoder();
        delete enc;
        enc = NULL;


        nFrame += (int)vPacket.size();

        // if(fpOut.is_open()){
        //     for (std::vector<uint8_t> &packet : vPacket)
        //     {
        //         fpOut.write(reinterpret_cast<char*>(packet.data()), packet.size());
        //     }
        //     fpOut.flush();
        //     fpOut.close();
        // }
        for (std::vector<uint8_t> &packet : vPacket)
        {
            PushDataFromMediaEncoder(packet.data(),packet.size());
        }

        LOG(INFO) << "?????????ID:" << myID << "????????????";
    }
}
//??????opengl?????? ????????????
void NVENCWrapper::handleReleaseResource(){
    #ifdef __linux
    // ?????? VBO ??? shader
	glDeleteBuffers(1, &quad_vertexbuffer);
	glDeleteProgram(quad_programID);

    //?????? FBO
    glDeleteFramebuffers(1, &fbo);
    //??????GLXDrawable ??? context
    glXDestroyPbuffer(glXGetCurrentDisplay(),glXGetCurrentDrawable());
    glXDestroyContext(glXGetCurrentDisplay(),glXGetCurrentContext());
    #endif
}

int NVENCWrapper::CoutSpanSize(int targetValue){
    int temp = 512;
    while (temp < targetValue)
    {
        temp = temp << 2;
    }
    if (temp < (targetValue * 1.2f))
    {
        temp = temp << 2;
    }
    
    // LOG(INFO) << "====    pushDataFromMediaEncoder ?????? ??????:" << targetValue << ",target : " << temp;
    return temp;
}