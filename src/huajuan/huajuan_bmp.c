//
// Created by HuaJuan on 2022/12/14.
//
#include "huajuan_bmp.h"
#include "string.h"

void read_bmp_data(compress_io *cio,
                   bmp_info *bmpInfo,
                   struct bmp_complemented *bmpComplemented) {
    // 设置文件指针到像素开始的位置
    fseek(cio->in->fp, bmpInfo->offset, SEEK_SET);

    // 设置bmp_complemented的width和height
    bmpComplemented->realWidth = bmpInfo->width;
    bmpComplemented->realHeight = bmpInfo->height;

    // 补齐到8的倍数的长度和宽度
    UINT32 complementedWidth = (bmpComplemented->realWidth + (DCTSIZE - 1)) / DCTSIZE * DCTSIZE;
    UINT32 complementedHeight = (bmpComplemented->realHeight + (DCTSIZE - 1)) / DCTSIZE * DCTSIZE;
    bmpComplemented->complementedWidth = complementedWidth;
    bmpComplemented->complementedHeight = complementedHeight;

    // 因为还没有迭代过mcu，所以i和j都是0
    bmpComplemented->i = 0;
    bmpComplemented->j = 0;

    // 把bmpComplement.rgbData 开成 complementedWidth * complementedHeight的数组
    bmpComplemented->data = malloc((complementedHeight * complementedWidth) * sizeof(struct rgb_unit));

    // 从cio中按行读取数据，不过bmp中原始数据流的数据，是从下至上，从左至右的。即第一个读取到的行是最后一行。。。。。。
    for (int i = (int) bmpComplemented->realHeight - 1; i >= 0; i--) {
        if (!cio->in->flush_buffer(cio)) {
            err_exit(BUFFER_READ_ERR);
        }
        for (int j = 0; j < bmpComplemented->realWidth; j++) {
            // bmp 里面，颜色数据是按照BGR的顺序存储的
            int x = i * (int) complementedWidth + j;
            bmpComplemented->data[x].b = *(cio->in->pos);
            bmpComplemented->data[x].g = *(cio->in->pos + 1);
            bmpComplemented->data[x].r = *(cio->in->pos + 2);
            cio->in->pos += 3;
        }
    }
}

void free_bmp_data(struct bmp_complemented *bmpComplemented) {
    free(bmpComplemented->data);
}

void next_mcu(struct bmp_complemented *bmpC, struct mcu *mcu) {
    // 判断还有没有mcu可以读取
    bool hasMcu = (0 <= bmpC->i && bmpC->i < bmpC->complementedHeight / MCUSIZE)
                  && (0 <= bmpC->j && bmpC->j < bmpC->complementedWidth / MCUSIZE);
    if (!hasMcu) {
        // 把mcu的data设成null，来表示没有mcu可以读取了
        mcu->rgbData = NULL;
        return;
    }

    // 3：RGB的通道数
    mcu->rgbData = malloc(sizeof(UINT8) * 3 * MCUSIZE * MCUSIZE);

    // 读取mcu，即以(i*8, j*8) 为左上角的，8*8的方块
    // dx，dy：偏移
    for (int dx = 0; dx < MCUSIZE; ++dx) {
        for (int dy = 0; dy < MCUSIZE; ++dy) {
            // 实际的像素位置
            int x = bmpC->i * MCUSIZE + dx;
            int y = bmpC->j * MCUSIZE + dy;
            // 实际的像素位置（一维）
            int t = x * bmpC->complementedWidth + y;

            // 像素的rgb数据
            UINT8 b = bmpC->data[t].b;
            UINT8 g = bmpC->data[t].g;
            UINT8 r = bmpC->data[t].r;

            // 将(dx,dy)转换成1维数组的下标
            t = dx * MCUSIZE + dy;

            // 3：RGB通道数
            mcu->rgbData[3 * t] = b;
            mcu->rgbData[3 * t + 1] = g;
            mcu->rgbData[3 * t + 2] = r;
        }
    }

    // 更新i,j
    bmpC->j = bmpC->j + 1;

    // 如果这一行的mcu已经迭代完了，就换下一行的mcu
    if (bmpC->j == bmpC->complementedWidth / MCUSIZE) {
        bmpC->j = 0;
        bmpC->i = bmpC->i + 1;
    }
}

void free_mcu_data(struct mcu *mcu) {
    if (mcu->rgbData != NULL) {
        free(mcu->rgbData);
    }
}


