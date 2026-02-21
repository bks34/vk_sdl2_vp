//
// Created by heshaoquan on 25-7-28.
//

#ifndef FFMPEGDECODER_H
#define FFMPEGDECODER_H

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
}
#include <thread>
#include <string>
#include <atomic>
#include <condition_variable>
#include <SDL2/SDL_audio.h>
#include "ThreadSafeQueue.h"

class FFmpegDecoder {
public:
    explicit FFmpegDecoder(const std::string& filename, const SDL_AudioSpec& audio_spec, bool replay);

    void run();

    void stop();

    void pause();

    bool isStopped();

    struct Frame {
        ~Frame() {
            this->free();
        }

        void free() {
            if (data) {
                av_freep(&data->data[0]);
                av_frame_free(&data);
            }
        }

        AVFrame* data = nullptr;
        uint8_t* audioData = nullptr;
        int audioBufferSize = 0;
        int64_t videoPts = 0;
        int64_t audioPts = 0;
    };

    std::shared_ptr<Frame> getVideoFrame();

    std::shared_ptr<Frame> getAudioFrame();

    bool isVideo();

    bool hasAudio();

    double getFps();

    double getDeltaTime();

    double getRelativeTime();

    double getDuration();

    void seekTime(double time);

    std::array<int, 2> getVideoSize();

    void setAudioSpec(SDL_AudioSpec audio_spec);

    // audio clock, for sync
    struct AudioClock {
        std::atomic<int64_t> pts = 0;
        int sample_rate = 1;
    };
    AudioClock audioClock;
    void updateAudioClock(int lens, int64_t newFrame);

    int64_t getAudioTimePts();
    double getDelay(int64_t videoPts);
private:
    std::string filename;
    double duration;
    double fps;
    bool replay;
    AVFormatContext* pFormatCtx;
    int videoIndex = -1, audioIndex = -1;
    bool videoIsCover = false;

    std::thread readPacketThread;

    std::atomic<bool> running = true;
    std::atomic<bool> paused = false;

    std::atomic<bool> stopped = false;

    // clock for seek
    struct Clock {
        int64_t audioPts = 0;
        int64_t videoPts = 0;

        double audioTime = 0.0;
        double videoTime = 0.0;
    };
    Clock clock;

    // seek
    std::atomic<bool> seekReq = false;
    double seekReqTime = 0.0;
    std::mutex mutexVideoCodec;
    std::mutex mutexAudioCodec;

    // decoder
    struct Packet {
        ~Packet() {
            if (data) {
                av_packet_unref(data);
            }
        }
        AVPacket* data = nullptr;
    };

    struct DecoderInfo {
        void setMaxFrameSize(const size_t maxFrameSize) {
            maxFrameQueueSize = maxFrameSize;
            frameQueue.set_max_size(maxFrameQueueSize);
        }
        ThreadSafeQueue<std::shared_ptr<Packet>> packetQueue;
        AVCodecContext* pAVCtx = nullptr;
        ThreadSafeQueue<std::shared_ptr<Frame>> frameQueue;
        size_t maxFrameQueueSize = 0;
        std::thread decodeThread;
        std::atomic<bool> threadRunning = false;
        std::atomic<bool> threadStopped = false;
    };

    DecoderInfo videoDecoder;
    DecoderInfo audioDecoder;

    // data about audio stream
    SwrContext* pSwrCtx = nullptr;
    struct AudioParams {
        int freq;
        AVChannelLayout channelLayout;
        AVSampleFormat sampleFormat;
    };
    AudioParams audioSrc{}, audioDst{};

    // data about video stream
    SwsContext* pSwsCtx = nullptr;
    int64_t numOfSubmittedVideoFrames = 0;
    int64_t numOfDecodedVideoFrames = 0;
    int fps_den, fps_num;
    int width, height;


    void readPacket();
    void videoDecode();
    void audioDecode();
};



#endif //FFMPEGDECODER_H
