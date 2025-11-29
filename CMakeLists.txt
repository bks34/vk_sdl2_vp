cmake_minimum_required(VERSION 3.10)
project(vk_sdl2_vp)

set(CMAKE_CXX_STANDRAD 20)
set(CMAKE_CXX_STANDRAD_REQUIRED ON)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)
find_package(sdl2 REQUIRED)

# 查找FFmpeg库
find_library(AVCODEC_LIBRARY avcodec)
find_library(AVFORMAT_LIBRARY avformat)
find_library(AVUTIL_LIBRARY avutil)
find_library(SWSCALE_LIBRARY swscale)
find_library(AVDEVICE_LIBRARY avdevice)
find_library(AVFILTER_LIBRARY avfilter)
find_library(SWRESAMPLE_LIBRARY swresample)

set(FFMPEG_LIBRARIES
        ${AVCODEC_LIBRARY}
        ${AVFORMAT_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${SWSCALE_LIBRARY}
        ${AVDEVICE_LIBRARY}
        ${AVFILTER_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
)

add_executable(${PROJECT_NAME}
        src/main.cpp
        src/VulkanSDL2App.h
        src/VulkanSDL2App.cpp
        src/FFmpegDecoder.cpp
        src/FFmpegDecoder.h
        shaders/vert_spv.h
        shaders/frag_spv.h
        src/Utils.cpp
        src/Utils.h
        src/ThreadSafeQueue.h
        src/SDLAudioPlayer.cpp
        src/SDLAudioPlayer.h
)

target_link_libraries(${PROJECT_NAME}
        Vulkan::Vulkan
        glm::glm
        SDL2::SDL2
        ${FFMPEG_LIBRARIES}
)

# about install
install(
        TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
)

set(CPACK_GENERATOR "DEB")

# 设置软件包基本信息
set(CPACK_PACKAGE_NAME "vk_sdl2_vp")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION "一个基于vulkan + sdl2 + ffmpeg的简单的视频播放器")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Shaoquan He 2712110988@qq.com") # 必须设置

# 设置deb包特定信息
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64") # e.g., amd64, arm64, armhf
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.28), libstdc++6") # 可选：指定依赖包
# 启用自动依赖检测
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# 包含CPack模块
include(CPack)