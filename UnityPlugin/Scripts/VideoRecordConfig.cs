using System.Collections;
using System.Collections.Generic;
using UnityEngine;


namespace FFMediaTool
{
    public class VideoRecordConfig
    {
        public struct MediaEncodeConfig
            {
                int inputWidth;
                int inputHeight;
                int codecWidth;
                int codecHeight;
                int frameRate;
                int bitRate;
                //0 for record,1 for steaming
                int recordType;

                public MediaEncodeConfig(int iw,int ih,int ow,int oh,int framerate,int bitrate,int recordtype){
                    inputWidth = iw;
                    inputHeight = ih;
                    codecWidth = ow;
                    codecHeight = oh;
                    frameRate = framerate;
                    bitRate = bitrate;
                    recordType = recordtype;
                }
            }
        //输入纹理的宽
        public int InputTextureWidth = 0;
        //输入纹理的高
        public int InputTextureHeight = 0;
        //编码的宽
        public int EncodeWidth = 0;
        //编码的高
        public int EncodeHeight = 0;

        //保存文件路径
        public string SaveFilePath = null;

        public int offset_x;
        public int offset_y;
        //码率
        public int BitRate = 0;

        public int FrameRate = 30;

        public RenderTexture InputTexture;

        public bool isLowLatency = false;
        


        public class Builder{
            VideoRecordConfig temp = null;
            public Builder(){
                temp = new VideoRecordConfig();
            }

            


            

            public Builder InputWidthAndHeight(int w,int h){
                temp.InputTextureWidth = w;
                temp.InputTextureHeight = h;
                return this;
            }

            public Builder OutputWidthAndHeight(int w,int h){
                temp.EncodeWidth = w;
                temp.EncodeHeight = h;
                return this;
            }

            public Builder BitRate(int Bitrate){
                temp.BitRate = Bitrate;
                return this;
            }

            public Builder FrameRate(int frameRate){
                temp.FrameRate = frameRate;
                return this;
            }

            public Builder SaveFilePath(string path){
                temp.SaveFilePath = path;
                return this;
            }

            public Builder OutputPositionOffset(int x,int y){
                    temp.offset_x = x;
                    temp.offset_y = y;
                    return this;
            }

            public Builder InputTexture(RenderTexture texture){
                temp.InputTexture = texture;
                return this;
            }

            public Builder LowLatency(bool islowlatency){
                temp.isLowLatency = islowlatency;
                return this;
            }


            public VideoRecordConfig Build(){
                //Y轴反转
                temp.offset_y  = temp.InputTextureHeight - temp.EncodeHeight - temp.offset_y;
                return temp;
            }
        }


        public MediaEncodeConfig BuildForNative(){
            return new MediaEncodeConfig(InputTextureWidth,InputTextureHeight,EncodeWidth,EncodeHeight,FrameRate,BitRate, isLowLatency? 1 :0);
        }

    }
}