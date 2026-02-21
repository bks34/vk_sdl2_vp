//
// Created by heshaoquan on 2025/9/6.
//

#include "SDLAudioPlayer.h"

#include <iostream>
#include <ostream>

#include "VulkanSDL2App.h"


SDLAudioPlayer::SDLAudioPlayer() {
    char* name;
    SDL_GetDefaultAudioInfo(&name, &spec_, 0);
    deviceName_.assign(name);

    if (spec_.samples == 0) {
        spec_.samples = 1024;
    }
    spec_.userdata = this;
    spec_.callback = callback;

    SDL_AudioSpec obtainedSpec;

    deviceID_ = SDL_OpenAudioDevice(deviceName_.c_str(), 0, &spec_, &obtainedSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (!deviceID_) {
        std::cerr << "Can't open Audio Device: " <<SDL_GetError() << std::endl;
    }
}

void SDLAudioPlayer::setFFmpegDecoder(FFmpegDecoder* decoder) {
    this->ffmpegDecoder = decoder;
}

SDL_AudioSpec SDLAudioPlayer::getAudioSpec() {
    return spec_;
}

std::string SDLAudioPlayer::getDeviceName() {
    return deviceName_;
}

void SDLAudioPlayer::run() {
    SDL_PauseAudioDevice(deviceID_, 0);
    playing = true;
}

void SDLAudioPlayer::pause() {
    SDL_PauseAudioDevice(deviceID_, 1);
}

void SDLAudioPlayer::updateVolume(int sign) {
    int t = volume_ + sign * std::max(volume_ / 8, 4);
    if (t >= 0 && t <= maxVolume_) {
        volume_ = t;
    } else if (t > maxVolume_) {
        volume_ = maxVolume_;
    } else {
        volume_ = 0;
    }
    std::printf("volume: %.2lf %% \n", static_cast<double>(volume_) / maxVolume_ * 100);
}


void SDLAudioPlayer::callback(void *userdata, Uint8 *stream, int len) {
    if (auto* self = static_cast<SDLAudioPlayer *>(userdata)) {
        self->fillAudio(stream, len);
    }
}

void SDLAudioPlayer::fillAudio(Uint8 *stream, int len) {
    SDL_memset(stream, 0, len);

    while (len > 0) {
        if (bufferSize_ == 0) {
            if (buffer_) {
                free(buffer_);
                buffer_ = nullptr;
            }
            auto frame = ffmpegDecoder->getAudioFrame();

            ffmpegDecoder->updateAudioClock(0, frame->audioPts);

            buffer_ = frame->audioData;
            audioPos = buffer_;
            bufferSize_ = frame->audioBufferSize;
        }

        int fillLen = (bufferSize_ < len) ? bufferSize_ : len;
        SDL_MixAudioFormat(stream, audioPos, spec_.format, fillLen, volume_);

        audioPos += fillLen;
        bufferSize_ -= fillLen;

        ffmpegDecoder->updateAudioClock(fillLen, 0);

        stream += fillLen;
        len -= fillLen;
    }
}
