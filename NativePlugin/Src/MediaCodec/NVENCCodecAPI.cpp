#include "NVENCCodecAPI.h"
#include "NVENCWrapper.h"
#include "MediaEncodeConfig.h"




MEDIA_CODEC_API void* CreateNVIDAVideoCodecNative(){
    return new NVENCWrapper();
}

MEDIA_CODEC_API bool EncodeTextureNative(void* obj,void* texture){
    glFinish();
    // //拷贝到编码器的input
    NVENCWrapper* dstObj = (NVENCWrapper*)obj;
    if(!dstObj) return false;
    dstObj->EncodeTexture(texture);
    return true;
}

MEDIA_CODEC_API bool StopAndReleaseVideoCodecNative(void* obj){
    NVENCWrapper *dstObj = (NVENCWrapper *)obj;
    if(!dstObj) return false;
    dstObj->StopEncode();
    delete dstObj;
    return true;
}

MEDIA_CODEC_API bool ConfigCodec(void* obj,MediaEncodeConfig* config ){
    //用主线程的opengl context作为共享context 创建编码器的opengl环境
    NVENCWrapper* dstObj = (NVENCWrapper*)obj;
    if(!dstObj) return false;
    return dstObj->UpdateConfig(config);
}

MEDIA_CODEC_API int GetNalData(void* obj, void * nal_data){
    //用主线程的opengl context作为共享context 创建编码器的opengl环境
    NVENCWrapper* dstObj = (NVENCWrapper*)obj;
    if(!dstObj) return false;
    return dstObj->GetNalData(nal_data);
}