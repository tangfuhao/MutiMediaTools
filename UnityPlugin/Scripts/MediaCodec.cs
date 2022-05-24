using System.Collections;
using System.Collections.Generic;
using System.Threading;
using UnityEngine;
using System;
using System.IO;
using Unity.Collections;

namespace FFMediaTool{
    public class MediaCodec 
    {
        //英伟达编码器
        NvEncWrapper nvEncWrapper;
        int encodeWidth;
        int encodeHeight;
        Material mat;

        byte[] nalDataBuffer;
        IntPtr nalNativePtr;

        //初始化
        public void UpdateConfig(VideoRecordConfig config){
            encodeWidth = config.EncodeWidth;
            encodeHeight = config.EncodeHeight;

            int calBufferSize = 8192;
            {
                int targetValue = config.BitRate / config.FrameRate;
                while (calBufferSize < targetValue)
                {
                    calBufferSize = calBufferSize << 1;
                }
                if(calBufferSize > (targetValue * 1.8)){
                    calBufferSize = calBufferSize >> 1;
                }
            }
            Debug.Log("初始化缓存大小 ： "+ calBufferSize);
            nalDataBuffer = new byte[calBufferSize];
            unsafe
            {
                fixed (byte* p = nalDataBuffer)
                {
                    nalNativePtr = (IntPtr)p;
                }
            }

            //剪切配置
            if(config.InputTextureWidth != encodeWidth || config.InputTextureHeight != encodeHeight){
                mat=new Material(Shader.Find("AvatarWorks/CutVideoFrame"));
                float scale_x = (float)encodeWidth / (float)(config.InputTextureWidth);
                float offset_x = (float)(config.offset_x) / (float)(config.InputTextureWidth);

                float scale_y = (float)encodeHeight / (float)(config.InputTextureHeight);
                float offset_y = (float)(config.offset_y) / (float)(config.InputTextureHeight);

                mat.SetFloat("_UVX_OFFSET", offset_x);
                mat.SetFloat("_UVX_SCALE", scale_x);

                mat.SetFloat("_UVY_OFFSET", offset_y);
                mat.SetFloat("_UVY_SCALE", scale_y);
            }


            if(nvEncWrapper == null){
                //获取当前OpenGL上下文的配置 配置编码器
                nvEncWrapper = new NvEncWrapper();
            }
            nvEncWrapper.LaunchCodec(config);

        }


        //停止编码
        public void StopCodec(){
            if(nvEncWrapper == null) return;
            nvEncWrapper.StopCodec();
            nvEncWrapper = null;
        }


        //编码一帧
        public void EncodeTexture(RenderTexture texture){
            if(nvEncWrapper == null) return;

            if(mat != null){
                //剪切
                RenderTexture tempTexture = RenderTexture.GetTemporary(encodeWidth, encodeHeight);
                Graphics.Blit(texture,tempTexture,mat);
                nvEncWrapper.EncodeTexture(tempTexture.GetNativeTexturePtr());
                RenderTexture.ReleaseTemporary(tempTexture);
            }else{
                nvEncWrapper.EncodeTexture(texture.GetNativeTexturePtr());
            }
        }

        public byte[] GetNalData(out int size){
            size = 0;
            if(nvEncWrapper == null) return null;
            int nalUnitSize = nvEncWrapper.GetNalDataFromCodec(nalNativePtr);
            if(nalUnitSize > 0){
                size = nalUnitSize;
                return nalDataBuffer;
            }

            return null;
        }

        










    }
}