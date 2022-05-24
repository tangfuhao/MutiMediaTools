using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System;
using System.Runtime.InteropServices;
using AOT;
using UnityEngine.UI;
using System.Threading;

namespace MetaWorks{



    [RequireComponent(typeof(AudioSource))]
    public class MediaPlayer : MonoBehaviour
    {
        static Dictionary<IntPtr,MediaPlayer>  mediaplyersDic = new Dictionary<IntPtr, MediaPlayer>();


        [DllImport ("unity_plugin_mediaplayer")]
        private static extern void NativeSeekPlayerGetParams(IntPtr mediaplayer,IntPtr mediaInfo);
        
        [DllImport ("unity_plugin_mediaplayer")]
        private static extern IntPtr NativeCreateSeekPlayer(string file);

        [DllImport ("unity_plugin_mediaplayer")]
        private static extern void NativeSeekPlayerSeek(IntPtr mediaplayer,long ms);
        [DllImport ("unity_plugin_mediaplayer")]
        private static extern void NativeSeekPlayerStop(IntPtr mediaplayer);
        [DllImport ("unity_plugin_mediaplayer")]
        private static extern void NativeSeekPlayerSetTexture(IntPtr mediaplayer,IntPtr texture);


        [DllImport("unity_plugin_mediaplayer")]
        static extern IntPtr GetRenderEventFlushBackBufferFunc(int playerID,IntPtr mediaplayer);

        public delegate void SeekCompletedAction(MediaPlayer video);
        public event SeekCompletedAction seekCompleted;



        [StructLayoutAttribute(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
        struct MediaInfo
        {
            public int vw;
            public int vh;

            public int rotation_degress;

            public int mediaLength;

        };


        IntPtr mediaplayerPtr = IntPtr.Zero;

        Texture2D _videoTexture;
        RenderTexture _effectVideoTexture;




        // Queue<int> callbackMSGs = new Queue<int>();

        // int sampleSizePerCall ;
        // IntPtr pcmCache;
        // byte[] shortByteArray;

        // short[] audioShortSamples;

        RawImage rawImage;

        MediaInfo mediaInfo = new MediaInfo();

        [SerializeField]
        Material imageEffectMaterial;


        bool isAbleToPlay = false;


        bool isPlaying = false;
        string dataSource;

        long lastSeekPosition;
        long lastPosition ;

        bool isMediaDataUpdate = false;

        int isWaitFirstFrame ;

        public Int64 playerIDMask = 0;
        private int playerIndex;
        //支持60个视频同时创建
        static int IDIndex = 0;

        private void Awake() {
            playerIndex = ++IDIndex;
            playerIDMask = 1L << playerIndex ;
            playerIndex = (playerIndex << 1) &  (~0x1);
            
            // RegisterMessageCallback(OnMessageCallback); 
            rawImage = transform.gameObject.GetComponent<RawImage>();
            imageEffectMaterial = new Material(Shader.Find("Metaworks/ImageEffectShader"));

            // Prepare("/home/fuhao/Downloads/1639396910985.mp4");

            // Prepare("/home/fuhao/Documents/unity_test/cache/202111/resources/81b03dc93fc9d38f89751a8809745a3.mp4");
            // Prepare("/home/fuhao/Downloads/386757056-1-192.mp4");
            // Prepare("/home/fuhao/Downloads/videoplayback.mp4");
            // isAutoPlay  = true;
        }

        private void Update() {
            // HandleMessageCallBack();
            
            if(mediaplayerPtr != IntPtr.Zero && isMediaDataUpdate){
                GL.IssuePluginEvent(GetRenderEventFlushBackBufferFunc(playerIndex,mediaplayerPtr), playerIndex);
                Graphics.Blit(_videoTexture, _effectVideoTexture, imageEffectMaterial);

                if(isWaitFirstFrame-- > 0) return;

                 if(seekCompleted != null) seekCompleted(this);
                 isMediaDataUpdate = false;

                // if (isMediaDataUpdate && !isPlaying && seekCompleted != null) {
                //     seekCompleted(this);
                // }

                // if(!rawImage.enabled && isMediaDataUpdate){
                //     NativeMediaPlayerLog("视频视图显示");
                //     rawImage.enabled = true;
                // }
                
                // isMediaDataUpdate = false;


            }
        }




        private void OnDestroy() {
            // Debug.Log("呜呜呜呜   OnDestroy");
            if(mediaplayerPtr != IntPtr.Zero){
                mediaplyersDic.Remove(mediaplayerPtr);
                NativeSeekPlayerStop(mediaplayerPtr);
                mediaplayerPtr = IntPtr.Zero;
            }

            if(_effectVideoTexture != null) RenderTexture.ReleaseTemporary(_effectVideoTexture);
            if (_videoTexture != null) Destroy(_videoTexture);

        }


    

        void resetParams(){
            isAbleToPlay = false;
            dataSource = null;
            isPlaying = false; 
            lastSeekPosition = -1;
            lastPosition = -1;
            isMediaDataUpdate = false;
        }

        public void Prepare(string file){
            
            if(  string.IsNullOrEmpty(file)  ) return;
            if( !string.IsNullOrEmpty(dataSource) && !string.IsNullOrEmpty(file) && string.Equals(dataSource,file)) return;
            
            resetParams();
            Debug.LogFormat("视频准备:{0}",file);



            //释放之前的播放器handle
            if(mediaplayerPtr != IntPtr.Zero){
                mediaplyersDic.Remove(mediaplayerPtr);
                NativeSeekPlayerStop(mediaplayerPtr);
                mediaplayerPtr = IntPtr.Zero;
            }
            

            dataSource = file;
            if (_videoTexture != null) {
                Destroy(_videoTexture);
                _videoTexture = null;
            }


            mediaplayerPtr = NativeCreateSeekPlayer(file);
            mediaplyersDic[mediaplayerPtr] = this;




            //获取视频信息
            IntPtr ptr = Marshal.AllocHGlobal(Marshal.SizeOf(mediaInfo));
            Marshal.StructureToPtr(mediaInfo, ptr, false);
            NativeSeekPlayerGetParams(mediaplayerPtr,ptr);
            mediaInfo = (MediaInfo)Marshal.PtrToStructure(ptr, typeof(MediaInfo));
            Marshal.FreeHGlobal(ptr);



            //初始化纹理
            if(_effectVideoTexture != null) RenderTexture.ReleaseTemporary(_effectVideoTexture);
            if (_videoTexture != null) Destroy(_videoTexture);
            _videoTexture = new Texture2D(mediaInfo.vw, mediaInfo.vh, TextureFormat.BGRA32, false);
            _effectVideoTexture = RenderTexture.GetTemporary( mediaInfo.vw, mediaInfo.vh, 0, RenderTextureFormat.ARGB32 );




            //指定src纹理
            NativeSeekPlayerSetTexture(mediaplayerPtr,_videoTexture.GetNativeTexturePtr());
            //指定dst纹理
            rawImage.texture = _effectVideoTexture;

            //设置旋转shader
            imageEffectMaterial.SetFloat("_RotationRadian", 0.0174f * (float)mediaInfo.rotation_degress);



            //初始化完成后 seek下位置
            Seek(0);

            isWaitFirstFrame = 3;
            isAbleToPlay = true;
        }


        public void Play(){}
        public void Pause(){}



    


        public void Seek(long ms){
            if(mediaplayerPtr == IntPtr.Zero) return;
            lastSeekPosition = ms < 0 ? 0 : ms;
            if(!isAbleToPlay) return; 
            NativeSeekPlayerSeek(mediaplayerPtr,lastSeekPosition);
            isMediaDataUpdate = true;
        }

        public long GetPosition(){
            if(mediaplayerPtr == IntPtr.Zero) return -1;
            return lastSeekPosition ;
        }

        public long GetMediaLength(){
            if(mediaplayerPtr == IntPtr.Zero) return -1;
            return mediaInfo.mediaLength ;
        }

        //停止并释放
        public void Close(){
            string fileUrl = dataSource;
            resetParams();
            if(mediaplayerPtr == IntPtr.Zero) return;
            mediaplyersDic.Remove(mediaplayerPtr);
            NativeSeekPlayerStop(mediaplayerPtr);
            mediaplayerPtr = IntPtr.Zero;
            Debug.LogFormat("视频释放:{0}",fileUrl);

        }

    }

}

