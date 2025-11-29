//
// Created by heshaoquan on 25-8-3.
//

#ifndef UTILS_H
#define UTILS_H

extern "C" {
#include <libavformat/avformat.h>
}


class Utils {
public:
    /**
     * 将RGB32格式的AVFrame保存为BMP文件
     * @param frame 输入帧（格式必须为AV_PIX_FMT_RGBA）
     * @param filename 输出BMP路径
     * @return 0=成功，非0=失败
     */
    static int save_rgb32_to_bmp(AVFrame *frame, const char *filename);
};


#endif //UTILS_H
