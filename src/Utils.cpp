//
// Created by heshaoquan on 25-8-3.
//

#include "Utils.h"
#include <iostream>
#include <fstream>

// BMP文件头（14字节，取消字节对齐）
#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t bfType = 0x4D42; // 固定为"BM"
    uint32_t bfSize{}; // 文件总大小（字节）
    uint16_t bfReserved1 = 0; // 保留字段
    uint16_t bfReserved2 = 0; // 保留字段
    uint32_t bfOffBits = 54; // 像素数据偏移量（14+40）
};
#pragma pack(pop)

// BMP信息头（40字节）
#pragma pack(push, 1)
struct BMPInfoHeader {
    uint32_t biSize = 40; // 信息头大小（固定40）
    int32_t biWidth{}; // 图像宽度（像素）
    int32_t biHeight{}; // 图像高度（像素，负数表示从上到下存储）
    uint16_t biPlanes = 1; // 色彩平面数（固定1）
    uint16_t biBitCount = 32; // 32位像素（RGBA/BGRA）
    uint32_t biCompression = 0; // 不压缩（BI_RGB）
    uint32_t biSizeImage = 0; // 像素数据大小（0表示不压缩）
    int32_t biXPelsPerMeter = 0; // 水平分辨率（忽略）
    int32_t biYPelsPerMeter = 0; // 垂直分辨率（忽略）
    uint32_t biClrUsed = 0; // 使用的颜色数（0表示全部）
    uint32_t biClrImportant = 0; // 重要颜色数（0表示全部）
};
#pragma pack(pop)

int Utils::save_rgb32_to_bmp(AVFrame *frame, const char *filename) {
    if (!frame || !filename) {
        std::cerr << "错误：输入帧或文件名为空\n";
        return -1;
    }

    // 检查帧格式（仅支持RGBA）
    if (frame->format != AV_PIX_FMT_RGBA) {
        std::cerr << "错误：仅支持AV_PIX_FMT_RGBA格式\n";
        return -2;
    }

    int width = frame->width;
    int height = frame->height;
    if (width <= 0 || height <= 0) {
        std::cerr << "错误：无效的图像宽高\n";
        return -3;
    }

    // 计算文件总大小（文件头+信息头+像素数据）
    uint32_t pixel_size = width * height * 4; // 32位像素，4字节/像素
    BMPFileHeader file_header;
    file_header.bfSize = 14 + 40 + pixel_size;

    // 初始化信息头（高度为负数，从上到下存储）
    BMPInfoHeader info_header;
    info_header.biWidth = width;
    info_header.biHeight = -height; // 负号表示扫描方向与AVFrame一致

    // 打开文件（二进制模式）
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "错误：无法打开文件 " << filename << "\n";
        return -4;
    }

    // 写入文件头和信息头
    file.write(reinterpret_cast<const char *>(&file_header), sizeof(file_header));
    file.write(reinterpret_cast<const char *>(&info_header), sizeof(info_header));

    // 转换并写入像素数据（RGBA → BGRA）
    for (int y = 0; y < height; ++y) {
        // 获取当前行的RGBA数据指针
        uint8_t *src_row = frame->data[0] + y * frame->linesize[0];

        // 逐像素转换（R和B通道交换）
        for (int x = 0; x < width; ++x) {
            uint8_t r = src_row[x * 4 + 0]; // 原R通道
            uint8_t g = src_row[x * 4 + 1]; // 原G通道
            uint8_t b = src_row[x * 4 + 2]; // 原B通道
            uint8_t a = src_row[x * 4 + 3]; // 原Alpha通道

            // 写入BGRA格式（BMP32位默认）
            file.put(b); // B
            file.put(g); // G
            file.put(r); // R
            file.put(a); // A（通常设为0xFF表示不透明）
        }
    }

    file.close();
    std::cout << "成功保存BMP文件：" << filename << "\n";
    return 0;
}
