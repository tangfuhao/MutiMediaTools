#pragma once
#include "MediaEncodeConfig.h"

#ifdef WIN32
    #define MEDIA_CODEC_API extern "C" __declspec(dllexport)
#else
    #define MEDIA_CODEC_API extern "C" 
#endif




MEDIA_CODEC_API void* CreateNVIDAVideoCodecNative();

MEDIA_CODEC_API bool PreLoadVideoCodecNative(void* obj,int sw,int sh,int dw,int dh,int framerate,int bitrate,const char* path);

MEDIA_CODEC_API bool EncodeTextureNative(void* obj,void* texture);

MEDIA_CODEC_API bool ConfigCodec(void* obj,MediaEncodeConfig* config );

MEDIA_CODEC_API int GetNalData(void* obj, void * nal_data);

MEDIA_CODEC_API bool StopAndReleaseVideoCodecNative(void* obj);
