//
// Created by heshaoquan on 2025/9/6.
//

#ifndef VK_SDL2_VP_SDLAUDIOPLAYER_H
#define VK_SDL2_VP_SDLAUDIOPLAYER_H

#include <string>
#include <SDL2/SDL.h>

#include "FFmpegDecoder.h"


class SDLAudioPlayer {
public:
    SDLAudioPlayer();

    void setFFmpegDecoder(FFmpegDecoder* decoder);

    SDL_AudioSpec getAudioSpec();

    std::string getDeviceName();

    void run();

    void pause();

    void stop();

    void updateVolume(int sign);

private:
    FFmpegDecoder* ffmpegDecoder = nullptr;
    bool playing = false;

    SDL_AudioSpec spec_{};
    std::string deviceName_;
    SDL_AudioDeviceID deviceID_;

    const int maxVolume_ = SDL_MIX_MAXVOLUME;
    int volume_ = SDL_MIX_MAXVOLUME;

    Uint8 *buffer_{};
    Uint8 *audioPos{};
    int bufferSize_{};


private:
    static void callback(void *userdata, Uint8 *stream, int len);

    void fillAudio(Uint8 *stream, int len);
};


#endif //VK_SDL2_VP_SDLAUDIOPLAYER_H