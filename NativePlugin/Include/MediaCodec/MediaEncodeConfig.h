#pragma once
//视频编码的配置
typedef struct {
    int inputWidth;
    int inputHeight;
    int codecWidth;
    int codecHeight;
    int frameRate;
    int bitRate;
    //0 for record,1 for steaming
    int recordType;
} MediaEncodeConfig;


