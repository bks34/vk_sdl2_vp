//
// Created by heshaoquan on 25-7-28.
//

#include "FFmpegDecoder.h"

#include <array>
#include <iostream>
#include <map>
#include <SDL_cpuinfo.h>
#include <stdexcept>
#include <bits/ostream.tcc>


static std::map<SDL_AudioFormat, AVSampleFormat> AUDIO_FORMAT_MAP = {
    // AV_SAMPLE_FMT_NONE = -1,
    {AUDIO_U8, AV_SAMPLE_FMT_U8    },
    {AUDIO_S16SYS, AV_SAMPLE_FMT_S16},
    {AUDIO_S32SYS, AV_SAMPLE_FMT_S32},
    {AUDIO_F32SYS, AV_SAMPLE_FMT_FLT}
};

FFmpegDecoder::FFmpegDecoder(const std::string& filename, const SDL_AudioSpec& audio_spec, bool replay) {
    this->filename = filename;
    this->replay = replay;
    videoDecoder.setMaxFrameSize(5);
    audioDecoder.setMaxFrameSize(10);

    bool hasVideo = false, hasAudio = false;

    // alloc AVFormatCtx
    pFormatCtx = avformat_alloc_context();

    // open file
    if (avformat_open_input(&pFormatCtx, filename.data(), nullptr, nullptr)) {
        throw std::runtime_error("Couldn't open file" + filename);
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        avformat_close_input(&pFormatCtx);
        throw std::runtime_error("Couldn't find stream info");
    }

    duration = pFormatCtx->duration / AV_TIME_BASE;

    // find video and audio stream
    videoIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audioIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    hasVideo = videoIndex >= 0;
    hasAudio = audioIndex >= 0;
    if (!(hasVideo || hasAudio)) {
        throw std::runtime_error("Couldn't find video or audio stream");
    }

    if (hasVideo) {
        if (pFormatCtx->streams[videoIndex]->attached_pic.size > 0) {
            videoIsCover = true;
        }
        fps_den = pFormatCtx->streams[videoIndex]->r_frame_rate.den;
        fps_num = pFormatCtx->streams[videoIndex]->r_frame_rate.num;
        if (!videoIsCover) {
            fps = static_cast<double>(fps_num) / fps_den;
        } else {
            fps = 30;
        }

        // Video Codec Context
        videoDecoder.pAVCtx = avcodec_alloc_context3(nullptr);
        videoDecoder.pAVCtx->thread_count = 2;
        videoDecoder.pAVCtx->thread_type = FF_THREAD_FRAME;
        avcodec_parameters_to_context(videoDecoder.pAVCtx, pFormatCtx->streams[videoIndex]->codecpar);

        // Video Codec
        const AVCodec* pVideoCodec = avcodec_find_decoder(videoDecoder.pAVCtx->codec_id);
        if (pVideoCodec == nullptr) {
            avcodec_free_context(&videoDecoder.pAVCtx);
            avformat_close_input(&pFormatCtx);
            throw std::runtime_error("Couldn't find video decoder");
        }

        // open video codec
        if (avcodec_open2(videoDecoder.pAVCtx, pVideoCodec, nullptr) < 0) {
            avcodec_free_context(&videoDecoder.pAVCtx);
            avformat_close_input(&pFormatCtx);
            throw std::runtime_error("Couldn't open video decoder");
        }

        width = videoDecoder.pAVCtx->width;
        height = videoDecoder.pAVCtx->height;
        pSwsCtx = sws_getContext(width, height, videoDecoder.pAVCtx->pix_fmt,
            width, height, AV_PIX_FMT_RGBA,
            SWS_BICUBIC, nullptr, nullptr, nullptr);

    }

    if (hasAudio) {
        audioClock.sample_rate = audio_spec.freq;
        // Audio Codec Context
        audioDecoder.pAVCtx = avcodec_alloc_context3(nullptr);
        avcodec_parameters_to_context(audioDecoder.pAVCtx, pFormatCtx->streams[audioIndex]->codecpar);

        // Audio Codec
        const AVCodec* pAudioCodec = avcodec_find_decoder(audioDecoder.pAVCtx->codec_id);
        if (pAudioCodec == nullptr) {
            avcodec_free_context(&audioDecoder.pAVCtx);
            avformat_close_input(&pFormatCtx);
            throw std::runtime_error("Couldn't find audio decoder");
        }

        // open audio codec
        if (avcodec_open2(audioDecoder.pAVCtx, pAudioCodec, nullptr) < 0) {
            avcodec_free_context(&audioDecoder.pAVCtx);
            avformat_close_input(&pFormatCtx);

            throw std::runtime_error("Couldn't open audio decoder");
        }

        // about SwrContext
        audioSrc.sampleFormat = audioDecoder.pAVCtx->sample_fmt;
        audioSrc.freq = audioDecoder.pAVCtx->sample_rate;
        audioSrc.channelLayout = audioDecoder.pAVCtx->ch_layout;

        audioDst.sampleFormat = AUDIO_FORMAT_MAP[audio_spec.format];
        audioDst.freq = audio_spec.freq;
        av_channel_layout_default(&audioDst.channelLayout, audio_spec.channels);

        if (swr_alloc_set_opts2(&pSwrCtx, &audioDst.channelLayout, audioDst.sampleFormat, audioDst.freq,
            &audioSrc.channelLayout, audioSrc.sampleFormat, audioSrc.freq, 0, nullptr
        )) {
            throw std::runtime_error("Couldn't allocate swrContext");
        }

        swr_init(pSwrCtx);
    }
}

void FFmpegDecoder::run() {
    readPacketThread = std::thread(&FFmpegDecoder::readPacket, this);
    readPacketThread.detach();
}

void FFmpegDecoder::stop() {
    running = false;
}

void FFmpegDecoder::pause() {
    paused = !paused;
}

bool FFmpegDecoder::isStopped() {
    return stopped;
}

std::shared_ptr<FFmpegDecoder::Frame> FFmpegDecoder::getVideoFrame() {
    std::shared_ptr<FFmpegDecoder::Frame> frame;

    if (videoDecoder.frameQueue.size() > 1) {
        videoDecoder.frameQueue.pop(frame);
    } else {
        videoDecoder.frameQueue.front(frame);
    }

    return  frame;
}

std::shared_ptr<FFmpegDecoder::Frame> FFmpegDecoder::getAudioFrame() {
    std::shared_ptr<FFmpegDecoder::Frame> frame;

    audioDecoder.frameQueue.pop(frame);

    return frame;
}

bool FFmpegDecoder::isVideo() {
    return !videoIsCover;
}

bool FFmpegDecoder::hasAudio() {
    return audioIndex >= 0;
}

double FFmpegDecoder::getFps() {
    if (videoIsCover) {
        return 0.0;
    }
    return fps;
}

double FFmpegDecoder::getDeltaTime() {
    return double(fps_den) / double(fps_num);
}

double FFmpegDecoder::getRelativeTime() {
    double curTime = 0.0;
    if (audioIndex >= 0) {
        curTime = clock.audioTime;
    } else {
        curTime = clock.videoTime;
    }
    return curTime;
}

double FFmpegDecoder::getDuration() {
    return duration;
}

void FFmpegDecoder::seekTime(double time) {
    seekReq = true;
    seekReqTime = time;
}

std::array<int, 2> FFmpegDecoder::getVideoSize() {
    return std::array<int, 2>{width, height};
}

void FFmpegDecoder::setAudioSpec(SDL_AudioSpec audio_spec) {
    audioDst.sampleFormat = AUDIO_FORMAT_MAP[audio_spec.format];
    audioDst.freq = audio_spec.freq;
    av_channel_layout_default(&audioDst.channelLayout, audio_spec.channels);
}

bool FFmpegDecoder::audioFrameReady() {
    return audioDecoder.frameQueue.size() > 0;
}

void FFmpegDecoder::updateAudioClock(int lens, int64_t newFrame) {
    if (newFrame) {
        audioClock.pts = newFrame;
    } else {
        static double temp = (audioDst.channelLayout.nb_channels *av_get_bytes_per_sample(audioDst.sampleFormat)) * audioDst.freq * av_q2d(pFormatCtx->streams[audioIndex]->time_base);
        audioClock.pts += (double) lens / temp;
    }
}

int64_t FFmpegDecoder::getAudioTimePts() {
    return audioClock.pts;
}

double FFmpegDecoder::getDelay(int64_t videoPts) {
    return videoPts * av_q2d(pFormatCtx->streams[videoIndex]->time_base) - audioClock.pts * av_q2d(pFormatCtx->streams[audioIndex]->time_base);
}


void FFmpegDecoder::readPacket() {
    if (audioIndex >= 0) {
        audioDecoder.decodeThread = std::thread(&FFmpegDecoder::audioDecode, this);
        audioDecoder.threadRunning = true;
        audioDecoder.decodeThread.detach();
    } else {
        audioDecoder.threadStopped = true;
    }
    if (videoIndex >= 0) {
        videoDecoder.decodeThread = std::thread(&FFmpegDecoder::videoDecode, this);
        videoDecoder.threadRunning = true;
        videoDecoder.decodeThread.detach();
    } else {
        videoDecoder.threadStopped = true;
    }


    while (running) {
        while (paused) {}
        if (seekReq) {
            double curTime = 0.0;
            if (audioIndex >= 0) {
                curTime = clock.audioTime;
            } else {
                curTime = clock.videoTime;
            }
            int64_t targetTs = (curTime + seekReqTime > duration) ?
                (duration - 0.5) * AV_TIME_BASE : (curTime + seekReqTime) * AV_TIME_BASE;

            if (int ret; (ret = avformat_seek_file(pFormatCtx, -1, INT64_MIN, targetTs, INT64_MAX, AVSEEK_FLAG_BACKWARD) < 0)) {
                char error[64];
                std::cout << "failed to seek time: " << av_make_error_string(error, sizeof(error), ret) << std::endl;
            } else {
                std::cout << "seeked " << seekReqTime << " s. " << std::endl;

                if (videoIndex >= 0) {
                    if (!videoIsCover) {
                        videoDecoder.packetQueue.clear();
                        mutexVideoCodec.lock();
                        avcodec_flush_buffers(videoDecoder.pAVCtx);
                        mutexVideoCodec.unlock();
                        videoDecoder.frameQueue.clear();
                    }
                }
                if (audioIndex >= 0) {
                    audioDecoder.packetQueue.clear();
                    mutexAudioCodec.lock();
                    avcodec_flush_buffers(audioDecoder.pAVCtx);
                    mutexAudioCodec.unlock();
                    audioDecoder.frameQueue.clear();
                }
            }
            if (curTime + seekReqTime > duration) {
                std::cout << "audioTime: " << duration << " s. " << std::endl;
                std::cout << "videoTime: " << duration << " s. " << std::endl;
                std::cout << std::endl;
            } else {
                std::cout << "audioTime: " << clock.audioTime + seekReqTime << " s. " << std::endl;
                std::cout << "videoTime: " << clock.videoTime + seekReqTime << " s. " << std::endl;
                std::cout << std::endl;
            }
            seekReq = false;
            seekReqTime = 0.0;
        }

        AVPacket* pAVpkt = av_packet_alloc();
        if (av_read_frame(pFormatCtx, pAVpkt) < 0) {
            if (replay) {
                double curTime = 0.0;
                if (audioIndex >= 0) {
                    curTime = clock.audioTime;
                } else {
                    curTime = clock.videoTime;
                }
                seekTime(-curTime);
                av_packet_unref(pAVpkt);
                std::cout << "Play again" << std::endl;
                continue;
            }
            std::cout << "Playback finished" << std::endl;
            av_packet_unref(pAVpkt);
            break;
        }
        std::shared_ptr<Packet> packet = std::make_shared<Packet>();
        packet->data = pAVpkt;
        if (pAVpkt->stream_index == videoIndex) {
            while (videoDecoder.packetQueue.full()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!running) {
                    break;
                }
            }
            videoDecoder.packetQueue.push(packet);
        } else if (pAVpkt->stream_index == audioIndex) {
            while (audioDecoder.packetQueue.full()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!running) {
                    break;
                }
            }
            audioDecoder.packetQueue.push(packet);
        }
    }

    videoDecoder.threadRunning = false;
    audioDecoder.threadRunning = false;

    while (!videoDecoder.threadStopped || !audioDecoder.threadStopped) {}

    avformat_close_input(&pFormatCtx);

    audioDecoder.packetQueue.clear();
    videoDecoder.packetQueue.clear();

    videoDecoder.frameQueue.clear();
    audioDecoder.frameQueue.clear();

    stopped = true;
    std::printf("FFmpegDecoder exiting...\n");
}

void FFmpegDecoder::videoDecode() {
    AVFrame* pAVframe = av_frame_alloc();
    while (videoDecoder.threadRunning) {
        while (paused) {}
        std::shared_ptr<Packet> pPacket;
        while (!videoDecoder.packetQueue.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (videoDecoder.threadRunning) {
                break;
            }
        }
        videoDecoder.packetQueue.pop(pPacket);

        mutexVideoCodec.lock();
        int ret = avcodec_send_packet(videoDecoder.pAVCtx, pPacket->data);
        int gotFrame = avcodec_receive_frame(videoDecoder.pAVCtx, pAVframe);
        mutexVideoCodec.unlock();


        if (ret < 0) {
            std::cout << "error during avcodec_send_packet" << std::endl;
            continue;
        }

        if (gotFrame == 0) {
            // get time
            if (pAVframe->pts == AV_NOPTS_VALUE) {
                if (pPacket->data->pts == AV_NOPTS_VALUE) {
                    // use frame rate
                    static double lastTime = 0.0;
                    clock.videoTime = lastTime + 1.0 * av_q2d(pFormatCtx->streams[videoIndex]->avg_frame_rate);
                    lastTime = clock.videoTime;
                } else {
                    clock.videoPts = pAVframe->pkt_dts;
                    clock.videoTime = (double) pAVframe->pkt_dts * av_q2d(pFormatCtx->streams[videoIndex]->time_base);
                }
            } else {
                clock.videoPts = pAVframe->pts;
                clock.videoTime = (double) pAVframe->pts * av_q2d(pFormatCtx->streams[videoIndex]->time_base);
            }

            AVFrame* pAVframeRGB = av_frame_alloc();
            pAVframeRGB->width = width;
            pAVframeRGB->height = height;
            pAVframeRGB->format = AV_PIX_FMT_RGBA;
            if (!av_image_alloc(pAVframeRGB->data, pAVframeRGB->linesize, width, height, AV_PIX_FMT_RGB32, 1)) {
                throw std::runtime_error("Couldn't allocate pixel buffer for pAVframeRGB");
            }

            if (!sws_scale(pSwsCtx, pAVframe->data, pAVframe->linesize, 0,
                height, pAVframeRGB->data, pAVframeRGB->linesize)) {
                throw std::runtime_error("Convert to RGB32 error!");
                }
            std::shared_ptr<Frame> frame = std::make_shared<Frame>();
            frame->data = pAVframeRGB;
            frame->videoPts = clock.videoPts;

            // push to queue
            while (videoDecoder.frameQueue.full()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!videoDecoder.threadRunning) {
                    break;
                }
            }
            videoDecoder.frameQueue.push(frame);

            if (videoIsCover) {
                break;
            }
        }
    }

    if (videoIsCover) {
        while (videoDecoder.threadRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    av_frame_free(&pAVframe);
    sws_freeContext(pSwsCtx);
    avcodec_flush_buffers(videoDecoder.pAVCtx);
    avcodec_free_context(&videoDecoder.pAVCtx);

    videoDecoder.threadStopped = true;
}

void FFmpegDecoder::audioDecode() {
    AVFrame* pAVframe = av_frame_alloc();
    while (audioDecoder.threadRunning) {
        while (paused) {}
        std::shared_ptr<Packet> pPacket;
        while (!audioDecoder.packetQueue.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (audioDecoder.threadRunning) {
                break;
            }
        }
        audioDecoder.packetQueue.pop(pPacket);

        mutexAudioCodec.lock();
        int ret = avcodec_send_packet(audioDecoder.pAVCtx, pPacket->data);
        int gotFrame = avcodec_receive_frame(audioDecoder.pAVCtx, pAVframe);
        mutexAudioCodec.unlock();

        if (ret < 0) {
            std::cout << "error during avcodec_send_packet" << std::endl;
            continue;
        }

        if (gotFrame == 0) {
            // get time
            if (pAVframe->pts != AV_NOPTS_VALUE) {
                clock.audioPts = pAVframe->pts;
                clock.audioTime = (double) pAVframe->pts * av_q2d(pFormatCtx->streams[audioIndex]->time_base);
            }

            // Estimated sample size and buffer size
            int outSamples = swr_get_out_samples(pSwrCtx, pAVframe->nb_samples);
            int outBufferSize = av_samples_get_buffer_size(
                nullptr, audioDst.channelLayout.nb_channels, outSamples,
                audioDst.sampleFormat, 1
                );

            uint8_t* outBuffer = (uint8_t*)av_malloc(outBufferSize);

            // Real sample size and buffer size
            int convertedSamples = swr_convert(pSwrCtx,
                &outBuffer, outSamples,
                const_cast<const uint8_t **>(pAVframe->data), pAVframe->nb_samples
                );
            if (convertedSamples < 0) {
                std::cout << "swr_convert error!" << std::endl;
                continue;
            }
            outBufferSize = av_samples_get_buffer_size(
                nullptr, audioDst.channelLayout.nb_channels, convertedSamples,
                audioDst.sampleFormat, 1
                );

            std::shared_ptr<Frame> frame = std::make_shared<Frame>();
            frame->audioData = outBuffer;
            frame->audioBufferSize = outBufferSize;
            frame->audioPts = clock.audioPts;

            // push to queue
            while (audioDecoder.frameQueue.full()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!audioDecoder.threadRunning) {
                    break;
                }
            }
            audioDecoder.frameQueue.push(frame);
        }
    }

    av_frame_free(&pAVframe);
    swr_free(&pSwrCtx);
    avcodec_flush_buffers(audioDecoder.pAVCtx);
    avcodec_free_context(&audioDecoder.pAVCtx);

    audioDecoder.threadStopped = true;
}
