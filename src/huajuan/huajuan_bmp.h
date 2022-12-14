//
// Created by HuaJuan on 2022/12/14.
//

#ifndef BMP2JPEG_CMAKE_HUAJUAN_BMP_H
#define BMP2JPEG_CMAKE_HUAJUAN_BMP_H

#include "../cjpeg.h"
#include "../cio.h"

/* RGB 颜色单元，包括RGB三个分量，原始的BMP图像中，数据是RGB编码的 */
struct rgb_unit {
    UINT8 b;
    UINT8 g;
    UINT8 r;
};

/**
 * 经过8*8补齐的bmp数据。
 * realWidth和realHeight是bmp图像的原始宽度和长度
 * complementedWidth和complementedHeight是bmp图像补齐到8的倍数以后的宽度和长度
 * 而data的长度和宽度是经过补齐到8的倍数的
 * 例如，如果有一个10*10的bmp图像，则realWidth=realHeight=10，而data是一个16*16=256的数组
 */
struct bmp_complemented {
    UINT32 realWidth;
    UINT32 realHeight;
    UINT32 complementedWidth;
    UINT32 complementedHeight;
    UINT32 i; // 当前在垂直方向，迭代到第几个MCU了
    UINT32 j; // 当前在水平方向，迭代到几个MCU了
    struct rgb_unit *data;
};

/**
 * 一个8*8*3的MCU
 * 数据格式为
 * (0,0)B, (0,0)G, (0,0)R, (0,1)B, (0,1)G, (0,1)R.......
 */
struct mcu {
    UINT8 *rgbData; // length：8*8*3
};

/* 读取bmp的数据 */
void read_bmp_data(compress_io *cio,
                   bmp_info *bmpInfo,
                   struct bmp_complemented *bmpComplemented);

void free_bmp_data(struct bmp_complemented *bmpComplemented);

/* 获取下一个MCU */
void next_mcu(struct bmp_complemented *bmpC, struct mcu *mcu);

/* 释放mcu录的data数据 */
void free_mcu_data(struct mcu *mcu);

#endif //BMP2JPEG_CMAKE_HUAJUAN_BMP_H
