//
// Created by heshaoquan on 25-7-28.
//

#include "FFmpegDecoder.h"

#include <array>
#include <cctype>
#include <iostream>
#include <map>
#include <SDL_cpuinfo.h>
#include <stdexcept>




static std::map<SDL_AudioFormat, AVSampleFormat> AUDIO_FORMAT_MAP = {
    // AV_SAMPLE_FMT_NONE = -1,
    {AUDIO_U8, AV_SAMPLE_FMT_U8    },
    {AUDIO_S16SYS, AV_SAMPLE_FMT_S16},
    {AUDIO_S32SYS, AV_SAMPLE_FMT_S32},
    {AUDIO_F32SYS, AV_SAMPLE_FMT_FLT}
};

FFmpegDecoder::FFmpegDecoder(const std::string& filename, const SDL_AudioSpec& audio_spec,
                             bool replay, const std::string& hwAccel) {
    this->filename = filename;
    this->replay = replay;
    this->hwAccelPref = hwAccel;
    this->width = 600;
    this->height = 600;
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

	// for debug
    //audioIndex = -1;
	//hasAudio = false;
    //

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

        videoTimeBase = pFormatCtx->streams[videoIndex]->time_base;

        // Video Codec Context
        width = pFormatCtx->streams[videoIndex]->codecpar->width;
		height = pFormatCtx->streams[videoIndex]->codecpar->height;

        videoDecoder.pAVCtx = avcodec_alloc_context3(nullptr);

        videoDecoder.pAVCtx->thread_count = 0;
        videoDecoder.pAVCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        avcodec_parameters_to_context(videoDecoder.pAVCtx, pFormatCtx->streams[videoIndex]->codecpar);

        // Video Codec
        const AVCodec* pVideoCodec = avcodec_find_decoder(videoDecoder.pAVCtx->codec_id);
        if (pVideoCodec == nullptr) {
            avcodec_free_context(&videoDecoder.pAVCtx);
            avformat_close_input(&pFormatCtx);
            throw std::runtime_error("Couldn't find video decoder");
        }
        videoDecoder.codecName = std::string(pVideoCodec->name);

        // open video codec
        if (avcodec_open2(videoDecoder.pAVCtx, pVideoCodec, nullptr) < 0) {
            avcodec_free_context(&videoDecoder.pAVCtx);
            avformat_close_input(&pFormatCtx);
            throw std::runtime_error("Couldn't open video decoder");
        }

        pSwsCtx = sws_getContext(width, height, videoDecoder.pAVCtx->pix_fmt,
            width, height, AV_PIX_FMT_NV12,
            SWS_BICUBIC, nullptr, nullptr, nullptr);

    }

    if (hasAudio) {
        audioClock.sample_rate = audio_spec.freq;
        audioTimeBase = pFormatCtx->streams[audioIndex]->time_base;

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

        audioDecoder.codecName = std::string(pAudioCodec->name);

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

    tryInitHwDecoder();
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

bool FFmpegDecoder::videoFrameReady() {
    return videoDecoder.frameQueue.size() > 0;
}

std::shared_ptr<FFmpegDecoder::Frame> FFmpegDecoder::getVideoFrame() {
    std::shared_ptr<FFmpegDecoder::Frame> frame;

    if (videoIsCover) {
        videoDecoder.frameQueue.front(frame);
    } else {
        videoDecoder.frameQueue.pop(frame);
    }
    return  frame;
}

std::shared_ptr<FFmpegDecoder::Frame> FFmpegDecoder::getAudioFrame() {
    std::shared_ptr<FFmpegDecoder::Frame> frame;

    audioDecoder.frameQueue.pop(frame);

    return frame;
}

bool FFmpegDecoder::isVideo() {
    return videoIndex >= 0 && !videoIsCover;
}

bool FFmpegDecoder::hasAudio() {
    return audioIndex >= 0;
}

std::string FFmpegDecoder::getVideoCodecName() const {
    return videoDecoder.codecName;
}

int FFmpegDecoder::getColorStandard() const {
    if (videoIndex < 0) return 0;
    auto cs = pFormatCtx->streams[videoIndex]->codecpar->color_space;
    // AVCOL_SPC_BT709=1, BT470BG=5, SMPTE170M=6, BT2020_NCL=9, BT2020_CL=10
    if (cs == AVCOL_SPC_BT2020_NCL || cs == AVCOL_SPC_BT2020_CL) return 2;
    if (cs == AVCOL_SPC_BT709) return 1;
    return 0;  // BT.601 default
}

int FFmpegDecoder::getColorRange() const {
    if (videoIndex < 0) return 0;
    // AVCOL_RANGE_JPEG = 2 (full range), AVCOL_RANGE_MPEG = 1 (limited)
    auto cr = pFormatCtx->streams[videoIndex]->codecpar->color_range;
    return (cr == AVCOL_RANGE_JPEG) ? 1 : 0;
}

std::string FFmpegDecoder::getAudioCodecName() const {
    return audioDecoder.codecName;
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
        static double temp = (audioDst.channelLayout.nb_channels *av_get_bytes_per_sample(audioDst.sampleFormat)) * audioDst.freq * av_q2d(audioTimeBase);
        audioClock.pts += (double) lens / temp;
    }
}

int64_t FFmpegDecoder::getAudioTimePts() {
    return audioClock.pts;
}

double FFmpegDecoder::getDelay(int64_t videoPts) {
    return videoPts * av_q2d(videoTimeBase) - audioClock.pts * av_q2d(audioTimeBase);
}

// get_format callback — tells the software decoder which hardware pixel
// format to use.  Modeled after FFmpeg's official hw_decode.c example.
AVPixelFormat FFmpegDecoder::getHwFormat(AVCodecContext* ctx,
                                          const AVPixelFormat* pix_fmts) {
    auto* self = static_cast<FFmpegDecoder*>(ctx->opaque);
    for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == self->hw_pix_fmt_) return *p;
    }
    std::fprintf(stderr, "[hw] getHwFormat: hw format not offered by decoder\n");
    return AV_PIX_FMT_NONE;
}

// Open a codec context.  If hwDevice/hwFrames are provided the codec will
// use hardware acceleration (either a dedicated hw codec or the software
// codec with the get_format callback set).
static bool openCodec(AVCodecContext*& ctx, const AVCodec* codec,
                       AVFormatContext* fmt, int streamIdx,
                       AVBufferRef* hwDevice, AVBufferRef* hwFrames,
                       FFmpegDecoder* self) {
    avcodec_free_context(&ctx);
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    avcodec_parameters_to_context(ctx, fmt->streams[streamIdx]->codecpar);
    if (hwDevice) {
        ctx->hw_device_ctx = av_buffer_ref(hwDevice);
        ctx->opaque = self;
        ctx->get_format = FFmpegDecoder::getHwFormat;
        // Disable frame threading when using hwaccel — the GPU does the
        // heavy lifting, and frame-worker threads can release hw-frame
        // references prematurely, leading to double-free on exit.
        ctx->thread_count = 1;
        ctx->thread_type  = 0;
    } else {
        ctx->thread_count = 0;
        ctx->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    return avcodec_open2(ctx, codec, nullptr) >= 0;
}

bool FFmpegDecoder::tryInitHwDecoder() {
    if (videoIndex < 0 || videoIsCover) return false;

    if (hwAccelPref == "none") {
        std::printf("Hardware acceleration disabled by user.\n");
        return false;
    }

    const AVCodec* swCodec =
        avcodec_find_decoder(pFormatCtx->streams[videoIndex]->codecpar->codec_id);
    if (!swCodec) return false;

    std::string pref = hwAccelPref;
    for (char& c : pref) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

    struct HwBackend {
        const char* name;
        AVHWDeviceType type;
        AVPixelFormat hwFmt;
        const char* codecSfx;
        const char* deviceOpt;
    };
    const HwBackend backends[] = {
        { "VAAPI", AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI, "vaapi", nullptr },
        { "CUDA",  AV_HWDEVICE_TYPE_CUDA,  AV_PIX_FMT_CUDA,  "nvdec", "0"     },
    };

    for (const auto& b : backends) {
        if (pref != "auto") {
            std::string key(b.name);
            for (char& c : key) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            if (pref != key) continue;
        }

        const int oldLog = av_log_get_level();
        av_log_set_level(AV_LOG_QUIET);

        // ---- 1. create hardware device --------------------------------------------
        AVDictionary* opts = nullptr;
        if (b.deviceOpt) av_dict_set(&opts, "device", b.deviceOpt, 0);
        int ret = av_hwdevice_ctx_create(&hw_device_ctx, b.type, nullptr, opts, 0);
        av_dict_free(&opts);

        if (ret < 0) {
            std::printf("[hw probe] %-8s — device unavailable\n", b.name);
            av_log_set_level(oldLog);
            continue;
        }

        // ---- 2. validate hw config (official example pattern) ---------------------
        hw_pix_fmt_ = AV_PIX_FMT_NONE;
        for (int i = 0; ; i++) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(swCodec, i);
            if (!cfg) break;
            if (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                cfg->device_type == b.type) {
                hw_pix_fmt_ = cfg->pix_fmt;
                break;
            }
        }
        if (hw_pix_fmt_ == AV_PIX_FMT_NONE) {
            std::printf("[hw probe] %-8s — no hwaccel config for decoder '%s'\n",
                        b.name, swCodec->name);
            av_buffer_unref(&hw_device_ctx);
            av_log_set_level(oldLog);
            continue;
        }

        // ---- 3. create hardware frames context ------------------------------------
        hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
        if (!hw_frames_ref) {
            av_buffer_unref(&hw_device_ctx);
            av_log_set_level(oldLog);
            continue;
        }
        auto* fctx = reinterpret_cast<AVHWFramesContext*>(hw_frames_ref->data);
        fctx->format            = hw_pix_fmt_;
        fctx->sw_format         = AV_PIX_FMT_NV12;
        fctx->width             = width;
        fctx->height            = height;
        fctx->initial_pool_size = 20;
        if (av_hwframe_ctx_init(hw_frames_ref) < 0) {
            std::printf("[hw probe] %-8s — frames-ctx init failed\n", b.name);
            av_buffer_unref(&hw_frames_ref);
            av_buffer_unref(&hw_device_ctx);
            av_log_set_level(oldLog);
            continue;
        }

        // ---- 4. open codec --------------------------------------------------------
        bool opened = false;

        // 4a. try dedicated hardware codec first (e.g. h264_qsv, hevc_vaapi)
        std::string hwName = std::string(swCodec->name) + "_" + b.codecSfx;
        const AVCodec* hwCodec = avcodec_find_decoder_by_name(hwName.c_str());
        if (hwCodec)
            opened = openCodec(videoDecoder.pAVCtx, hwCodec, pFormatCtx,
                                videoIndex, hw_device_ctx, hw_frames_ref, this);
        // CUDA has an alias: _cuvid (older API)
        if (!opened && b.type == AV_HWDEVICE_TYPE_CUDA) {
            std::string cuvName = std::string(swCodec->name) + "_cuvid";
            hwCodec = avcodec_find_decoder_by_name(cuvName.c_str());
            if (hwCodec)
                opened = openCodec(videoDecoder.pAVCtx, hwCodec, pFormatCtx,
                                    videoIndex, hw_device_ctx, hw_frames_ref, this);
        }

        // 4b. fall back: software codec + hwaccel (get_format callback drives it)
        if (!opened) {
            opened = openCodec(videoDecoder.pAVCtx, swCodec, pFormatCtx,
                                videoIndex, hw_device_ctx, hw_frames_ref, this);
        }

        av_log_set_level(oldLog);

        if (opened) {
            videoDecoder.codecName = videoDecoder.pAVCtx->codec->name;
            useHwDecode = true;
            hwBackendName = b.name;
            std::printf("Using hardware acceleration: %s (+%s, %s→%s)\n",
                        videoDecoder.codecName.c_str(), b.name,
                        av_get_pix_fmt_name(hw_pix_fmt_),
                        av_get_pix_fmt_name(AV_PIX_FMT_NV12));
            return true;
        }

        // rollback — restore software codec for next backend attempt
        openCodec(videoDecoder.pAVCtx, swCodec, pFormatCtx,
                   videoIndex, nullptr, nullptr, this);
        av_buffer_unref(&hw_frames_ref);
        av_buffer_unref(&hw_device_ctx);
    }

    std::printf("Hardware acceleration not available, using software decoder: %s\n",
                swCodec->name);
    return false;
}

bool FFmpegDecoder::initHwNV12Filter(AVBufferRef* hwFramesRef,
                                      int codedW, int codedH) {
    int ret = 0;
    // Declare all locals up front (C++ forbids goto over initializers)
    const AVFilter* bufSrcFilter   = nullptr;
    const char*     scaleName      = nullptr;
    const AVFilter* scaleFilter    = nullptr;
    AVFilterContext* scaleCtx      = nullptr;
    const AVFilter* downloadFilter = nullptr;
    AVFilterContext* downloadCtx   = nullptr;
    const AVFilter* bufSinkFilter  = nullptr;

    hwFilterGraph = avfilter_graph_alloc();
    if (!hwFilterGraph) {
        std::fprintf(stderr, "[hw filter] failed to allocate filter graph\n");
        return false;
    }

    // ---- 1. buffersrc --------------------------------------------------
    bufSrcFilter = avfilter_get_by_name("buffer");
    if (!bufSrcFilter) goto fail;

    hwBufferSrcCtx = avfilter_graph_alloc_filter(hwFilterGraph,
                                                  bufSrcFilter, "src");
    if (!hwBufferSrcCtx) goto fail;

    {
        AVBufferSrcParameters* par = av_buffersrc_parameters_alloc();
        if (!par) goto fail;
        par->format        = hw_pix_fmt_;
        par->width         = codedW;
        par->height        = codedH;
        par->time_base     = videoTimeBase;
        par->hw_frames_ctx = av_buffer_ref(hwFramesRef);
        ret = av_buffersrc_parameters_set(hwBufferSrcCtx, par);
        av_free(par);
        if (ret < 0) { std::fprintf(stderr, "[hw filter] buffersrc params failed\n"); goto fail; }
    }

    ret = avfilter_init_str(hwBufferSrcCtx, nullptr);
    if (ret < 0) { std::fprintf(stderr, "[hw filter] buffersrc init failed\n"); goto fail; }

    // ---- 2. scale_vaapi / scale_cuda -----------------------------------
    if (hwBackendName == "VAAPI")      scaleName = "scale_vaapi";
    else if (hwBackendName == "CUDA") scaleName = "scale_cuda";
    else { std::fprintf(stderr, "[hw filter] unknown backend\n"); goto fail; }

    scaleFilter = avfilter_get_by_name(scaleName);
    if (!scaleFilter) { std::fprintf(stderr, "[hw filter] %s not found\n", scaleName); goto fail; }

    scaleCtx = avfilter_graph_alloc_filter(hwFilterGraph, scaleFilter, "scale");
    if (!scaleCtx) goto fail;

    // critical: scale_vaapi/cuda is AVFILTER_FLAG_HWDEVICE — must set
    // hw_device_ctx BEFORE init
    scaleCtx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

    ret = avfilter_init_str(scaleCtx, "format=nv12");
    if (ret < 0) { std::fprintf(stderr, "[hw filter] %s init failed\n", scaleName); goto fail; }

    // ---- 3. hwdownload -------------------------------------------------
    downloadFilter = avfilter_get_by_name("hwdownload");
    if (!downloadFilter) { std::fprintf(stderr, "[hw filter] hwdownload not found\n"); goto fail; }

    downloadCtx = avfilter_graph_alloc_filter(hwFilterGraph,
                                               downloadFilter, "download");
    if (!downloadCtx) goto fail;

    ret = avfilter_init_str(downloadCtx, nullptr);
    if (ret < 0) { std::fprintf(stderr, "[hw filter] hwdownload init failed\n"); goto fail; }

    // ---- 4. buffersink (official pattern: alloc → opt_set → init) ------
    bufSinkFilter = avfilter_get_by_name("buffersink");
    if (!bufSinkFilter) { std::fprintf(stderr, "[hw filter] buffersink not found\n"); goto fail; }

    hwBufferSinkCtx = avfilter_graph_alloc_filter(hwFilterGraph,
                                                   bufSinkFilter, "sink");
    if (!hwBufferSinkCtx) goto fail;

    ret = av_opt_set(hwBufferSinkCtx, "pixel_formats", "nv12",
                     AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) { std::fprintf(stderr, "[hw filter] buffersink pix_fmts failed\n"); goto fail; }

    ret = avfilter_init_str(hwBufferSinkCtx, nullptr);
    if (ret < 0) { std::fprintf(stderr, "[hw filter] buffersink init failed\n"); goto fail; }

    // ---- 5. link: src → scale → download → sink -----------------------
    if (avfilter_link(hwBufferSrcCtx, 0, scaleCtx, 0) < 0)      { std::fprintf(stderr, "[hw filter] link 1 failed\n"); goto fail; }
    if (avfilter_link(scaleCtx, 0, downloadCtx, 0) < 0)         { std::fprintf(stderr, "[hw filter] link 2 failed\n"); goto fail; }
    if (avfilter_link(downloadCtx, 0, hwBufferSinkCtx, 0) < 0)  { std::fprintf(stderr, "[hw filter] link 3 failed\n"); goto fail; }

    // ---- 6. finalize ---------------------------------------------------
    if (avfilter_graph_config(hwFilterGraph, nullptr) < 0) {
        std::fprintf(stderr, "[hw filter] graph config failed\n");
        goto fail;
    }

    std::printf("[hw filter] GPU NV12 filter graph initialized (%s, %dx%d)\n",
                scaleName, codedW, codedH);
    return true;

fail:
    std::fprintf(stderr, "[hw filter] init failed, falling back to sws_scale\n");
    avfilter_graph_free(&hwFilterGraph);
    hwFilterGraph   = nullptr;
    hwBufferSrcCtx  = nullptr;
    hwBufferSinkCtx = nullptr;
    return false;
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

            int ret;
            if ((ret = avformat_seek_file(pFormatCtx, -1, INT64_MIN, targetTs, INT64_MAX, AVSEEK_FLAG_BACKWARD) < 0)) {
                char error[64];
                std::cout << "failed to seek time: " << av_make_error_string(error, sizeof(error), ret) << std::endl;
            } else {
                std::cout<< std::endl << "seeked " << seekReqTime << " s. " << std::endl;

                if (videoIndex >= 0) {
                    if (!videoIsCover) {
                        videoDecoder.packetQueue.clear();
                        mutexVideoCodec.lock();
                        avcodec_flush_buffers(videoDecoder.pAVCtx);
                        mutexVideoCodec.unlock();
                        videoDecoder.frameQueue.clear();

                        // Signal videoDecode to rebuild filter graph on
                        // the next frame — the decoder's hw_frames_ctx may
                        // have changed after seek.  The actual free happens
                        // on the videoDecode thread to avoid a race.
                        hwFilterNeedRebuild = true;
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
                while (videoDecoder.frameQueue.size() > 1) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
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
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                if (!running) {
                    break;
                }
            }
            videoDecoder.packetQueue.push(packet);
        } else if (pAVpkt->stream_index == audioIndex) {
            while (audioDecoder.packetQueue.full()) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
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

    audioDecoder.packetQueue.clear();
    videoDecoder.packetQueue.clear();

    // videoDecoder.frameQueue.clear();
    // audioDecoder.frameQueue.clear();

    stopped = true;

    avformat_close_input(&pFormatCtx);
    std::printf("FFmpegDecoder exiting...\n");
}

void FFmpegDecoder::videoDecode() {
    AVFrame* pAVframe = av_frame_alloc();

    while (videoDecoder.threadRunning) {
        while (paused) {}
        std::shared_ptr<Packet> pPacket;
        while (!videoDecoder.packetQueue.size()) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            if (!videoDecoder.threadRunning) {
                break;
            }
        }
        if (!videoDecoder.threadRunning) {
            break;
        }
        videoDecoder.packetQueue.pop(pPacket);

        mutexVideoCodec.lock();
        int ret = avcodec_send_packet(videoDecoder.pAVCtx, pPacket->data);
        mutexVideoCodec.unlock();
        if (ret < 0) {
            std::cout << "error during avcodec_send_packet" << std::endl;
            break;
        }

        while (videoDecoder.threadRunning) {
            mutexVideoCodec.lock();
            ret = avcodec_receive_frame(videoDecoder.pAVCtx, pAVframe);
            mutexVideoCodec.unlock();
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                std::cout << "error during avcodec_receive_frame" << std::endl;
                break;
            }

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

            if (getDelay(clock.videoPts) < -0.1) {
                continue;
            }

            // produce NV12 frame (hardware: GPU filter graph, software: sws_scale)
            AVFrame* outFrame = av_frame_alloc();
            if (useHwDecode && pAVframe->format == hw_pix_fmt_) {
                // Lazy init / recreate GPU filter graph on first frame,
                // when coded dimensions change, or after seek.
                if (hwFilterNeedRebuild.exchange(false) ||
                    !hwFilterGraph ||
                    pAVframe->width  != hwFilterW ||
                    pAVframe->height != hwFilterH) {
                    avfilter_graph_free(&hwFilterGraph);
                    hwFilterGraph   = nullptr;
                    hwBufferSrcCtx  = nullptr;
                    hwBufferSinkCtx = nullptr;
                    hwFilterW = pAVframe->width;
                    hwFilterH = pAVframe->height;
                    initHwNV12Filter(pAVframe->hw_frames_ctx,
                                     hwFilterW, hwFilterH);
                }

                if (hwFilterGraph) {
                    // === GPU filter graph path (primary) ===
                    int ret = av_buffersrc_add_frame_flags(hwBufferSrcCtx,
                                        pAVframe, AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT);
                    if (ret < 0) {
                        std::fprintf(stderr, "[hw] buffersrc add frame failed\n");
                        av_frame_free(&outFrame);
                        continue;
                    }
                    ret = av_buffersink_get_frame(hwBufferSinkCtx, outFrame);
                    if (ret < 0) {
                        std::fprintf(stderr, "[hw] buffersink get frame failed\n");
                        av_frame_free(&outFrame);
                        continue;
                    }
                    // outFrame is now CPU NV12 — ready for Vulkan pipeline
                } else {
                    // === Fallback: CPU-side sws_scale ===
                    AVFrame* tmpFrame = av_frame_alloc();
                    if (av_hwframe_transfer_data(tmpFrame, pAVframe, 0) < 0) {
                        std::fprintf(stderr, "[hw] transfer data failed\n");
                        av_frame_free(&tmpFrame);
                        av_frame_free(&outFrame);
                        continue;
                    }
                    int srcFmt = tmpFrame->format;
                    if (srcFmt != hwSwsSrcFmt) {
                        sws_freeContext(hwSwsCtx);
                        hwSwsCtx = sws_getContext(tmpFrame->width, tmpFrame->height,
                            static_cast<AVPixelFormat>(srcFmt),
                            tmpFrame->width, tmpFrame->height,
                            AV_PIX_FMT_NV12, SWS_FAST_BILINEAR,
                            nullptr, nullptr, nullptr);
                        hwSwsSrcFmt = srcFmt;
                    }
                    outFrame->width  = pAVframe->width;
                    outFrame->height = pAVframe->height;
                    outFrame->format = AV_PIX_FMT_NV12;
                    if (av_frame_get_buffer(outFrame, 0) < 0 || !hwSwsCtx) {
                        av_frame_free(&tmpFrame);
                        av_frame_free(&outFrame);
                        continue;
                    }
                    sws_scale(hwSwsCtx, tmpFrame->data, tmpFrame->linesize, 0,
                              tmpFrame->height, outFrame->data, outFrame->linesize);
                    av_frame_free(&tmpFrame);
                }
            } else {
                // software path: sws_scale to NV12
                outFrame->width  = width;
                outFrame->height = height;
                outFrame->format = AV_PIX_FMT_NV12;
                if (av_frame_get_buffer(outFrame, 0) < 0) {
                    av_frame_free(&outFrame);
                    continue;
                }
                sws_scale(pSwsCtx, pAVframe->data, pAVframe->linesize, 0,
                          height, outFrame->data, outFrame->linesize);
            }

            std::shared_ptr<Frame> frame = std::make_shared<Frame>();
            frame->data = outFrame;
            frame->videoPts = clock.videoPts;

            // push to queue
            while (videoDecoder.frameQueue.full()) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                if (!videoDecoder.threadRunning) {
                    break;
                }
            }
            if (!videoDecoder.threadRunning) {
                break;
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

    // Free software sws_ctx (always present for SW decode)
    sws_freeContext(pSwsCtx);
    pSwsCtx = nullptr;

    // Free the GPU filter graph FIRST — it holds internal refs to
    // hw_frames_ctx / hw_device_ctx via buffersrc parameters.
    avfilter_graph_free(&hwFilterGraph);
    hwFilterGraph   = nullptr;
    hwBufferSrcCtx  = nullptr;
    hwBufferSinkCtx = nullptr;

    // Free fallback HW sws_ctx (only used if filter graph init failed)
    sws_freeContext(hwSwsCtx);
    hwSwsCtx = nullptr;

    // Codec must be flushed + freed BEFORE hw resources —
    // the codec holds its own references to them (av_buffer_ref).
    avcodec_flush_buffers(videoDecoder.pAVCtx);
    avcodec_free_context(&videoDecoder.pAVCtx);

    av_buffer_unref(&hw_frames_ref);
    av_buffer_unref(&hw_device_ctx);

    videoDecoder.threadStopped = true;
}

void FFmpegDecoder::audioDecode() {
    AVFrame* pAVframe = av_frame_alloc();
    while (audioDecoder.threadRunning) {
        while (paused) {}
        std::shared_ptr<Packet> pPacket;
        while (!audioDecoder.packetQueue.size()) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            if (!audioDecoder.threadRunning) {
                break;
            }
        }
        if (!audioDecoder.threadRunning) {
            break;
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
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                if (!audioDecoder.threadRunning) {
                    break;
                }
            }
            if (!audioDecoder.threadRunning) {
                break;
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
