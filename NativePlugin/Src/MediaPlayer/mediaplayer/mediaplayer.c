// 包含头文件
#include <pthread.h>
#include "pktqueue.h"
#include "ffrender.h"


#include "mediaplayer.h"
#include "vdev.h"
#include "keyframelist.h"



#include "libavutil/time.h"

#include "libavdevice/avdevice.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libavutil/display.h"
#include "logger.h"
#include "unity_plugin_mediaplayer.h"


// 内部类型定义
typedef struct {
    // format
    AVFormatContext *avformat_context;

    // audio
    AVCodecContext  *acodec_context;
    int              astream_index;
    AVRational       astream_timebase;
    AVFrame          aframe;

    // video
    AVCodecContext  *vcodec_context;
    int              vstream_index;
    AVRational       vstream_timebase;
    AVFrame          vframe;

    void            *pktqueue; // pktqueue
    void            *render;   // render


    // thread
    #define PS_A_PAUSE    (1 << 0)  // audio decoding pause
    #define PS_V_PAUSE    (1 << 1)  // video decoding pause
    #define PS_R_PAUSE    (1 << 2)  // rendering pause
    #define PS_F_SEEK     (1 << 3)  // seek flag
    #define PS_A_SEEK     (1 << 4)  // seek audio
    #define PS_V_SEEK     (1 << 5)  // seek video
    #define PS_CLOSE      (1 << 6)  // close player


    int              status;
    int              seek_req ;
    int64_t          seek_pos ;
    int64_t          seek_dest;
    int64_t          seek_vpts;
    int              seek_diff;
    int              seek_sidx;

    // player common vars
    CMNVARS          cmnvars;

    pthread_t        avdemux_thread;
    pthread_t        adecode_thread;
    pthread_t        vdecode_thread;

    AVFilterGraph   *vfilter_graph;
    AVFilterContext *vfilter_src_ctx;
    AVFilterContext *vfilter_sink_ctx;

    // player init timeout, and init params
    int64_t            read_timelast;
    int64_t            read_timeout;
    PLAYER_INIT_PARAMS init_params;

    // save url
    char  url[PATH_MAX];

    pthread_mutex_t seek_lock;
    pthread_rwlock_t seek_ops_lock;

    KEYFRAMELIST keyframlist;
} PLAYER;

// 内部常量定义
static const AVRational TIMEBASE_MS = { 1, 1000 };

// 内部函数实现
static void avlog_callback(void* ptr, int level, const char *fmt, va_list vl) {
    DO_USE_VAR(ptr);
    if (level <= av_log_get_level()) {
        log_print(fmt,vl);
    }
}

static int interrupt_callback(void *param)
{
    PLAYER *player = (PLAYER*)param;
    if (player->read_timeout == -1) return 0;
    return av_gettime_relative() - player->read_timelast > player->read_timeout ? AVERROR_EOF : 0;
}

//++ for filter graph
static void vfilter_graph_init(PLAYER *player)
{
    const AVFilter    *filter_src  = avfilter_get_by_name("buffer"    );
    const AVFilter    *filter_sink = avfilter_get_by_name("buffersink");
    AVCodecContext    *vdec_ctx    = player->vcodec_context;
    int                pixfmts[]   = { vdec_ctx ? vdec_ctx->pix_fmt : AV_PIX_FMT_NONE, AV_PIX_FMT_NONE };
    AVBufferSinkParams params      = { (enum AVPixelFormat*) pixfmts };
    AVFilterInOut     *inputs, *outputs;
    char               temp[256], fstr[256];
    int                ret;
    if (!player->vcodec_context) return;

    AVStream *st = player->avformat_context->streams[player->vstream_index];
    AVPacketSideData *side_data = st->side_data;
    int nb_side_data =  st->nb_side_data;
    // log_print("nb_side_data : %d",nb_side_data);
    for (int i = 0; i < nb_side_data; i++) {
        const AVPacketSideData *sd = &side_data[i];
        if (sd->type == AV_PKT_DATA_DISPLAYMATRIX && sd->size >= 9*sizeof(int32_t)) {
            double angle = av_display_rotation_get((int32_t *)sd->data);
            player->init_params.video_rotate = (int)angle;
            // log_print("角度是：%f,  %d",angle,player->init_params.video_rotate);
            break;
        }
    }

    return;
    //++ check if no filter used
    if (  !player->init_params.video_deinterlace
       && !player->init_params.video_rotate
       && !player->init_params.filter_string[0] ) {
            return;
    }
    //-- check if no filter used

    player->vfilter_graph = avfilter_graph_alloc();
    if (!player->vfilter_graph) return;

    //++ create in & out filter
    sprintf(temp, "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            vdec_ctx->width, vdec_ctx->height, vdec_ctx->pix_fmt,
            vdec_ctx->time_base.num, vdec_ctx->time_base.den,
            vdec_ctx->sample_aspect_ratio.num, vdec_ctx->sample_aspect_ratio.den);

    avfilter_graph_create_filter(&player->vfilter_src_ctx , filter_src , "in" , temp, NULL   , player->vfilter_graph);
    avfilter_graph_create_filter(&player->vfilter_sink_ctx, filter_sink, "out", NULL, &params, player->vfilter_graph);
    //-- create in & out filter

    //++ generate filter string according to deinterlace and rotation
    if (player->init_params.video_rotate) {
        int ow = abs((int)(vdec_ctx->width  * cos(player->init_params.video_rotate * M_PI / 180)))
               + abs((int)(vdec_ctx->height * sin(player->init_params.video_rotate * M_PI / 180)));
        int oh = abs((int)(vdec_ctx->width  * sin(player->init_params.video_rotate * M_PI / 180)))
               + abs((int)(vdec_ctx->height * cos(player->init_params.video_rotate * M_PI / 180)));
        player->init_params.video_owidth  = ow;
        player->init_params.video_oheight = oh;
        sprintf(temp, "rotate=%d*PI/180:%d:%d", player->init_params.video_rotate, ow, oh);
    }
    strcpy(fstr, player->init_params.video_deinterlace ? "yadif=0:-1:1" : "");
    strcat(fstr, player->init_params.video_deinterlace ? "[a];[a]" : "");
    strcat(fstr, player->init_params.video_rotate ? temp : "");
    strcat(fstr, player->init_params.video_rotate? ",vflip":"vflip");
    
    //-- generate filter string according to deinterlace and rotation

    inputs  = avfilter_inout_alloc();
    outputs = avfilter_inout_alloc();
    inputs->name        = av_strdup("out");
    inputs->filter_ctx  = player->vfilter_sink_ctx;
    inputs->pad_idx     = 0;
    inputs->next        = NULL;
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = player->vfilter_src_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    log_print("fstr:%s",fstr);
    ret = avfilter_graph_parse_ptr(player->vfilter_graph, player->init_params.filter_string[0] ? player->init_params.filter_string : fstr, &inputs, &outputs, NULL);
    avfilter_inout_free(&inputs );
    avfilter_inout_free(&outputs);
    if (ret < 0) {
        av_log(NULL, AV_LOG_WARNING, "avfilter_graph_parse_ptr failed !\n");
        goto failed;
    }

    // config filter graph
    ret = avfilter_graph_config(player->vfilter_graph, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_WARNING, "avfilter_graph_config failed !\n");
        goto failed;
    }

failed:
    if (ret < 0) {
        avfilter_graph_free(&player->vfilter_graph);
        player->vfilter_graph    = NULL;
        player->vfilter_src_ctx  = NULL;
        player->vfilter_sink_ctx = NULL;
    }
}

static void vfilter_graph_free(PLAYER *player)
{
    if (!player->vfilter_graph) return;
    avfilter_graph_free(&player->vfilter_graph);
    player->vfilter_graph    = NULL;
    player->vfilter_src_ctx  = NULL;
    player->vfilter_sink_ctx = NULL;
}

static void vfilter_graph_input(PLAYER *player, AVFrame *frame)
{
    if (player->vfilter_graph) {
        int ret = av_buffersrc_add_frame(player->vfilter_src_ctx, frame);
        if (ret != 0) {
            av_log(NULL, AV_LOG_WARNING, "av_buffersrc_add_frame_flags failed !\n");
        }
    }
}

static int vfilter_graph_output(PLAYER *player, AVFrame *frame)
{
    return player->vfilter_graph ? av_buffersink_get_frame(player->vfilter_sink_ctx, frame) : 0;
}
//-- for filter graph

static int init_stream(PLAYER *player, enum AVMediaType type, int sel) {
    AVCodec *decoder = NULL;
    int     idx = -1, cur = -1, i;

    if (sel == -1) return -1;
    for (i=0; i<(int)player->avformat_context->nb_streams; i++) {
        if (player->avformat_context->streams[i]->codec->codec_type == type) {
            idx = i; if (++cur == sel) break;
        }
    }
    if (idx == -1) return -1;

    switch (type) {
    case AVMEDIA_TYPE_AUDIO:
        // get new acodec_context & astream_timebase
        player->acodec_context   = player->avformat_context->streams[idx]->codec;
        player->astream_timebase = player->avformat_context->streams[idx]->time_base;

        // reopen codec
        decoder = avcodec_find_decoder(player->acodec_context->codec_id);
        if (decoder && avcodec_open2(player->acodec_context, decoder, NULL) == 0) {
            player->astream_index = idx;
        } else {
            av_log(NULL, AV_LOG_WARNING, "failed to find or open decoder for audio !\n");
        }
        break;

    case AVMEDIA_TYPE_VIDEO:
        // get new vcodec_context & vstream_timebase
        player->vcodec_context   = player->avformat_context->streams[idx]->codec;
        player->vstream_timebase = player->avformat_context->streams[idx]->time_base;

        //++ open codec
        //+ try nvidia hardware decoder
        if (player->init_params.video_hwaccel) {
            switch (player->vcodec_context->codec_id) {
            case AV_CODEC_ID_H264      : decoder = avcodec_find_decoder_by_name("h264_cuvid" ); break;
            case AV_CODEC_ID_HEVC      : decoder = avcodec_find_decoder_by_name("hevc_cuvid" ); break;
            case AV_CODEC_ID_VP8       : decoder = avcodec_find_decoder_by_name("vp8_cuvid"  ); break;
            case AV_CODEC_ID_VP9       : decoder = avcodec_find_decoder_by_name("vp9_cuvid"  ); break;
            case AV_CODEC_ID_MPEG1VIDEO     : decoder = avcodec_find_decoder_by_name("mpeg1_cuvid"); break;
            case AV_CODEC_ID_MPEG2VIDEO: decoder = avcodec_find_decoder_by_name("mpeg2_cuvid"); break;
            case AV_CODEC_ID_MPEG4     : decoder = avcodec_find_decoder_by_name("mpeg4_cuvid"); break;
            case AV_CODEC_ID_VC1     : decoder = avcodec_find_decoder_by_name("vc1_cuvid"); break;

            default: break;
            }
            if (decoder && avcodec_open2(player->vcodec_context, decoder, NULL) == 0) {
                player->vstream_index = idx;
                av_log(NULL, AV_LOG_WARNING, "using android mediacodec hardware decoder %s !\n", decoder->name);
            } else {
                avcodec_close(player->vcodec_context);
                decoder = NULL;
            }
            player->init_params.video_hwaccel = decoder ? 1 : 0;
        }
        //- try android mediacodec hardware decoder

        if (!decoder) {
            //+ try to set video decoding thread count
            if (player->init_params.video_thread_count > 0) {
                player->vcodec_context->thread_count = player->init_params.video_thread_count;
            }
            //- try to set video decoding thread count
            decoder = avcodec_find_decoder(player->vcodec_context->codec_id);
            if (decoder && avcodec_open2(player->vcodec_context, decoder, NULL) == 0) {
                player->vstream_index = idx;
            } else {
                av_log(NULL, AV_LOG_WARNING, "failed to find or open decoder for video !\n");
            }
            // get the actual video decoding thread count
            player->init_params.video_thread_count = player->vcodec_context->thread_count;
        }
        //-- open codec
        break;

    case AVMEDIA_TYPE_SUBTITLE:
        return -1; // todo...
    default:
        return -1;
    }

    return 0;
}

static int get_stream_total(PLAYER *player, enum AVMediaType type) {
    int total, i;
    for (i=0,total=0; i<(int)player->avformat_context->nb_streams; i++) {
        if (player->avformat_context->streams[i]->codec->codec_type == type) {
            total++;
        }
    }
    return total;
}

#if 0
static int get_stream_current(PLAYER *player, enum AVMediaType type) {
    int idx, cur, i;
    switch (type) {
    case AVMEDIA_TYPE_AUDIO   : idx = player->astream_index; break;
    case AVMEDIA_TYPE_VIDEO   : idx = player->vstream_index; break;
    case AVMEDIA_TYPE_SUBTITLE: return -1; // todo...
    default: return -1;
    }
    for (i=0,cur=-1; i<(int)player->avformat_context->nb_streams && i!=idx; i++) {
        if (player->avformat_context->streams[i]->codec->codec_type == type) {
            cur++;
        }
    }
    return cur;
}
#endif

static int player_prepare(PLAYER *player)
{
    //++ for avdevice
    #define AVDEV_DSHOW   "dshow"
    #define AVDEV_GDIGRAB "gdigrab"
    #define AVDEV_VFWCAP  "vfwcap"
    char          *url    = player->url;
    AVInputFormat *fmt    = NULL;
    //-- for avdevice

    AVRational    vrate   = { 20, 1 };
    AVDictionary *opts    = NULL;
    int           ret     = -1;

    //++ for avdevice
    if (strstr(player->url, AVDEV_DSHOW) == player->url) {
        fmt = av_find_input_format(AVDEV_DSHOW);
        url = player->url + strlen(AVDEV_DSHOW) + 3;
    } else if (strstr(player->url, AVDEV_GDIGRAB) == player->url) {
        fmt = av_find_input_format(AVDEV_GDIGRAB);
        url = player->url + strlen(AVDEV_GDIGRAB) + 3;
    } else if (strstr(player->url, AVDEV_VFWCAP) == player->url) {
        fmt = av_find_input_format(AVDEV_VFWCAP);
        url = player->url + strlen(AVDEV_VFWCAP) + 3;
    }
    //-- for avdevice

    // open input file
    if (  strstr(player->url, "rtsp://" ) == player->url
       || strstr(player->url, "rtmp://" ) == player->url
       || strstr(player->url, "dshow://") == player->url) {
        if (player->init_params.rtsp_transport) {
            av_dict_set(&opts, "rtsp_transport", player->init_params.rtsp_transport == 1 ? "udp" : "tcp", 0);
        }
        av_dict_set(&opts, "buffer_size"    , "1048576", 0);
        av_dict_set(&opts, "fpsprobesize"   , "2"      , 0);
        av_dict_set(&opts, "analyzeduration", "5000000", 0);
        if (player->init_params.avts_syncmode == AVSYNC_MODE_AUTO) {
            player->init_params.avts_syncmode = memcmp(player->url, "rtmp://", 7) == 0 ? AVSYNC_MODE_LIVE_SYNC1 : AVSYNC_MODE_LIVE_SYNC0;
        }
    } else {
        player->init_params.init_timeout   = 0;
        player->init_params.auto_reconnect = 0;
        player->init_params.avts_syncmode  = AVSYNC_MODE_FILE;
    }
    if (player->init_params.video_vwidth != 0 && player->init_params.video_vheight != 0) {
        char vsize[64];
        sprintf(vsize, "%dx%d", player->init_params.video_vwidth, player->init_params.video_vheight);
        av_dict_set(&opts, "video_size", vsize, 0);
    }
    if (player->init_params.video_frame_rate != 0) {
        char frate[64];
        sprintf(frate, "%d", player->init_params.video_frame_rate);
        av_dict_set(&opts, "framerate" , frate, 0);
    }

    while (1) {
        // allocate avformat_context
        player->avformat_context = avformat_alloc_context();
        if (!player->avformat_context) goto done;

        // setup interrupt_callback
        player->avformat_context->interrupt_callback.callback = interrupt_callback;
        player->avformat_context->interrupt_callback.opaque   = player;
        player->avformat_context->video_codec_id              = player->init_params.video_codecid;

        // set init_timetick & init_timeout
        player->read_timelast = av_gettime_relative();
        player->read_timeout  = player->init_params.init_timeout ? player->init_params.init_timeout * 1000 : -1;

        if (avformat_open_input(&player->avformat_context, url, fmt, &opts) != 0) {
            
            if (player->init_params.auto_reconnect > 0 && !(player->status & PS_CLOSE)) {
                av_log(NULL, AV_LOG_INFO, "retry to open url: %s ...\n", url);
                av_usleep(100*1000);
            } else {
                av_log(NULL, AV_LOG_ERROR, "failed to open url: %s !\n", url);
                goto done;
            }
        } else {
            av_log(NULL, AV_LOG_INFO, "successed to open url: %s !\n", url);
            break;
        }
    }

    // find stream info
    if (avformat_find_stream_info(player->avformat_context, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to find stream info !\n");
        goto done;
    }

    // set current audio & video stream
    player->astream_index = -1; init_stream(player, AVMEDIA_TYPE_AUDIO, player->init_params.audio_stream_cur);
    player->vstream_index = -1; init_stream(player, AVMEDIA_TYPE_VIDEO, player->init_params.video_stream_cur);
    if (player->astream_index != -1) player->seek_req |= PS_A_SEEK;
    if (player->vstream_index != -1) player->seek_req |= PS_V_SEEK;

    // for audio
    if (player->astream_index != -1) {
        //++ fix audio channel layout issue
        if (player->acodec_context->channel_layout == 0) {
            player->acodec_context->channel_layout = av_get_default_channel_layout(player->acodec_context->channels);
        }
        //-- fix audio channel layout issue
    }

    // for video
    if (player->vstream_index != -1) {
        vrate = player->avformat_context->streams[player->vstream_index]->r_frame_rate;
        if (vrate.num / vrate.den > 100) { vrate.num = 20; vrate.den = 1; }
        player->init_params.video_vwidth = player->init_params.video_owidth  = player->vcodec_context->width;
        player->init_params.video_vheight = player->init_params.video_oheight = player->vcodec_context->height;
    }

    // calculate start_time, apts & vpts
    player->cmnvars.start_time = player->avformat_context->start_time * 1000 / AV_TIME_BASE;
    player->cmnvars.apts       = player->astream_index != -1 ? player->cmnvars.start_time : -1;
    player->cmnvars.vpts       = player->vstream_index != -1 ? player->cmnvars.start_time : -1;

    // init avfilter graph
    vfilter_graph_init(player);

    // open render
    player->render = render_open(player->init_params.adev_render_type, player->init_params.vdev_render_type,
    player->cmnvars.winmsg, vrate, player->init_params.video_owidth, player->init_params.video_oheight, &player->cmnvars);


    // for player init params
    player->init_params.video_frame_rate     = vrate.num / vrate.den;
    player->init_params.video_stream_total   = get_stream_total(player, AVMEDIA_TYPE_VIDEO);
    player->init_params.audio_channels       = player->acodec_context ? av_get_channel_layout_nb_channels(player->acodec_context->channel_layout) : 0;
    player->init_params.audio_sample_rate    = player->acodec_context ? player->acodec_context->sample_rate : 0;
    player->init_params.audio_stream_total   = get_stream_total(player, AVMEDIA_TYPE_AUDIO);
    player->init_params.subtitle_stream_total= get_stream_total(player, AVMEDIA_TYPE_SUBTITLE);
    player->init_params.video_codecid        = player->avformat_context->video_codec_id;
    ret = 0; // prepare ok

done:
    // log_print("player_prepare == 准备完成");
    // send player init message
    player_send_message(player->cmnvars.winmsg, ret ? MSG_OPEN_FAILED : MSG_OPEN_DONE, player);
    return ret;
}


static void read_all_video_key_frames(PLAYER* player){
    if(player->avformat_context == NULL) return;
    AVPacket* packet = pktqueue_request_packet(player->pktqueue);
    if(packet == NULL) return;

    keyframe_list_init(&player->keyframlist,512);
    for (;;) {
            
            int read_status;
            do {
                read_status = av_read_frame(player->avformat_context, packet);
            } while (read_status == AVERROR(EAGAIN));

            if (read_status < 0)
                break;


            if (player->vstream_index == packet->stream_index ) {
                if (packet->flags & AV_PKT_FLAG_KEY) {
                    //获取到视频的关键帧
                    // log_print("视频关键帧位置:%ld",packet->pts);
                    keyframe_list_append(&player->keyframlist,packet->pts);
                }
            }
    }
    pktqueue_release_packet(player->pktqueue, packet);




    // //验证数据
    // for (size_t i = 0; i < player->keyframlist.key; i++)
    // {
    //     log_print("验证::::视频关键帧位置:%ld",keyframe_list_get(&player->keyframlist,i));
    // }
    // keyframe_list_destory(keyframlist);
    


}

static int64_t analyse_next_key_frame(PLAYER *player){
    int64_t vpts = player->vframe.pts;
    
    int64_t keyframepos = 0;
    for (size_t i = 0; i < player->keyframlist.key; i++)
    {
        keyframepos = keyframe_list_get(&player->keyframlist,i);
        if(keyframepos >= vpts) {
            return keyframepos * 1000;
        }
    }

    return 1844674407370955161;
    
}

static void handle_fseek_or_reconnect(PLAYER *player, int reconnect)
{

    //先判断
    //是否需要执行seek
    bool isSeekOps = false;
    //是否进入seek状态
    bool isSeekQuest = false;
    long frame_span = 1000 / 10;

    int64_t vpts = player->vframe.pts * 1000;
    // log_print("seek判断 seek_pos:%ld,  vframe pts:%ld  ",player->seek_pos,vpts);
    //往前seek
    if(vpts < 0){
        isSeekQuest = isSeekOps = true;
    }else if(player->seek_pos < vpts){
        //大于一帧的间隔
        if(vpts - player->seek_pos > frame_span){
            // log_print("往前seek,大于一帧间隔");
            isSeekQuest = isSeekOps = true;
        }else{
            isSeekQuest = isSeekOps = false;
            // log_print("往前seek,小于一帧间隔,不处理");
        }
    }else{
        //往后seek
        //小于一帧的间隔
        if(player->seek_pos - vpts <= frame_span){
            isSeekQuest = isSeekOps = false;
            // log_print("往后seek,小于一帧间隔,不处理");
        }else{
            //小于10帧
            if(player->seek_pos - vpts <= frame_span * 10){
                isSeekQuest = true;
                isSeekOps = false;

                // log_print("往后seek,小于10帧,仅请求seek");
            }else{
                int64_t next_keyframe_pos = analyse_next_key_frame(player);
                
                //下一个关键帧位置在seek请求位置之后
                if(next_keyframe_pos > player->seek_pos){
                    // log_print("往后seek,下一个关键帧位置:%ld,关键帧更远,仅请求seek",next_keyframe_pos);
                    isSeekQuest = true;
                    isSeekOps = false;
                }else{
                    // log_print("往后seek,下一个关键帧位置:%ld,seek位置更远,seek操作",next_keyframe_pos);
                    isSeekQuest = isSeekOps = true;
                }
            }
        }
    }

    if(!isSeekQuest && !isSeekOps) return;

    if(isSeekQuest && isSeekOps){
        // pthread_mutex_lock(&player->seek_lock);
        // log_print("执行ffmpeg seek");
        int PAUSE_REQ = 0;
        int PAUSE_ACK = 0;

        if (player->astream_index != -1) { PAUSE_REQ |= PS_A_PAUSE; PAUSE_ACK |= PS_A_PAUSE << 16; }
        if (player->vstream_index != -1) { PAUSE_REQ |= PS_V_PAUSE; PAUSE_ACK |= PS_V_PAUSE << 16; }

        pthread_rwlock_wrlock(&player->seek_ops_lock);
        // set audio & video decoding pause flags
        player->status = (player->status & ~PAUSE_ACK) | PAUSE_REQ | player->seek_req;


        // make render run
        render_pause(player->render, 0);
        render_setparam(player->render, PARAM_RENDER_STEPFORWARD, NULL);
        // wait for pause done
        // log_print("wait for pause done  1");
        while ((player->status & PAUSE_ACK) != PAUSE_ACK) {
            if (player->status & PS_CLOSE) return;
            av_usleep(20*1000);
        }
        render_setparam(player->render, PARAM_CLEAR_RENDER_STEPFORWARD, NULL);
        pthread_rwlock_unlock(&player->seek_ops_lock);
        // pthread_mutex_unlock(&player->seek_lock);


        av_seek_frame(player->avformat_context, player->seek_sidx, player->seek_pos, AVSEEK_FLAG_BACKWARD);
        if (player->astream_index != -1) avcodec_flush_buffers(player->acodec_context);
        if (player->vstream_index != -1) avcodec_flush_buffers(player->vcodec_context);

        pktqueue_reset(player->pktqueue); // reset pktqueue
        render_reset  (player->render  ); // reset render

        // make audio & video decoding thread resume
        player->status &= ~(PAUSE_REQ|PAUSE_ACK);
        // log_print("wait for pause done  3");

    }else{
        // log_print("执行 文件 seek");
        // pthread_mutex_lock(&player->seek_lock);
        int PAUSE_REQ = 0;
        int PAUSE_ACK = 0;

        if (player->astream_index != -1) { PAUSE_REQ |= PS_A_PAUSE; PAUSE_ACK |= PS_A_PAUSE << 16; }
        if (player->vstream_index != -1) { PAUSE_REQ |= PS_V_PAUSE; PAUSE_ACK |= PS_V_PAUSE << 16; }

        pthread_rwlock_wrlock(&player->seek_ops_lock);
        // set audio & video decoding pause flags
        player->status = (player->status & ~PAUSE_ACK) | PAUSE_REQ | player->seek_req ;


        // make render run
        render_pause(player->render, 0);
        render_setparam(player->render, PARAM_RENDER_STEPFORWARD, NULL);
        // wait for pause done
        // log_print("wait for pause done  1");
        while ((player->status & PAUSE_ACK) != PAUSE_ACK) {
            if (player->status & PS_CLOSE) return;
            av_usleep(20*1000);
        }
        render_setparam(player->render, PARAM_CLEAR_RENDER_STEPFORWARD, NULL);
        pthread_rwlock_unlock(&player->seek_ops_lock);
        // log_print("wait for pause done  2");
        // pthread_mutex_unlock(&player->seek_lock);
        render_reset  (player->render  ); // reset render

        // make audio & video decoding thread resume
        player->status &= ~(PAUSE_REQ|PAUSE_ACK);
        // log_print("wait for pause done  3");


    }

    






}

static void* av_demux_thread_proc(void *param)
{
    PLAYER   *player = (PLAYER*)param;
    AVPacket *packet = NULL;
    int       retv   = 0;

    // async prepare player
    if (!player->init_params.open_syncmode) {
        retv = player_prepare(player);
        //读取所有关键帧的位置
        read_all_video_key_frames(player);
        if (retv != 0) goto done;
    }

    
    while (!(player->status & PS_CLOSE)) {
        if (!player->avformat_context) { av_usleep(20*1000); continue; }
        //++ when player seek ++//
        if (player->status & (PS_F_SEEK)) {

            pthread_mutex_lock(&player->seek_lock);
            if (player->status & (PS_F_SEEK)) {
                handle_fseek_or_reconnect(player,  0);
                player->status &= ~(PS_F_SEEK);
            }
            pthread_mutex_unlock(&player->seek_lock);
        }
        //-- when player seek --//

        packet = pktqueue_request_packet(player->pktqueue);
        if (packet == NULL) continue;

        retv = av_read_frame(player->avformat_context, packet);
        if (retv < 0) {
            pktqueue_release_packet(player->pktqueue, packet);

            if (  player->init_params.auto_reconnect > 0 && av_gettime_relative() - player->read_timelast > player->init_params.auto_reconnect * 1000) {

            } else {
                av_usleep(20*1000);
            }
            // log_print("av_demux_thread_proc 没有解析到帧");
            continue;
        } else {
            player->read_timelast = av_gettime_relative();
        }


        // audio
        if (packet->stream_index == player->astream_index) {
            // log_print("av_demux_thread_proc 解封装-音频");

            pktqueue_audio_enqueue(player->pktqueue, packet);
        }

        // video
        if (packet->stream_index == player->vstream_index) {
            // log_print("av_demux_thread_proc 解封装-视频");
            pktqueue_video_enqueue(player->pktqueue, packet);
        }

        if (  packet->stream_index != player->astream_index && packet->stream_index != player->vstream_index) {
            pktqueue_release_packet(player->pktqueue, packet);
        }
    }

done:
    // log_print("解封装 线程完毕");
    return NULL;
}

static void* audio_decode_thread_proc(void *param)
{
    // log_print("音频解码 == 线程启动 ");
    PLAYER   *player = (PLAYER*)param;
    AVPacket *packet = NULL;
    int64_t   apts;

    player->aframe.pts = -1;
    while (!(player->status & PS_CLOSE)) {
        // log_print("音频解码 == 线程   循环开始 ");
        //++ when audio decode pause ++//
        if (player->status & PS_A_PAUSE) {
            // log_print("音频解码 == 线程   等待seek ");
            player->status |= (PS_A_PAUSE << 16);
            av_usleep(20*1000); continue;
        }
        //-- when audio decode pause --//
        if(player->astream_index == -1){
            av_usleep(20*1000); continue;
        }
        
        // dequeue audio packet
        packet = pktqueue_audio_dequeue(player->pktqueue);
        if (packet == NULL) {
            // log_print("异常情况");
            // render_audio(player->render, &player->aframe);
            continue;
        } else{
            // datarate_audio_packet(player->datarate, packet);
        } 
        

        // log_print("decode audio  packet  1");
        //++ decode audio packet ++//
        apts = AV_NOPTS_VALUE;
        while (packet->size > 0 && !(player->status & (PS_A_PAUSE|PS_CLOSE))) {
            int consumed = 0;
            int gotaudio = 0;

            consumed = avcodec_decode_audio4(player->acodec_context, &player->aframe, &gotaudio, packet);
            if (consumed < 0) {
                av_log(NULL, AV_LOG_WARNING, "an error occurred during decoding audio.\n");
                break;
            }

            if (gotaudio) {
                // log_print("获取到音频");
                AVRational tb_sample_rate = { 1, player->acodec_context->sample_rate };
                if (apts == AV_NOPTS_VALUE) {
                    apts  = av_rescale_q(player->aframe.pts, player->astream_timebase, tb_sample_rate);
                } else {
                    apts += player->aframe.nb_samples;
                }
                player->aframe.pts = av_rescale_q(apts, tb_sample_rate, TIMEBASE_MS);
                //++ for seek operation
                if (player->status & PS_A_SEEK && pthread_rwlock_tryrdlock(&player->seek_ops_lock) == 0 ) {

                        // log_print("音频请求位置:%ld, 音频当前位置pts:%ld，seek_diff是：%ld",player->seek_dest,player->aframe.pts,player->seek_diff);
                        if (player->status & PS_A_SEEK && player->seek_dest - player->aframe.pts <= player->seek_diff) {
                            // log_print("音频 seek 满足");
                            player->cmnvars.start_tick = av_gettime_relative() / 1000;
                            player->cmnvars.start_pts  = player->aframe.pts;
                            player->cmnvars.apts       = player->aframe.pts;
                            player->cmnvars.vpts       = player->vstream_index == -1 ? -1 : player->seek_dest;
                            player->status &= ~PS_A_SEEK;

                            // render_pause(player->render, 1);
                            if (player->status & PS_R_PAUSE) {
                                render_pause(player->render, 1);
                            }
                        }

                        pthread_rwlock_unlock(&player->seek_ops_lock);
                    }
                //-- for seek operation
                if (!(player->status & PS_A_SEEK)) {
                    // log_print("decode audio  render_audio  1");
                    render_audio(player->render, &player->aframe);
                    // log_print("decode audio  render_audio  2");
                }


            }

            packet->data += consumed;
            packet->size -= consumed;
        }
        // log_print("结束音频循环");
        //-- decode audio packet --//

        // release packet
        pktqueue_release_packet(player->pktqueue, packet);
        // log_print("结束音频循环 2");

        // log_print("decode audio  packet  4");
    }

    av_frame_unref(&player->aframe);
    // log_print("音频解码 ==  线程完毕");
    return NULL;
}

static void* video_decode_thread_proc(void *param)
{
    // log_print("视频解码 == 线程启动");
    PLAYER   *player = (PLAYER*)param;
    AVPacket *packet = NULL;

    player->vframe.pts = -1;
    while (!(player->status & PS_CLOSE)) {
        //++ when video decode pause ++//
        if (player->status & PS_V_PAUSE) {
            // log_print("视频解码 == 等待seek操作");
            player->status |= (PS_V_PAUSE << 16);
            av_usleep(20*1000); continue;
        }else{
            // log_print("视频解码 == 结束seek操作");
        }
        //-- when video decode pause --//


        // dequeue video packet
        packet = pktqueue_video_dequeue(player->pktqueue);
        if (packet == NULL) {
            // av_usleep(20*1000);
            // render_video(player->render, &player->vframe);
            continue;
        }


        // log_print("视频解码中。。。");
        //++ decode video packet ++//
        while (packet->size > 0 && !(player->status & (PS_V_PAUSE|PS_CLOSE))) {
            int consumed = 0;
            int gotvideo = 0;

            consumed = avcodec_decode_video2(player->vcodec_context, &player->vframe, &gotvideo, packet);
            if (consumed < 0) {
                av_log(NULL, AV_LOG_WARNING, "an error occurred during decoding video.\n");
                break;
            }
            if (player->vcodec_context->width != player->init_params.video_vwidth || player->vcodec_context->height != player->init_params.video_vheight) {
                player->init_params.video_vwidth  = player->init_params.video_owidth  = player->vcodec_context->width;
                player->init_params.video_vheight = player->init_params.video_oheight = player->vcodec_context->height;
                vfilter_graph_free(player);
                vfilter_graph_init(player);
                player_send_message(player->cmnvars.winmsg, MSG_VIDEO_RESIZED, 0);
            }

            if (gotvideo) {
                // log_print("获取到图像。。。");
                player->vframe.height = player->vcodec_context->height; // when using dxva2 hardware hwaccel, the frame heigh may incorrect, so we need fix it
                vfilter_graph_input(player, &player->vframe);
                do {
                    if (vfilter_graph_output(player, &player->vframe) < 0) break;
                    player->seek_vpts = av_frame_get_best_effort_timestamp(&player->vframe);
//                  player->seek_vpts = player->vframe.pkt_dts; // if rtmp has problem, try to use this code
                    player->vframe.pts= av_rescale_q(player->seek_vpts, player->vstream_timebase, TIMEBASE_MS);
                    // log_print(" vframe pts:%ld  ",player->vframe.pts * 1000);
                    //锁住操作
                    //++ for seek operation
                    if (player->status & PS_V_SEEK) {
                        // log_print("视频解码 == 获取到seek请求");
                        if(pthread_rwlock_tryrdlock(&player->seek_ops_lock) == 0){
                            if (player->status & PS_V_SEEK && player->seek_dest - player->vframe.pts <= player->seek_diff) {
                                // log_print("视频seek 满足");
                                player->cmnvars.start_tick = av_gettime_relative() / 1000;
                                player->cmnvars.start_pts  = player->vframe.pts;
                                player->cmnvars.vpts       = player->vframe.pts;
                                player->cmnvars.apts       = player->astream_index == -1 ? -1 : player->seek_dest;
                                player->status &= ~PS_V_SEEK;

                                if(player->astream_index == -1){
                                    if (player->status & PS_R_PAUSE) {
                                        // log_print("seek完成调用暂停");
                                        render_pause(player->render, 1);
                                    }
                                }else{
                                    render_pause(player->render, 1);
                                }

                                // if (player->status & PS_R_PAUSE) {
                                //     log_print("seek完成调用暂停");
                                //     render_pause(player->render, 1);
                                // }
                            }

                            pthread_rwlock_unlock(&player->seek_ops_lock);
                        }

                    }

                    //-- for seek operation
                    if (!(player->status & PS_V_SEEK)){
                        render_video(player->render, &player->vframe);
                    }
                } while (player->vfilter_graph);
            }
            // log_print("视频 干4");

            packet->data += packet->size;
            packet->size -= packet->size;
        }
        //-- decode video packet --//

        // release packet
        pktqueue_release_packet(player->pktqueue, packet);
    }

    av_frame_unref(&player->vframe);
    // log_print("视频解码 ==  线程完毕");
    return NULL;
}

// 函数实现
void* player_open(char *file, void *win, PLAYER_INIT_PARAMS *params)
{
    PLAYER *player = NULL;

    // av register all
    av_register_all();
    avdevice_register_all();
    avfilter_register_all();
    avformat_network_init();

    // setup log
    av_log_set_level   (AV_LOG_WARNING);
    av_log_set_callback(avlog_callback);

    // alloc player context
    player = (PLAYER*)calloc(1, sizeof(PLAYER));
    if (!player) return NULL;

    pthread_mutex_init(&player->seek_lock, NULL);
    pthread_rwlock_init(&player->seek_ops_lock, NULL);
    

    // create packet queue
    player->pktqueue = pktqueue_create(0, &player->cmnvars);
    if (!player->pktqueue) {
        av_log(NULL, AV_LOG_ERROR, "failed to create packet queue !\n");
        goto error_handler;
    }

    // for player init params
    if (params) memcpy(&player->init_params, params, sizeof(PLAYER_INIT_PARAMS));
    player->cmnvars.init_params = &player->init_params;

    //++ for player_prepare
    strcpy(player->url, file);
    //纹理的指针
    player->cmnvars.winmsg = win;
    //-- for player_prepare

    // make sure player status paused
    player->status = (PS_A_PAUSE|PS_V_PAUSE|PS_R_PAUSE);

    if (player->init_params.open_syncmode && player_prepare(player) == -1) {
        av_log(NULL, AV_LOG_ERROR, "failed to prepare player !\n");
        goto error_handler;
    }

    pthread_create(&player->avdemux_thread, NULL, av_demux_thread_proc, player);


    pthread_create(&player->adecode_thread, NULL, audio_decode_thread_proc, player);
    pthread_create(&player->vdecode_thread, NULL, video_decode_thread_proc, player);
    return player; // return

error_handler:
    player_close(player);
    return NULL;
}

void player_close(void *hplayer)
{
    // log_print("关闭播放器！！！！！！！！！！！！！！！！！！！！1");
    PLAYER *player = (PLAYER*)hplayer;
    if (!hplayer) return;

    // set read_timeout to 0
    player->read_timeout = 0;

    // set close flag
    player->status |= PS_CLOSE;
    render_setparam(player->render, PARAM_RENDER_STOP, NULL);

    // wait audio/video demuxing thread exit
    if (player->avdemux_thread) pthread_join(player->avdemux_thread, NULL);

    // wait audio decoding thread exit
    if (player->adecode_thread) pthread_join(player->adecode_thread, NULL);

    // wait video decoding thread exit
    if (player->vdecode_thread) pthread_join(player->vdecode_thread, NULL);

    // free avfilter graph
    vfilter_graph_free(player);

    if (player->acodec_context  ) avcodec_close(player->acodec_context);
    if (player->vcodec_context  ) avcodec_close(player->vcodec_context);
    if (player->avformat_context) avformat_close_input(&player->avformat_context);
    if (player->render          ) render_close (player->render);


    // datarate_destroy(player->datarate); // destroy data rate
    pktqueue_destroy(player->pktqueue); // destroy packet queue



    free(player);

    // deinit network
    avformat_network_deinit();
}

void player_play(void *hplayer)
{
    PLAYER *player = (PLAYER*)hplayer;
    if (!hplayer) return;

    // log_print("调用player_play   开始");

    struct timespec time_out;
    clock_gettime(CLOCK_REALTIME, &time_out);
    time_out.tv_sec += 1;

    if(pthread_mutex_timedlock(&player->seek_lock, &time_out) == 0){
        player->status &= PS_CLOSE;
        render_pause(player->render, 0);
        // datarate_reset(player->datarate);
        pthread_mutex_unlock(&player->seek_lock);
    }
    // pthread_mutex_lock(&player->seek_lock);
    // log_print("调用player_play   结束");
}

void player_pause(void *hplayer)
{
    PLAYER *player = (PLAYER*)hplayer;
    if (!hplayer) return;
    // log_print("调用player_pause   开始");
    struct timespec time_out;
    clock_gettime(CLOCK_REALTIME, &time_out);
    time_out.tv_sec += 1;

    if(pthread_mutex_timedlock(&player->seek_lock, &time_out) == 0){
        // log_print("调用player_pause   执行");
        player->status |= PS_R_PAUSE;
        render_pause(player->render, 1);
        // datarate_reset(player->datarate);
        pthread_mutex_unlock(&player->seek_lock);
    }
    // pthread_mutex_lock(&player->seek_lock);
    // log_print("调用player_pause   结束");
}

void player_setrect(void *hplayer, int type, int x, int y, int w, int h)
{
    PLAYER *player = (PLAYER*)hplayer;
    if (!hplayer) return;
    render_setrect(player->render, type, x, y, w, h);
}

void player_seek(void *hplayer, int64_t ms, int type)
{
    PLAYER    *player = (PLAYER*)hplayer;
    AVRational frate;
    if (!hplayer) return;

    // if(pthread_mutex_trylock(&player->seek_lock) != 0) {
    //     av_log(NULL, AV_LOG_WARNING, "seek busy 1 !\n");
    //     return;
    // }
    if (player->status & (PS_F_SEEK | player->seek_req)) {
        // pthread_mutex_unlock(&player->seek_lock);
        av_log(NULL, AV_LOG_WARNING, "seek busy 2 !\n");
        return;
    }
    

    switch (type) {
    case SEEK_STEP_FORWARD:
        render_pause(player->render, 1);
        render_setparam(player->render, PARAM_RENDER_STEPFORWARD, NULL);
        return;
    case SEEK_STEP_BACKWARD:
        frate = player->avformat_context->streams[player->vstream_index]->r_frame_rate;
        player->seek_dest = av_rescale_q(player->seek_vpts, player->vstream_timebase, TIMEBASE_MS) - 1000 * frate.den / frate.num - 1;
        player->seek_pos  = player->seek_vpts + av_rescale_q(ms, TIMEBASE_MS, player->vstream_timebase);
        player->seek_diff = 0;
        player->seek_sidx = player->vstream_index;
        player->status   |= PS_R_PAUSE;
        break;
    default:
        player->seek_dest =  player->cmnvars.start_time + ms;
        player->seek_pos  = (player->cmnvars.start_time + ms) * AV_TIME_BASE / 1000;
        player->seek_diff = 100;
        player->seek_sidx = -1;
        break;
    }

    // set PS_F_SEEK flag
    player->status |= PS_F_SEEK;
    // pthread_mutex_unlock(&player->seek_lock);
}


void player_setparam(void *hplayer, int id, void *param)
{
    PLAYER *player = (PLAYER*)hplayer;
    if (!hplayer) return;

    switch (id) {
        default: render_setparam(player->render, id, param); break;
    }
}

void player_getparam(void *hplayer, int id, void *param)
{
    PLAYER *player = (PLAYER*)hplayer;
    if (!hplayer || !param) return;

    switch (id) {
    case PARAM_MEDIA_DURATION:
        *(int64_t*)param = player->avformat_context ? (player->avformat_context->duration * 1000 / AV_TIME_BASE) : 1;
        break;
    case PARAM_MEDIA_POSITION:
        if ((player->status & PS_F_SEEK) || (player->status & player->seek_req)) {
            *(int64_t*)param = player->seek_dest - player->cmnvars.start_time;
        } else {
            int64_t pos = 0; render_getparam(player->render, id, &pos);
            *(int64_t*)param = pos == -1 ? -1 : pos - player->cmnvars.start_time;
        }
        break;
    case PARAM_VIDEO_WIDTH:
        if (!player->vcodec_context) *(int*)param = 0;
        else *(int*)param = player->init_params.video_owidth;
        break;
    case PARAM_VIDEO_HEIGHT:
        if (!player->vcodec_context) *(int*)param = 0;
        else *(int*)param = player->init_params.video_oheight;
        break;
    case PARAM_RENDER_GET_CONTEXT:
        *(void**)param = player->render;
        break;
    case PARAM_PLAYER_INIT_PARAMS:
        memcpy(param, &player->init_params, sizeof(PLAYER_INIT_PARAMS));
        break;
    case PARAM_ASTEAM_ID:
        *(int*)param = player->astream_index;
        break;
    default:
        render_getparam(player->render, id, param);
        break;
    }
}

void player_send_message(void *extra, int32_t msg, void *param) {
    UnityPostMessage(extra,msg);
}

//++ load player init params from string
static char* parse_params(const char *str, const char *key, char *val, int len)
{
    char *p = (char*)strstr(str, key);
    int   i;

    if (!p) return NULL;
    p += strlen(key);
    if (*p == '\0') return NULL;

    while (1) {
        if (*p != ' ' && *p != '=' && *p != ':') break;
        else p++;
    }

    for (i=0; i<len; i++) {
        if (*p == '\\') {
            p++;
        } else if (*p == ';' || *p == '\r' || *p == '\n' || *p == '\0') {
            break;
        }
        val[i] = *p++;
    }
    val[i < len ? i : len - 1] = '\0';
    return val;
}

void player_load_params(PLAYER_INIT_PARAMS *params, char *str)
{
    char value[16];
    params->video_stream_cur    = atoi(parse_params(str, "video_stream_cur"   , value, sizeof(value)) ? value : "0");
    params->video_thread_count  = atoi(parse_params(str, "video_thread_count" , value, sizeof(value)) ? value : "0");
    params->video_hwaccel       = atoi(parse_params(str, "video_hwaccel"      , value, sizeof(value)) ? value : "0");
    params->video_deinterlace   = atoi(parse_params(str, "video_deinterlace"  , value, sizeof(value)) ? value : "0");
    params->video_rotate        = atoi(parse_params(str, "video_rotate"       , value, sizeof(value)) ? value : "0");
    params->video_bufpktn       = atoi(parse_params(str, "video_bufpktn"      , value, sizeof(value)) ? value : "0");
    params->video_vwidth        = atoi(parse_params(str, "video_vwidth"       , value, sizeof(value)) ? value : "0");
    params->video_vheight       = atoi(parse_params(str, "video_vheight"      , value, sizeof(value)) ? value : "0");
    params->audio_stream_cur    = atoi(parse_params(str, "audio_stream_cur"   , value, sizeof(value)) ? value : "0");
    params->audio_bufpktn       = atoi(parse_params(str, "audio_bufpktn"      , value, sizeof(value)) ? value : "0");
    params->audio_read_channels       = atoi(parse_params(str, "audio_read_channels"      , value, sizeof(value)) ? value : "0");
    params->audio_read_sampleRate       = atoi(parse_params(str, "audio_read_sampleRate"      , value, sizeof(value)) ? value : "0");

    params->subtitle_stream_cur = atoi(parse_params(str, "subtitle_stream_cur", value, sizeof(value)) ? value : "0");
    params->vdev_render_type    = atoi(parse_params(str, "vdev_render_type"   , value, sizeof(value)) ? value : "0");
    params->adev_render_type    = atoi(parse_params(str, "adev_render_type"   , value, sizeof(value)) ? value : "0");
    params->init_timeout        = atoi(parse_params(str, "init_timeout"       , value, sizeof(value)) ? value : "0");
    params->open_syncmode       = atoi(parse_params(str, "open_syncmode"      , value, sizeof(value)) ? value : "0");
    params->auto_reconnect      = atoi(parse_params(str, "auto_reconnect"     , value, sizeof(value)) ? value : "0");
    params->rtsp_transport      = atoi(parse_params(str, "rtsp_transport"     , value, sizeof(value)) ? value : "0");
    params->avts_syncmode       = atoi(parse_params(str, "avts_syncmode"      , value, sizeof(value)) ? value : "0");
    params->swscale_type        = atoi(parse_params(str, "swscale_type"       , value, sizeof(value)) ? value : "0");
    params->waveout_device_id   = atoi(parse_params(str, "waveout_device_id"  , value, sizeof(value)) ? value : "0");
    parse_params(str, "filter_string", params->filter_string, sizeof(params->filter_string));
    parse_params(str, "ffrdp_tx_key" , params->ffrdp_tx_key , sizeof(params->ffrdp_tx_key ));
    parse_params(str, "ffrdp_rx_key" , params->ffrdp_rx_key , sizeof(params->ffrdp_rx_key ));
}
//-- load player init params from string
