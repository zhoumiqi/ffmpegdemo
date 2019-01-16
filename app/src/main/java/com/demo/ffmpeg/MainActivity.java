package com.demo.ffmpeg;

import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;

import java.io.File;

public class MainActivity extends AppCompatActivity {
    private VideoView videoView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        videoView = findViewById(R.id.video_view);
    }
    public void mDecode(View view) {
        String input = new File(Environment.getExternalStorageDirectory(),"input.mp4").getAbsolutePath();
        String output = new File(Environment.getExternalStorageDirectory(),"output_1280x720_yuv420p.yuv").getAbsolutePath();
//        String config = VideoPlayer.decode(input, output);
//        VideoPlayer.decodeVideo(input, output);
        VideoPlayer.decodeAndDrawVideo(input,videoView.getHolder().getSurface());
//        Log.e("zmq",config);
    }

    public static void main(String[] args){
        int[] nums ={2,7,19,33,5};
        int[] result = twoSum(nums,9);
        System.out.println("结果为:"+result[0]+","+result[1]);
    }
    public static int[] twoSum(int[] nums, int target) {
        for(int i = 0;i<nums.length;i++){
            for(int j=i+1;j<nums.length;j++){
                if(nums[i]+nums[j]==target){
                    return new int[]{i,j};
                }
            }
        }
        return null;
    }

    public void decodeAudio(View view) {
        String input = new File(Environment.getExternalStorageDirectory(),"input.mp4").getAbsolutePath();
        String output = new File(Environment.getExternalStorageDirectory(),"output.pcm").getAbsolutePath();
        VideoPlayer.decodeAudio(input,output);
    }

    public void decodeAudioAndPlayByAudioTrack(View view) {
        String input = new File(Environment.getExternalStorageDirectory(),"input.mp4").getAbsolutePath();
        VideoPlayer.decodeAndPlayAudio(input);
    }

    public void play(View view) {
        VideoPlayer.play();
    }

    public void pause(View view) {
        VideoPlayer.pause();
    }

    public void stop(View view) {
        VideoPlayer.stop();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        VideoPlayer.destroy();
    }
}
