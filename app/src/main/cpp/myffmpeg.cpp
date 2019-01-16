#include <jni.h>
#include <string>
#include <android/log.h>
#include <zconf.h>
#include "android/native_window.h"
#include "android/native_window_jni.h"
#include "unistd.h"

#define LOGI(FORMAT, ...) __android_log_print(ANDROID_LOG_INFO,"zmq",FORMAT,##__VA_ARGS__);
#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"zmq",FORMAT,##__VA_ARGS__);
//编码
//C/C++混编，指示编译器按照C语言进行编译
//#ifdef __cplusplus
extern "C" {
//#endif
#include "libavcodec/avcodec.h"
//封装格式处理
#include "libavformat/avformat.h"
//像素处理
#include "libswscale/swscale.h"
#include "libavutil/avutil.h"
#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"
#include "libyuv/convert_argb.h"
#include "libswresample/swresample.h"
}
#define MAX_AUDIO_FRAME_SIZE 48000 * 4
jobject audioTrack_gb_ref;
using namespace libyuv;

extern "C"
JNIEXPORT jstring JNICALL
Java_com_demo_ffmpeg_VideoPlayer_decode(JNIEnv *env, jclass jcls, jstring input_jstr,
                                        jstring output_jstr) {
    //需要转码的视频文件(输入的视频文件)
    const char *input_cstr = env->GetStringUTFChars(input_jstr, 0);
    const char *output_cstr = env->GetStringUTFChars(output_jstr, 0);
    const char *config = avcodec_configuration();
    LOGE("config is %s", config);

    //1.注册所有组件
    av_register_all();

    //封装格式上下文，统领全局的结构体，保存了视频文件封装格式的相关信息
    AVFormatContext *pFormatCtx = avformat_alloc_context();

    //2.打开输入视频文件
    if (avformat_open_input(&pFormatCtx, input_cstr, NULL, NULL) != 0) {
        LOGE("%s", "无法打开输入视频文件");
        return NULL;
    }

    //3.获取视频文件信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("%s", "无法获取视频文件信息");
        return NULL;
    }

    //获取视频流的索引位置
    //遍历所有类型的流（音频流、视频流、字幕流），找到视频流
    int v_stream_idx = -1;
    int i = 0;
    //number of streams
    for (; i < pFormatCtx->nb_streams; i++) {
        //流的类型
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            v_stream_idx = i;
            break;
        }
    }

    if (v_stream_idx == -1) {
        LOGE("%s", "找不到视频流\n");
        return NULL;
    }

    //只有知道视频的编码方式，才能够根据编码方式去找到解码器
    //获取视频流中的编解码上下文
    AVCodecContext *pCodecCtx = pFormatCtx->streams[v_stream_idx]->codec;
    //4.根据编解码上下文中的编码id查找对应的解码
    AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    //（迅雷看看，找不到解码器，临时下载一个解码器）
    if (pCodec == NULL) {
        LOGE("%s", "找不到解码器\n");
        return NULL;
    }

    //5.打开解码器
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        LOGE("%s", "解码器无法打开\n");
        return NULL;
    }

    //输出视频信息
    LOGI("视频的文件格式：%s", pFormatCtx->iformat->name);
    LOGI("视频时长：%d", (pFormatCtx->duration) / 1000000);
    LOGI("视频的宽高：%d,%d", pCodecCtx->width, pCodecCtx->height);
    LOGI("解码器的名称：%s", pCodec->name);

    //准备读取
    //AVPacket用于存储一帧一帧的压缩数据（H264）
    //缓冲区，开辟空间
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));

    //AVFrame用于存储解码后的像素数据(YUV)
    //内存分配
    AVFrame *pFrame = av_frame_alloc();
    //YUV420
    AVFrame *pFrameYUV = av_frame_alloc();
    //只有指定了AVFrame的像素格式、画面大小才能真正分配内存
    //缓冲区分配内存
    uint8_t *out_buffer = (uint8_t *) av_malloc(
            avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
    //初始化缓冲区
    avpicture_fill((AVPicture *) pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width,
                   pCodecCtx->height);

    //用于转码（缩放）的参数，转之前的宽高，转之后的宽高，格式等
    struct SwsContext *sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                                                pCodecCtx->pix_fmt,
                                                pCodecCtx->width, pCodecCtx->height,
                                                AV_PIX_FMT_YUV420P,
                                                SWS_BICUBIC, NULL, NULL, NULL);


    int got_picture, ret;

    FILE *fp_yuv = fopen(output_cstr, "wb+");

    int frame_count = 0;

    //6.一帧一帧的读取压缩数据
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        //只要视频压缩数据（根据流的索引位置判断）
        if (packet->stream_index == v_stream_idx) {
            //7.解码一帧视频压缩数据，得到视频像素数据
            ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
            if (ret < 0) {
                LOGE("%s", "解码错误");
                return NULL;
            }

            //为0说明解码完成，非0正在解码
            if (got_picture) {
                //AVFrame转为像素格式YUV420，宽高
                //2 6输入、输出数据
                //3 7输入、输出画面一行的数据的大小 AVFrame 转换是一行一行转换的
                //4 输入数据第一列要转码的位置 从0开始
                //5 输入画面的高度
                sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
                          pFrameYUV->data, pFrameYUV->linesize);

                //输出到YUV文件
                //AVFrame像素帧写入文件
                //data解码后的图像像素数据（音频采样数据）
                //Y 亮度 UV 色度（压缩了） 人对亮度更加敏感
                //U V 个数是Y的1/4
                int y_size = pCodecCtx->width * pCodecCtx->height;
                fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);
                fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);
                fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);

                frame_count++;
                LOGI("解码第%d帧", frame_count);
            }
        }

        //释放资源
        av_free_packet(packet);
    }

    fclose(fp_yuv);

    // TODO

    env->ReleaseStringUTFChars(input_jstr, input_cstr);
    env->ReleaseStringUTFChars(output_jstr, output_cstr);

    av_frame_free(&pFrame);

    avcodec_close(pCodecCtx);

    avformat_free_context(pFormatCtx);

    return env->NewStringUTF(config);

}

extern "C"
JNIEXPORT void JNICALL
Java_com_demo_ffmpeg_VideoPlayer_decodeVideo(JNIEnv *env, jclass type, jstring input_jstr,
                                             jstring output_jstr) {
    const char *input_cstr = env->GetStringUTFChars(input_jstr, 0);
    const char *output_cstr = env->GetStringUTFChars(output_jstr, 0);
    //1、注册所有组件(可以不全部注册)
    av_register_all();
    //初始化封装格式上下文
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    //2、打开输入视频文件
    if (avformat_open_input(&pFormatCtx, input_cstr, NULL, NULL) < 0) {
        LOGE("%s", "打开视频文件失败");
        return;
    }
    //获取视频文件信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("%s", "获取视频文件信息出错");
        return;
    }
    int i = 0;
    int video_stream_index = -1;
    //nb_streams 输入视频的AVStream 个数
    //遍历视频的AVStream数组查找到视频AVStream所在的索引
    for (; i < pFormatCtx->nb_streams; i++) {
        //如果输入视频的AVStream数组
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    //编解码上下文
    AVCodecContext *avCodecCtx = pFormatCtx->streams[video_stream_index]->codec;
    AVCodecID id = avCodecCtx->codec_id;;
    //3、查找编解码器
    AVCodec *avCodec = avcodec_find_decoder(id);
    if (avCodec == NULL) {
        LOGE("%s", "没有找到解码器");
        return;
    }
    //打开解码器
    if (avcodec_open2(avCodecCtx, avCodec, NULL) < 0) {
        LOGE("%s", "打开解码器失败");
        return;
    }
    //编码数据(压缩数据)
    AVPacket *packet = av_packet_alloc();
//    AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));
    //像素数据(解压缩数据)
    AVFrame *frame = av_frame_alloc();
    AVFrame *yuvFrame = av_frame_alloc();

    //只有指定了AVFrame的像素格式、画面大小才能真正分配内存
    //缓冲区分配内存
    uint8_t *out_buffer = static_cast<uint8_t *>(av_malloc(
            avpicture_get_size(AV_PIX_FMT_YUV420P, avCodecCtx->width, avCodecCtx->height)));
    //初始化缓冲区
    avpicture_fill((AVPicture *) yuvFrame, out_buffer, AV_PIX_FMT_YUV420P, avCodecCtx->width,
                   avCodecCtx->height);
    //输出文件
    FILE *fp = fopen(output_cstr, "wb");
    //用于像素格式转换(swscale库用于视频像素格式转换)
    struct SwsContext *swsContext = sws_getContext(avCodecCtx->width, avCodecCtx->height,
                                                   avCodecCtx->pix_fmt, avCodecCtx->width,
                                                   avCodecCtx->height, AV_PIX_FMT_YUV420P,
                                                   SWS_BILINEAR, NULL, NULL, NULL);
    int got_picture, len, frameCount = 0;
    //一帧一帧的读取视频的编码数据(压缩数据)
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        //注意加上判断,解析视频Packet
        if (packet->stream_index == video_stream_index) {
            //解码 AVPacket -> AVFrame
            len = avcodec_decode_video2(avCodecCtx, frame, &got_picture, packet);//解码一帧压缩数据
            //非零,正在解码
            if (got_picture) {
                //转为指定的YUV420P像素帧
                sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height,
                          yuvFrame->data,
                          yuvFrame->linesize);
                //向YUV文件保存解码之后的帧数据
                //AVFrame -> YUV
                //一个像素包含一个Y
                int y_size = avCodecCtx->width * avCodecCtx->height;
                fwrite(yuvFrame->data[0], 1, y_size, fp);
                fwrite(yuvFrame->data[1], 1, y_size / 4, fp);
                fwrite(yuvFrame->data[2], 1, y_size / 4, fp);
                LOGE("解码%d帧", frameCount++);
            }
        }
        av_free_packet(packet);
    }
    //关闭文件
    fclose(fp);
    //
    av_frame_free(&frame);
    av_frame_free(&yuvFrame);
    //关闭解码器
    avcodec_close(avCodecCtx);
    avformat_free_context(pFormatCtx);
    //关闭输入视频文件
    //avformat_close_input(&pFormatCtx);

    env->ReleaseStringUTFChars(input_jstr, input_cstr);
    env->ReleaseStringUTFChars(output_jstr, output_cstr);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_demo_ffmpeg_VideoPlayer_decodeAndDrawVideo(JNIEnv *env, jclass jclass, jstring input_jstr,
                                                    jobject surface) {
    const char *input_cstr = env->GetStringUTFChars(input_jstr, 0);
    //1、注册所有组件(可以不全部注册)
    av_register_all();
    //初始化封装格式上下文
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    //2、打开输入视频文件
    if (avformat_open_input(&pFormatCtx, input_cstr, NULL, NULL) < 0) {
        LOGE("%s", "打开视频文件失败");
        return;
    }
    //获取视频文件信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("%s", "获取视频文件信息出错");
        return;
    }
    int i = 0;
    int video_stream_index = -1;
    //nb_streams 输入视频的AVStream 个数
    //遍历视频的AVStream数组查找到视频AVStream所在的索引
    for (; i < pFormatCtx->nb_streams; i++) {
        //如果输入视频的AVStream数组
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    //编解码上下文
    AVCodecContext *avCodecCtx = pFormatCtx->streams[video_stream_index]->codec;
    AVCodecID id = avCodecCtx->codec_id;;
    //3、查找编解码器
    AVCodec *avCodec = avcodec_find_decoder(id);
    if (avCodec == NULL) {
        LOGE("%s", "没有找到解码器");
        return;
    }
    //打开解码器
    if (avcodec_open2(avCodecCtx, avCodec, NULL) < 0) {
        LOGE("%s", "打开解码器失败");
        return;
    }
    //编码数据(压缩数据)
    AVPacket *packet = av_packet_alloc();
//    AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));
    //像素数据(解压缩数据)
    AVFrame *yuvFrame = av_frame_alloc();
    AVFrame *rgbFrame = av_frame_alloc();
    //native 绘制 窗体  依赖系统的android库,CMakeList.txt中添加依赖配置
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
    //绘制时的缓冲区
    ANativeWindow_Buffer outBuffer;

    int got_picture, len, frameCount = 0;
    //一帧一帧的读取视频的编码数据(压缩数据)
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        //注意只解析视频Packet
        if (packet->stream_index == video_stream_index) {
            //解码 AVPacket -> AVFrame
            len = avcodec_decode_video2(avCodecCtx, yuvFrame, &got_picture, packet);//解码一帧压缩数据
            //非零,正在解码
            if (got_picture) {
                LOGE("解码第%d帧", frameCount++);
                //设置缓冲区属性(宽/高/像素格式)
                ANativeWindow_setBuffersGeometry(nativeWindow, avCodecCtx->width,
                                                 avCodecCtx->height,
                                                 WINDOW_FORMAT_RGBA_8888);
                //锁定Window
                ANativeWindow_lock(nativeWindow, &outBuffer, NULL);
                //设置rgb_frame属性(宽、高、像素格式、缓冲区)
                //rgb_frame缓冲区与outBuffer.bits是同一块内存
                avpicture_fill((AVPicture *) rgbFrame, static_cast<const uint8_t *>(outBuffer.bits),
                               AV_PIX_FMT_RGBA, avCodecCtx->width, avCodecCtx->height);
                //将yuv格式的AVFrame 转换为 RGB8888格式,用到一个开源库libyuv来处理
                //注意 u v 顺序对调，解决颜色显示不正的问题(参考示例程序)
                I420ToARGB(yuvFrame->data[0], yuvFrame->linesize[0],
                           yuvFrame->data[2], yuvFrame->linesize[2],
                           yuvFrame->data[1], yuvFrame->linesize[1],
                           rgbFrame->data[0], rgbFrame->linesize[0],
                           avCodecCtx->width, avCodecCtx->height);
                //解锁Window
                ANativeWindow_unlockAndPost(nativeWindow);
                //休眠 16ms 单位是微秒
                usleep(1000 * 16);
            }
        }
        av_free_packet(packet);
    }
    //释放窗体
    ANativeWindow_release(nativeWindow);
    av_frame_free(&yuvFrame);
    //关闭解码器
    avcodec_close(avCodecCtx);
    avformat_free_context(pFormatCtx);
    //关闭输入视频文件
    //avformat_close_input(&pFormatCtx);
//
    env->ReleaseStringUTFChars(input_jstr, input_cstr);
}
//#ifdef __cplusplus
//};
//#endif

extern "C"
JNIEXPORT void JNICALL
Java_com_demo_ffmpeg_VideoPlayer_decodeAudio(JNIEnv *env, jclass type, jstring input_jstr,
                                             jstring output_jstr) {
    const char *input_cstr = env->GetStringUTFChars(input_jstr, 0);
    const char *output_cstr = env->GetStringUTFChars(output_jstr, 0);
    //注册组件
    av_register_all();
    AVFormatContext *pAvFmtCtx = avformat_alloc_context();
    avformat_open_input(&pAvFmtCtx, input_cstr, NULL, NULL);
    avformat_find_stream_info(pAvFmtCtx, NULL);
    int audio_stream_index = -1;
    for (int i = 0; i < pAvFmtCtx->nb_streams; ++i) {
        if (pAvFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    AVCodecContext *pAvCodecCtx = pAvFmtCtx->streams[audio_stream_index]->codec;
    //获取解码器
    AVCodec *pAvCodec = avcodec_find_decoder(pAvCodecCtx->codec_id);
    //打开解码器
    avcodec_open2(pAvCodecCtx, pAvCodec, NULL);

    AVPacket *avPacket = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    //重采样设置参数开始
    //输入的采样格式
    enum AVSampleFormat in_sample_fmt = pAvCodecCtx->sample_fmt;
    //输出的采样格式 16bit PCM
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    //输入采样率
    int in_sample_rate = pAvCodecCtx->sample_rate;
    //输出的采样率
    int out_sample_rate = 44100;
    //获取输入的声道布局
    //可以根据声道个数获取默认的声道布局(2个声道，默认的是立体声stereo)
    //int64_t in_channel_layout = av_get_default_channel_layout(pAvCodecCtx->channels);
    uint64_t in_channel_layout = pAvCodecCtx->channel_layout;
    //输出的声道布局(立体声)
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    struct SwrContext *swrCtx = swr_alloc();
    swr_alloc_set_opts(swrCtx,
                       out_channel_layout, out_sample_fmt, out_sample_rate,
                       in_channel_layout, in_sample_fmt, in_sample_rate,
                       0, NULL);
    swr_init(swrCtx);
    //输出声道个数
    int out_channle_nb = av_get_channel_layout_nb_channels(out_channel_layout);
    //重采样设置参数结束
    FILE *fp_pcm = fopen(output_cstr, "wb");
    uint8_t *out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRAME_SIZE);

    int got_frame_ptr, len, index = 0;
    //读取一帧压缩数据
    while (av_read_frame(pAvFmtCtx, avPacket) == 0) {
        //注意只解析音频Packet
        if (avPacket->stream_index == audio_stream_index) {
            len = avcodec_decode_audio4(pAvCodecCtx, frame,
                                        &got_frame_ptr, avPacket);
            if (got_frame_ptr > 0) {
                //解压缩到一帧数据成功
                LOGE("解码：%d", index++);
                swr_convert(swrCtx, &out_buffer, MAX_AUDIO_FRAME_SIZE,
                            (const uint8_t **) frame->data, frame->nb_samples);
                //获取sample 的size
                int out_buffer_size = av_samples_get_buffer_size(NULL, out_channle_nb,
                                                                 frame->nb_samples, out_sample_fmt,
                                                                 1);
                fwrite(out_buffer, 1, static_cast<size_t>(out_buffer_size), fp_pcm);

            }
        }
        av_free_packet(avPacket);
    }
    LOGE("解码：%s", "解码完成");
    fclose(fp_pcm);
    av_frame_free(&frame);
    av_free(out_buffer);

    swr_free(&swrCtx);
    avcodec_close(pAvCodecCtx);
    avformat_close_input(&pAvFmtCtx);
    env->ReleaseStringUTFChars(input_jstr, input_cstr);
    env->ReleaseStringUTFChars(output_jstr, output_cstr);
}


extern "C"
JNIEXPORT void JNICALL
Java_com_demo_ffmpeg_VideoPlayer_decodeAndPlayAudio(JNIEnv *env, jclass jclazz,
                                                    jstring input_jstr) {
    const char *input_cstr = env->GetStringUTFChars(input_jstr, 0);
    //注册组件
    av_register_all();
    AVFormatContext *pAvFmtCtx = avformat_alloc_context();
    avformat_open_input(&pAvFmtCtx, input_cstr, NULL, NULL);
    avformat_find_stream_info(pAvFmtCtx, NULL);
    int audio_stream_index = -1;
    for (int i = 0; i < pAvFmtCtx->nb_streams; ++i) {
        if (pAvFmtCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    AVCodecContext *pAvCodecCtx = pAvFmtCtx->streams[audio_stream_index]->codec;
    //获取解码器
    AVCodec *pAvCodec = avcodec_find_decoder(pAvCodecCtx->codec_id);
    //打开解码器
    avcodec_open2(pAvCodecCtx, pAvCodec, NULL);

    AVPacket *avPacket = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    //重采样设置参数开始
    //输入的采样格式
    enum AVSampleFormat in_sample_fmt = pAvCodecCtx->sample_fmt;
    //输出的采样格式 16bit PCM
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    //输入采样率
    int in_sample_rate = pAvCodecCtx->sample_rate;
    //输出的采样率
    int out_sample_rate = 44100;
    //获取输入的声道布局
    //可以根据声道个数获取默认的声道布局(2个声道，默认的是立体声stereo)
    //int64_t in_channel_layout = av_get_default_channel_layout(pAvCodecCtx->channels);
    uint64_t in_channel_layout = pAvCodecCtx->channel_layout;
    //输出的声道布局(立体声)
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    struct SwrContext *swrCtx = swr_alloc();
    swr_alloc_set_opts(swrCtx,
                       out_channel_layout, out_sample_fmt, out_sample_rate,
                       in_channel_layout, in_sample_fmt, in_sample_rate,
                       0, NULL);
    swr_init(swrCtx);
    //输出声道个数
    int out_channle_nb = av_get_channel_layout_nb_channels(out_channel_layout);
    //重采样设置参数结束
    uint8_t *out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRAME_SIZE);

    //获取AudioTrack对象以及调用play write pause stop等方法的id 签名等
    //获取AudioTrack
    //获取VideoPlayer对象及其createAudioTrack方法
    jmethodID mid_create_audio_track = env->GetStaticMethodID(jclazz, "createAudioTrack",
                                                              "(II)Landroid/media/AudioTrack;");
    jobject audioTrack = env->CallStaticObjectMethod(jclazz, mid_create_audio_track,
                                                     out_sample_rate, out_channle_nb);
    audioTrack_gb_ref = env->NewGlobalRef(audioTrack);
    jclass audioTrack_jclss = env->GetObjectClass(audioTrack);
    //调用audioTrack.play()
    jmethodID mid_audio_track_play = env->GetMethodID(audioTrack_jclss, "play", "()V");
    env->CallVoidMethod(audioTrack, mid_audio_track_play);
    //获取int write(@NonNull byte[] audioData, int offsetInBytes, int sizeInBytes);
    jmethodID mid_audio_track_write = env->GetMethodID(audioTrack_jclss, "write", "([BII)I");
    int got_frame_ptr, len, index = 0;
    //读取一帧压缩数据
    while (av_read_frame(pAvFmtCtx, avPacket) == 0) {
        //注意只解析音频Packet
        if (avPacket->stream_index == audio_stream_index) {
            len = avcodec_decode_audio4(pAvCodecCtx, frame,
                                        &got_frame_ptr, avPacket);
            if (got_frame_ptr > 0) {
                //解压缩到一帧数据成功
                LOGE("解码：%d", index++);
                swr_convert(swrCtx, &out_buffer, MAX_AUDIO_FRAME_SIZE,
                            (const uint8_t **) frame->data, frame->nb_samples);
                //获取sample 的size
                int out_buffer_size = av_samples_get_buffer_size(NULL, out_channle_nb,
                                                                 frame->nb_samples, out_sample_fmt,
                                                                 1);
                //传递byte[] audioData, int offsetInBytes, int sizeInBytes
                //需要将byte数组转换为jbyteArray
                //将out_buffer缓冲区数据转换为byte数组(jbyteArray)
                jbyteArray out_buffer_array = env->NewByteArray(out_buffer_size);
                jbyte *p_sample_array = env->GetByteArrayElements(out_buffer_array, NULL);
                //将out_buffer中的数据复制到p_sample_array中
                memcpy(p_sample_array, out_buffer, static_cast<size_t>(out_buffer_size));
                //同步数组
                env->ReleaseByteArrayElements(out_buffer_array, p_sample_array, 0);
                //调用audioTrack.write方法,写入PCM数据
                env->CallIntMethod(audioTrack, mid_audio_track_write, out_buffer_array, 0,
                                   out_buffer_size);
                //释放局部引用(否则会造成Native内存溢出)
                env->DeleteLocalRef(out_buffer_array);
                //16毫秒
                usleep(1000 * 16);
            }
        }
        av_free_packet(avPacket);
    }
    LOGE("解码：%s", "解码完成");
    av_frame_free(&frame);
    av_free(out_buffer);

    swr_free(&swrCtx);
    avcodec_close(pAvCodecCtx);
    avformat_close_input(&pAvFmtCtx);

    env->ReleaseStringUTFChars(input_jstr, input_cstr);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_demo_ffmpeg_VideoPlayer_play(JNIEnv *env, jclass jclazz) {
    jclass audioTrack_jclss = env->GetObjectClass(audioTrack_gb_ref);
    //调用audioTrack.play()
    jmethodID mid_audio_track_play = env->GetMethodID(audioTrack_jclss, "play", "()V");
    env->CallVoidMethod(audioTrack_gb_ref, mid_audio_track_play);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_demo_ffmpeg_VideoPlayer_pause(JNIEnv *env, jclass jclazz) {
    jclass audioTrack_jclss = env->GetObjectClass(audioTrack_gb_ref);
    //调用audioTrack.pause()
    jmethodID mid_audio_track_pause = env->GetMethodID(audioTrack_jclss, "pause", "()V");
    env->CallVoidMethod(audioTrack_gb_ref, mid_audio_track_pause);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_demo_ffmpeg_VideoPlayer_stop(JNIEnv *env, jclass jclazz) {
    jclass audioTrack_jclss = env->GetObjectClass(audioTrack_gb_ref);
    //调用audioTrack.stop()
    jmethodID mid_audio_track_stop = env->GetMethodID(audioTrack_jclss, "stop", "()V");
    env->CallVoidMethod(audioTrack_gb_ref, mid_audio_track_stop);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_demo_ffmpeg_VideoPlayer_destroy(JNIEnv *env, jclass jclazz) {
    if (audioTrack_gb_ref != NULL) {
        env->DeleteGlobalRef(audioTrack_gb_ref);
    }
}

