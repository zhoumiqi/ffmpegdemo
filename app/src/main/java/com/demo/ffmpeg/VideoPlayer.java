package com.demo.ffmpeg;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.support.annotation.NonNull;
import android.view.Surface;

public class VideoPlayer {
    static {
        System.loadLibrary("avutil");
        System.loadLibrary("swresample");
        System.loadLibrary("avcodec");
        System.loadLibrary("avformat");
        System.loadLibrary("swscale");
        System.loadLibrary("avfilter");
        System.loadLibrary("avdevice");
        System.loadLibrary("myffmpeg");
    }

    public static AudioTrack createAudioTrack(int sampleRateInHz, int nb_channels) {
        int channelConfig;
        if (nb_channels == 1) {
            channelConfig = AudioFormat.CHANNEL_OUT_MONO;
        } else if (nb_channels == 2) {
            channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
        } else {
            channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
        }
        int audioFormat = AudioFormat.ENCODING_PCM_16BIT;
        int bufferSizeInBytes = AudioTrack.getMinBufferSize(sampleRateInHz, channelConfig, audioFormat);
        //        audioTrack.play();
//        audioTrack.write(byte[] audioData, int offsetInBytes, int sizeInBytes)
//        audioTrack.pause();
//        audioTrack.stop();
        return new AudioTrack(AudioManager.STREAM_MUSIC, sampleRateInHz, channelConfig, audioFormat,
                bufferSizeInBytes, AudioTrack.MODE_STREAM);
    }

    public int write(@NonNull byte[] audioData, int offsetInBytes, int sizeInBytes) {
        return 1;
    }

    public native static String decode(String input, String output);

    public native static void decodeVideo(String input, String output);

    public native static void decodeAndDrawVideo(String input, Surface surface);

    public native static void decodeAudio(String input, String output);

    public native static void decodeAndPlayAudio(String input);

    public native static void play();

    public native static void pause();

    public native static void stop();

    public native static void destroy();

}
