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


        [DllImport ("MediaTools")]
        static extern void NativeMeddiaPlayerGetParams(
            IntPtr mediaplayer,
            IntPtr mediaInfo
        );
        
        [DllImport ("MediaTools")]
        private static extern IntPtr NativeCreateMediaPlayer(string file,string player_params);

        [DllImport ("MediaTools")]
        private static extern int UnityWriteAudioCallback(IntPtr mediaplayer, IntPtr audioData,int length,int audioChannel);

        [DllImport ("MediaTools")]
        private static extern IntPtr NativeMediaPlayerSetTexture(IntPtr mediaplayer,IntPtr texture);

        [DllImport ("MediaTools")]
        private static extern void NativeMediaPlayerPlay(IntPtr mediaplayer);

        [DllImport ("MediaTools")]
        private static extern void NativeMediaPlayerStop(IntPtr mediaplayer);

        [DllImport ("MediaTools")]
        private static extern void NativeMediaPlayerPause(IntPtr mediaplayer);

        [DllImport ("MediaTools")]
        private static extern void NativeMediaPlayerSeek(IntPtr mediaplayer,long ms);

        [DllImport ("MediaTools")]
        private static extern long NativeMediaPlayerPosition(IntPtr mediaplayer);
        [DllImport ("MediaTools")]
        private static extern long NativeMediaPlayerLength(IntPtr mediaplayer);


        


        [DllImport("MediaTools", CallingConvention = CallingConvention.Cdecl)]
        static extern void RegisterMessageCallback(messageCallback cb);

        delegate void messageCallback(IntPtr request,int message);

         [MonoPInvokeCallback(typeof(messageCallback))]
        static void OnMessageCallback(IntPtr request, int message)
        {
            MediaPlayer owner = null;
            if(mediaplyersDic.ContainsKey(request)){
                owner = mediaplyersDic[request];
                owner.EnqueueMessageQueue(message);
            }else{
                Debug.LogWarning("mediaplyersDic不存在的key");
            }

        }

        
        [DllImport("MediaTools")]
        static extern IntPtr GetRenderEventFlushBackBufferFunc(int playerID,IntPtr mediaplayer);
        




        public float volume = 1.0f;



        public const int MSG_OPEN_DONE           = (('O' << 24) | ('P' << 16) | ('E' << 8) | ('N' << 0));
        public const int MSG_OPEN_FAILED         = (('F' << 24) | ('A' << 16) | ('I' << 8) | ('L' << 0));
        public const int MSG_PLAY_PROGRESS       = (('R' << 24) | ('U' << 16) | ('N' << 8) | (' ' << 0));
        public const int MSG_PLAY_COMPLETED      = (('E' << 24) | ('N' << 16) | ('D' << 8) | (' ' << 0));
        public const int MSG_IMAGE_RENDER  =  (('I' << 24) | ('M' << 16) | ('G' << 8) | ('S' << 0));



        [StructLayoutAttribute(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
        struct MediaInfo
        {
            public int vw;
            public int vh;

            public int rotation_degress;
        };


        IntPtr mediaplayerPtr = IntPtr.Zero;

        Texture2D _videoTexture;
        RenderTexture _effectVideoTexture;

        bool isAbleToPlay = false;
        bool isAutoPlay = false;

        bool isPlaying = false;
        string dataSource;


        Queue<int> callbackMSGs = new Queue<int>();

        int sampleSizePerCall ;
        IntPtr pcmCache;
        byte[] shortByteArray;

        short[] audioShortSamples;

        RawImage rawImage;

         private IEnumerator _updateVideoTextureEnum;

        MediaInfo mediaInfo = new MediaInfo();

        [SerializeField]
        Material imageEffectMaterial;


        long mediaLength = -1;
        long lastSeekPosition = -1;
        long lastPosition = -1;

        bool isMediaDataUpdate = false;



        private int playerIndex;
        static int IDIndex = 0;

        private void Awake() {
            playerIndex = ++IDIndex;
            playerIndex = (playerIndex << 1) |  0x1;


            RegisterMessageCallback(OnMessageCallback); 
            rawImage = transform.gameObject.GetComponent<RawImage>();
            imageEffectMaterial = new Material(Shader.Find("Metaworks/ImageEffectShader"));


            // Prepare("/home/fuhao/Downloads/1639396910985.mp4");

            // Prepare("/home/fuhao/Documents/unity_test/cache/202111/resources/81b03dc93fc9d38f89751a8809745a3.mp4");
            // Prepare("/home/fuhao/Downloads/386757056-1-192.mp4");
            // Prepare("/home/fuhao/Downloads/videoplayback.mp4");
            // isAutoPlay  = true;
        }

        private void Update() {
            HandleMessageCallBack();
            if(mediaplayerPtr != IntPtr.Zero && _effectVideoTexture){
                GL.IssuePluginEvent(GetRenderEventFlushBackBufferFunc(playerIndex,mediaplayerPtr), playerIndex);
                Graphics.Blit(_videoTexture, _effectVideoTexture, imageEffectMaterial);

                if(!rawImage.enabled && isMediaDataUpdate){
                    // Debug.Log("视频视图显示");
                    rawImage.enabled = true;
                }
            }
        }

        private void OnApplicationQuit() {
            // Debug.Log("呜呜呜呜   OnApplicationQuit");
        }

        private void OnApplicationPause(bool pauseStatus) {
            // Debug.LogFormat("呜呜呜呜  OnApplicationPause {0}",pauseStatus);
        }


        private void OnDisable() {
            // Debug.Log("呜呜呜呜   OnDisable");
            Pause();
        }



        private void OnDestroy() {
            // Debug.Log("呜呜呜呜   OnDestroy");
            if(mediaplayerPtr != IntPtr.Zero){
                mediaplyersDic.Remove(mediaplayerPtr);
                callbackMSGs.Clear();
                NativeMediaPlayerStop(mediaplayerPtr);
                mediaplayerPtr = IntPtr.Zero;
            }

            if(_effectVideoTexture != null) RenderTexture.ReleaseTemporary(_effectVideoTexture);
            if (_videoTexture != null) Destroy(_videoTexture);

            if(pcmCache != IntPtr.Zero){
                Marshal.FreeHGlobal(pcmCache);
                pcmCache = IntPtr.Zero;
            }
        }

        private void OnAudioFilterRead(float[] data, int nbChannels)
        {
            if(!isAbleToPlay) return; 

            int dataLength = sampleSizePerCall*2;
            //初始化缓存数据
            if(sampleSizePerCall == 0 || sampleSizePerCall != data.Length){
                sampleSizePerCall = data.Length;
                dataLength = sampleSizePerCall*2;


                if(pcmCache != IntPtr.Zero){
                    Marshal.FreeHGlobal(pcmCache);
                    pcmCache = IntPtr.Zero;
                }
                pcmCache  = Marshal.AllocHGlobal (dataLength);
                shortByteArray = new byte[dataLength];
                audioShortSamples = new short[sampleSizePerCall];
            }


            //0=有效 
            //1= 设备未初始化
            //2=没有新的数据
            //3=设备暂停
            //4=设备停止
            int getAudioFromMediaState = UnityWriteAudioCallback(mediaplayerPtr,pcmCache,dataLength,2);
            if(getAudioFromMediaState == 1) return;


            while(isPlaying && getAudioFromMediaState != 0){
                if(getAudioFromMediaState == 3 || getAudioFromMediaState == 4){
                    isPlaying = false;
                    break;
                }


                Thread.Sleep(100);
                getAudioFromMediaState = UnityWriteAudioCallback(mediaplayerPtr,pcmCache,dataLength,2); 
            }

            //获取short 音频数据
            if( isPlaying && getAudioFromMediaState == 0  ){
                Marshal.Copy(pcmCache, shortByteArray, 0, dataLength);
                Buffer.BlockCopy(shortByteArray, 0 , audioShortSamples, 0 , dataLength);

                //short 转换拷贝到 float
                for (var i = 0; i < sampleSizePerCall; i++)
                {
                    short sample = audioShortSamples[i];
                    data[i] = (float) sample / 32768.0f * volume; 
                }
            }
        }

        public void EnqueueMessageQueue(int msg){
            callbackMSGs.Enqueue(msg);
        }
        private void HandleMessageCallBack(){

            while (callbackMSGs.Count > 0)
            {
                int message = callbackMSGs.Dequeue();
                //遍历队列，处理消息
                switch (message)
                {
                    case MSG_OPEN_DONE:
                        CallBackPrepare(true);
                        break;
                    case MSG_OPEN_FAILED:
                        UnityEngine.Debug.Log("MSG_OPEN_FAILED");
                        CallBackPrepare(false);
                        break;
                    case MSG_PLAY_COMPLETED:
                        Debug.Log("播放完成！！！重新回到初始");
                        break;
                    case MSG_IMAGE_RENDER:
                        // Debug.Log("获取到渲染回调");
                        isMediaDataUpdate = true;
                        
                        break;
                    default:
                        break;
                }
            }
        }

    

        void resetParams(){
            isAbleToPlay = false;
            isAutoPlay = false;
            dataSource = null;
            isPlaying = false; 
            lastSeekPosition = -1;
            lastPosition = -1;
            mediaLength = -1;
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
                callbackMSGs.Clear();
                NativeMediaPlayerStop(mediaplayerPtr);
                mediaplayerPtr = IntPtr.Zero;
            }
            

            dataSource = file;
            if (_videoTexture != null) {
                Destroy(_videoTexture);
                _videoTexture = null;
            }

            string player_params = "";
            mediaplayerPtr = NativeCreateMediaPlayer(file,player_params);
            mediaplyersDic[mediaplayerPtr] = this;

            rawImage.enabled = false;
            
        }


        public void Play(){
            if(mediaplayerPtr == IntPtr.Zero) return;
            isAutoPlay = true;
            if(isPlaying || !isAbleToPlay){
                return;
            } 
            isPlaying = true;
            NativeMediaPlayerPlay(mediaplayerPtr);
        }



    
        public void Pause(){
            if(mediaplayerPtr == IntPtr.Zero) return;
            isAutoPlay = false;
            if(!isPlaying || !isAbleToPlay){
                return;
            } 
            // Debug.Log("暂停 暂停！！！！");
            isPlaying = false;
            NativeMediaPlayerPause(mediaplayerPtr);
        }


        public void Seek(long ms){
            if(mediaplayerPtr == IntPtr.Zero) return;
            lastSeekPosition = ms < 0 ? 0 : ms;
            if(!isAbleToPlay) return; 
            NativeMediaPlayerSeek(mediaplayerPtr,ms);
        }

        public long GetPosition(){
            if(mediaplayerPtr == IntPtr.Zero) return -1;
            lastPosition =  NativeMediaPlayerPosition(mediaplayerPtr);
            return lastPosition ;
        }

        public long GetMediaLength(){
            if(mediaplayerPtr == IntPtr.Zero) return -1;
            return mediaLength ;
        }

        //停止并释放
        public void Close(){
            resetParams();

            if(mediaplayerPtr == IntPtr.Zero) return;
            callbackMSGs.Clear();
            mediaplyersDic.Remove(mediaplayerPtr);
            NativeMediaPlayerStop(mediaplayerPtr);
            mediaplayerPtr = IntPtr.Zero;
        }


        




        //////////////////////// callback
    // static  int get_order(int size)
    // {
    //     int order;
    //     size = (size - 1) >> (0);
    //     order = -1;
    //     do {
    //         size >>= 1;
    //         order++;
    //     } while (size != 0);
    //     return order;
    // }
        void CallBackPrepare(bool ret){
            isAbleToPlay = ret;
            if(isAbleToPlay){
                

                //获取视频信息
                IntPtr ptr = Marshal.AllocHGlobal(Marshal.SizeOf(mediaInfo));
                Marshal.StructureToPtr(mediaInfo, ptr, false);
                NativeMeddiaPlayerGetParams(mediaplayerPtr,ptr);
                mediaInfo = (MediaInfo)Marshal.PtrToStructure(ptr, typeof(MediaInfo));
                Marshal.FreeHGlobal(ptr);



                //初始化纹理
                if(_effectVideoTexture != null) RenderTexture.ReleaseTemporary(_effectVideoTexture);
                if (_videoTexture != null) Destroy(_videoTexture);
                if (mediaInfo.vw != 0 &&  mediaInfo.vh != 0)
                {
                    _videoTexture = new Texture2D(mediaInfo.vw, mediaInfo.vh, TextureFormat.BGRA32, false);
                    _effectVideoTexture = RenderTexture.GetTemporary( mediaInfo.vw, mediaInfo.vh, 0, RenderTextureFormat.ARGB32 );
                    //指定src纹理
                    NativeMediaPlayerSetTexture(mediaplayerPtr,_videoTexture.GetNativeTexturePtr());
                    //指定dst纹理
                    rawImage.texture = _effectVideoTexture;
                    //设置旋转shader
                    imageEffectMaterial.SetFloat("_RotationRadian", 0.0174f * (float)mediaInfo.rotation_degress);
                }
                //获取视频长度
                mediaLength = NativeMediaPlayerLength(mediaplayerPtr);
                
                //初始化完成后 seek下位置
                if(lastSeekPosition >= 0 ){
                    Seek(lastSeekPosition);
                }

                if(isAutoPlay) Play();
            }
        }

    }

}

