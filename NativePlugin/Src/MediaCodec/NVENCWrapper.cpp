#include "NVENCWrapper.h"
#include "../Utils/NvEncoderCLIOptions.h"
#include "../Utils/NvCodecUtils.h"
#include "GraphicsUtils.h"
#include "RenderAPI.h"
#include "shader.hpp"
#include <pthread.h>
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

extern RenderAPI* s_CurrentAPI;

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

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
}


NVENCWrapper::NVENCWrapper(/* args */) {
    runing = false;
    enc = NULL;
    media_config = NULL;
    opengl_config = NULL;
    myID = ID_GEN++;

    
    pthread_mutex_init(&count_mutex, NULL);
    pthread_cond_init (&count_threshold_cv, NULL);
    pthread_mutex_init(&event_mutex, NULL);
    pthread_cond_init (&event_threshold_cv, NULL);
    pthread_mutex_init(&update_cache_mutex, NULL);
    

    pthread_mutex_lock(&count_mutex);
    /* 为了兼容性，使用属性指明线程可被连接 */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    //启动编码器线程
    int pthread_ret = pthread_create(&tids, &attr, ExcuteCodecThread, (void *)this);
    if (pthread_ret)
    {
        std::cout << "NVENCWrapper pthread_create error: error_code=" << pthread_ret << std::endl;
    }else{
        //等待启动
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
                LOG(INFO) << "编码器id:" << this->myID << "初始化";
                this->handlePreload(false);
                break;
            case NVENCWrapperParams::UPDATE_CONFIG:
                LOG(INFO) << "编码器id:" << this->myID << "更新参数";
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
                LOG(INFO) << "关闭编码器id:" << this->myID ;
                this->handleStopEncode();
                this->handleReleaseResource();
                LOG(INFO) << "关闭编码器id:" << this->myID << "完成";
                break;        
            default:
                break;
        }
    }
    pthread_mutex_unlock(&(this->event_mutex));
}

void NVENCWrapper::EncodeTexture(void* texture,bool forceIDR){
    sourceTex = (GLuint)(size_t)(texture);

    //发送事件
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
    //发送结束事件
    LOG(INFO) << "编码器ID:" << myID << "发送结束";
    pthread_mutex_lock(&(event_mutex));
    event_code = NVENCWrapperParams::CLOSE;
    runing = false;
    pthread_cond_signal(&(event_threshold_cv));
    pthread_mutex_unlock(&(event_mutex));  

    LOG(INFO) << "编码器ID:" << myID << "等待结束操作";
    //等待编码器结束
    pthread_join(tids,NULL);
    LOG(INFO) << "编码器ID:" << myID << "完成结束操作";
}

bool NVENCWrapper::UpdateConfig(MediaEncodeConfig* config){
    LOG(INFO) << "====    UpdateConfig  bitRate:  "<< config->bitRate;
    if(config == NULL) return false;
    if(s_CurrentAPI->openglConfig == NULL) return false;
    //更新配置
    if(this->media_config){
        memcpy(this->media_config,config,sizeof(MediaEncodeConfig));

        //发送事件
        pthread_mutex_lock(&(event_mutex));
        event_code = NVENCWrapperParams::UPDATE_CONFIG;
        pthread_cond_signal(&(event_threshold_cv));
        pthread_mutex_unlock(&(event_mutex));
    }else{
        void* temp = malloc(sizeof(MediaEncodeConfig));
        memcpy(temp,config,sizeof(MediaEncodeConfig));

        // LOG(INFO) << "add checck   1:" <<  &*(this->media_config) << " 2:" <<  &*config << " 3:" << temp;
        this->media_config = (MediaEncodeConfig*)temp;
        // LOG(INFO) << "add checck   2:" <<  &*(this->media_config) << " 2:" <<  &*config;
        this->opengl_config = (OpenGLConfig*)s_CurrentAPI->openglConfig;

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


        //发送事件
        pthread_mutex_lock(&(event_mutex));
        event_code = NVENCWrapperParams::INIT;
        pthread_cond_signal(&(event_threshold_cv));
        pthread_mutex_unlock(&(event_mutex));  
    }

    return true;
}

int NVENCWrapper::GetNalData(void* nal_data){
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
    LOG(INFO) << "====    pushDataFromMediaEncoder  start  count:"<<count;
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

    //扩容
    if(nalCacheData->cacheCurrentData == NULL ||  nalCacheData->cacaheDataLength < count ){
        free(nalCacheData->cacheCurrentData);
        nalCacheData->cacheCurrentData = NULL;
        nalCacheData->cacaheDataLength = CoutSpanSize(count);
        nalCacheData->cacheCurrentData = malloc(nalCacheData->cacaheDataLength);
    }

    nalCacheData->cacaheDataIndex = count;
    memcpy(nalCacheData->cacheCurrentData,data,nalCacheData->cacaheDataIndex);
    LOG(INFO) << "====    pushDataFromMediaEncoder  finish  ,nalCacheReadIndex:"<<nalCacheReadIndex<<",nalCahceWriteIndex:"<<nalCahceWriteIndex<<",cacaheDataIndex:"<<nalCacheData->cacaheDataIndex;
    pthread_mutex_unlock(&update_cache_mutex);
    

}

////////////////////////////////////////////////////////////////////////////////////////////////////////

void NVENCWrapper::handlePreload(bool update_config){
    // {
    //     //test output config
    //     LOG(INFO) << "add checck   3:" <<  &*(this->media_config) ;
    //     std::stringstream params;
    //     params << "inputWidth:" << media_config->inputWidth
    //                     << "inputHeight:" << media_config->inputHeight
    //                     << "codecWidth:" << media_config->codecWidth
    //                     << "codecHeight:" << media_config->codecHeight
    //                     << "frameRate:" << media_config->frameRate
    //                     << "bitRate:" << media_config->bitRate
    //                     << "recordType:" << media_config->recordType
    //                     << "update_config:" << update_config;

    //     LOG(INFO) << "====  config ======  " <<  params.str().c_str();
       
    // }


    NV_ENC_BUFFER_FORMAT eFormat = NV_ENC_BUFFER_FORMAT_ABGR;
    NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
    int nWidth = media_config->codecWidth;
    int nHeight = media_config->codecHeight;

    LOG(INFO) << "====  config ======  nWidth:" << nWidth << " nHeight:" << nHeight; 

    if(!update_config){
        //创建opengl 环境
        SetupGLXResourcesWithShareContext(opengl_config->majorGLXContext,opengl_config->dpy,opengl_config->fbconfig,nWidth,nHeight);
        //准备数据
        preloadOpenglResource();

        if(enc == NULL ){
            enc = new NvEncoderGL(nWidth, nHeight, eFormat);
        }

        //创建配置
        if (media_config->recordType == 0){
            std::stringstream params;
            params << " -profile main" 
                << " -bitrate " << media_config->bitRate 
                << " -fps " << media_config->frameRate 
                << " -bf 0"
                << " -vbvbufsize " << media_config->bitRate   * 4 * 10
                << " -lookahead 0";
            NvEncoderInitParam encodeCLIOptions = NvEncoderInitParam(params.str().c_str());
            NvEncoderInitParam *encodeCLIOptionsPtr = &encodeCLIOptions;

            NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };
            initializeParams.encodeConfig = &encodeConfig;
            enc->CreateDefaultEncoderParams(&initializeParams, encodeCLIOptionsPtr->GetEncodeGUID(),
                encodeCLIOptionsPtr->GetPresetGUID(),encodeCLIOptionsPtr->GetTuningInfo());
            encodeCLIOptionsPtr->SetInitParams(&initializeParams, eFormat);
            
            LOG(INFO) << "NativeOpenNVENC: CreateEncoder  ";
            enc->CreateEncoder(&initializeParams);
        }else{
            std::stringstream params;
            params << " -profile baseline" 
                << " -codec h264"
                << " -fps " << media_config->frameRate 
                << " -rc cbr"
                << " -tuninginfo lowlatency";
            NvEncoderInitParam encodeCLIOptions = NvEncoderInitParam(params.str().c_str(),NULL,true);
            NvEncoderInitParam *encodeCLIOptionsPtr = &encodeCLIOptions;
            NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };
            initializeParams.encodeConfig = &encodeConfig;
            enc->CreateDefaultEncoderParams(&initializeParams, encodeCLIOptionsPtr->GetEncodeGUID(), encodeCLIOptionsPtr->GetPresetGUID(),
                        encodeCLIOptionsPtr->GetTuningInfo() == NV_ENC_TUNING_INFO_LOW_LATENCY ? NV_ENC_TUNING_INFO_LOW_LATENCY : NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY);
            encodeConfig.gopLength = 60 * 60 * 2;
            encodeConfig.frameIntervalP = 1;
            if (encodeCLIOptionsPtr->IsCodecH264())
            {
                encodeConfig.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
            }
            else
            {
                encodeConfig.encodeCodecConfig.hevcConfig.idrPeriod = NVENC_INFINITE_GOPLENGTH;
            }

            encodeConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
            encodeConfig.rcParams.multiPass = NV_ENC_TWO_PASS_FULL_RESOLUTION;
            encodeConfig.rcParams.averageBitRate = (static_cast<unsigned int>(15.0f * nWidth * nHeight) / (1280 * 720)) * 100000;
            encodeConfig.rcParams.vbvBufferSize = (encodeConfig.rcParams.averageBitRate * initializeParams.frameRateDen / initializeParams.frameRateNum) * 5;
            encodeConfig.rcParams.maxBitRate = encodeConfig.rcParams.averageBitRate;
            encodeConfig.rcParams.vbvInitialDelay = encodeConfig.rcParams.vbvBufferSize;

            encodeCLIOptionsPtr->SetInitParams(&initializeParams, eFormat);
        }

        enc->CreateEncoder(&initializeParams);

    }else{
        //copy params
        assert(enc);

        NV_ENC_RECONFIGURE_PARAMS reconfigureParams = { NV_ENC_RECONFIGURE_PARAMS_VER };
        memcpy(&reconfigureParams.reInitEncodeParams, &initializeParams, sizeof(initializeParams));
        NV_ENC_CONFIG reInitCodecConfig = { NV_ENC_CONFIG_VER };
        memcpy(&reInitCodecConfig, initializeParams.encodeConfig, sizeof(reInitCodecConfig));
        reconfigureParams.reInitEncodeParams.encodeConfig = &reInitCodecConfig;
    }

    
}

void NVENCWrapper::preloadOpenglResource(){
    
    //创建FBO
    glGenFramebuffers(1,&fbo);
    // 绑定帧缓存
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // 指定片段着色器绘制到的颜色缓冲区
    GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, DrawBuffers); // "1" 指定缓存区的数量


    // 四边形顶点数据
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


    // 创建和编译着色器
    std::string vertexShaderCode = VertexShaderCode;
    std::string fragmentShaderCode = FragmentShaderCode;
    // LOG(INFO) << "vertexShaderCode: "<<vertexShaderCode;
    // LOG(INFO) << "fragmentShaderCode: "<<fragmentShaderCode;
    quad_programID = LoadShaderStrings( vertexShaderCode, fragmentShaderCode );


    // quad_programID = LoadShaders("/home/fu/Downloads/Passthrough.vertexshader","/home/fu/Downloads/SimpleTexture.fragmentshader");
    // 获取输入纹理id
	textureSamplerID = glGetUniformLocation(quad_programID, "textureSampler");

}

void NVENCWrapper::handleEncodeTexture() {
	const NvEncInputFrame* encoderInputFrame = enc->GetNextInputFrame();
    NV_ENC_INPUT_RESOURCE_OPENGL_TEX *pResource = (NV_ENC_INPUT_RESOURCE_OPENGL_TEX *)encoderInputFrame->inputPtr;


    // 绑定帧缓存
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // 绑定需要渲染到的纹理
    glBindTexture(pResource->target, pResource->texture);
    // 把纹理设置为 attachement #0
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, pResource->target,pResource->texture, 0);
    glBindTexture(pResource->target, 0);


    // 检查
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return;

    //指定视口
    glViewport(0,0,media_config->codecWidth,media_config->codecHeight);

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    // Use our shader
    glUseProgram(quad_programID);

    // 输入四边形顶点数据
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

    // 指定片段着色器使用的纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTex);
    glUniform1i(textureSamplerID, 0);

    // 绘制三角
    glDrawArrays(GL_TRIANGLES, 0, 6); // 2*3 indices starting at 0 -> 2 triangles
    glDisableVertexAttribArray(0);

    // 解绑FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // 解绑纹理
    glBindTexture(GL_TEXTURE_2D,0);

    //刷新
    glFinish();
}

void NVENCWrapper::handleCodecProcess() {
    std::vector<std::vector<uint8_t>> vPacket;
    enc->EncodeFrame(vPacket);
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
        LOG(INFO) << "编码器ID:" << myID << "结束中...";
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

        LOG(INFO) << "编码器ID:" << myID << "结束成功";
    }
}
//释放opengl资源 子线程中
void NVENCWrapper::handleReleaseResource(){
    // 清理 VBO 和 shader
	glDeleteBuffers(1, &quad_vertexbuffer);
	glDeleteProgram(quad_programID);

    //清理 FBO
    glDeleteFramebuffers(1, &fbo);
    //清理GLXDrawable 和 context
    glXDestroyPbuffer(glXGetCurrentDisplay(),glXGetCurrentDrawable());
    glXDestroyContext(glXGetCurrentDisplay(),glXGetCurrentContext());
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
    
    LOG(INFO) << "====    pushDataFromMediaEncoder 扩容 目标:" << targetValue << ",target : " << temp;
    return temp;
}