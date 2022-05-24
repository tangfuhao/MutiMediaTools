using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System;
using System.Runtime.InteropServices;

namespace FFMediaTool{



    public class NvEncWrapper 
    {
        [DllImport("MediaTools")]
        private static extern IntPtr CreateNVIDAVideoCodecNative();
        [DllImport("MediaTools")]
        private static extern bool ConfigCodec(IntPtr obj,IntPtr config);
        [DllImport("MediaTools")]
        private static extern bool EncodeTextureNative(IntPtr obj,IntPtr texture);
        [DllImport("MediaTools")]
        private static extern bool StopAndReleaseVideoCodecNative(IntPtr obj);
        [DllImport("MediaTools")]
        private static extern int GetNalData(IntPtr obj, IntPtr nal_data);
        IntPtr nativeObjPtr = IntPtr.Zero;


        ////运行编码器
        public void LaunchCodec(VideoRecordConfig config){
            if(nativeObjPtr == IntPtr.Zero){
                nativeObjPtr = CreateNVIDAVideoCodecNative();
            }
            VideoRecordConfig.MediaEncodeConfig codecConfig = config.BuildForNative(0);
            IntPtr configPtr = Marshal.AllocHGlobal(Marshal.SizeOf(codecConfig));
            Marshal.StructureToPtr(codecConfig, configPtr, false);
            ConfigCodec(nativeObjPtr,configPtr);
            Marshal.FreeHGlobal(configPtr);
        }

        public void EncodeTexture(IntPtr texture){
            if(nativeObjPtr != IntPtr.Zero){
                EncodeTextureNative(nativeObjPtr,texture);
            }
        }

        public void StopCodec(){
            if(nativeObjPtr != IntPtr.Zero){
                StopAndReleaseVideoCodecNative(nativeObjPtr);
            }
            nativeObjPtr =  IntPtr.Zero;
        }

        public int GetNalDataFromCodec(IntPtr data){
            return GetNalData(nativeObjPtr,data);
        }
    }

}

