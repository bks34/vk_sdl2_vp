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
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
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
    explicit FFmpegDecoder(const std::string& filename, const SDL_AudioSpec& audio_spec,
                            bool replay, const std::string& hwAccel = "auto");

    void run();

    void stop();

    void pause();

    bool isStopped();

    struct Frame {
        ~Frame() { if (data) av_frame_free(&data); }
        AVFrame* data = nullptr;
        uint8_t* audioData = nullptr;
        int audioBufferSize = 0;
        int64_t videoPts = 0;
        int64_t audioPts = 0;
    };

    bool videoFrameReady();

    std::shared_ptr<Frame> getVideoFrame();

    std::shared_ptr<Frame> getAudioFrame();

    bool isVideo();

    bool hasAudio();

    std::string getVideoCodecName() const;

    std::string getAudioCodecName() const;

    // 0 = BT.601, 1 = BT.709, 2 = BT.2020  (matches shader push constant)
    int getColorStandard() const;
    // 0 = Limited (TV), 1 = Full (PC)       (matches shader push constant)
    int getColorRange() const;

    double getFps();

    double getDeltaTime();

    double getRelativeTime();

    double getDuration();

    void seekTime(double time);

    // get_format callback (FFmpeg official example pattern)
    static AVPixelFormat getHwFormat(AVCodecContext* ctx, const AVPixelFormat* pix_fmts);

    std::array<int, 2> getVideoSize();

    // about sdl2 audio
    void setAudioSpec(SDL_AudioSpec audio_spec);

    bool audioFrameReady();

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

    AVRational audioTimeBase;
    AVRational videoTimeBase;

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
        std::string codecName;
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
    SwsContext* pSwsCtx = nullptr;     // software path
    SwsContext* hwSwsCtx = nullptr;    // hardware GPU→NV12 conversion
    int hwSwsSrcFmt = -1;
    int fps_den, fps_num;
    int width, height;


    bool tryInitHwDecoder();

    void readPacket();
    void videoDecode();
    void audioDecode();

    // hardware decoding support
    std::string hwAccelPref;
    AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;
    AVBufferRef* hw_device_ctx = nullptr;
    AVBufferRef* hw_frames_ref = nullptr;
    bool useHwDecode = false;
};



#endif //FFMPEGDECODER_H
