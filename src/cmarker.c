/** 
 * @file cmarker.c
 * @brief write JPEG markers.
 */

#include "cjpeg.h"
#include "cio.h"

/*
 * Length of APP0 block   (2 bytes)
 * Block ID               (4 bytes - ASCII "JFIF")
 * Zero byte              (1 byte to terminate the ID string)
 * Version Major, Minor   (2 bytes - major first)
 * Units                  (1 byte - 0x00 = none, 0x01 = inch, 0x02 = cm)
 * Xdpu                   (2 bytes - dots per unit horizontal)
 * Ydpu                   (2 bytes - dots per unit vertical)
 * Thumbnail X size       (1 byte)
 * Thumbnail Y size       (1 byte)
 */
void
write_app0(compress_io *cio) {
    write_marker(cio, M_APP0);
    write_word(cio, 2 + 4 + 1 + 2 + 1 + 2 + 2 + 1 + 1); /* length */
    write_byte(cio, 0x4A);       /* Identifier: ASCII "JFIF" */
    write_byte(cio, 0x46);
    write_byte(cio, 0x49);
    write_byte(cio, 0x46);
    write_byte(cio, 0);
    write_byte(cio, 1); /* Version fields */
    write_byte(cio, 1);
    write_byte(cio, 0); /* Pixel size information */
    write_word(cio, 1);
    write_word(cio, 1);
    write_byte(cio, 0); /* No thumbnail image */
    write_byte(cio, 0);
}

// 写入SOF0标记
void
write_sof0(compress_io *cio, bmp_info *binfo) {
    write_marker(cio, M_SOF0);
    write_word(cio, 3 * COMP_NUM + 2 + 5 + 1); /* length */
    // 每个数据样本的位数为8
    write_byte(cio, PRECISION);

    // 图像宽度和高度
    write_word(cio, binfo->height);
    write_word(cio, binfo->width);

    // 颜色分量数（通道数）：固定为3，因为我们是用YCrCb
    write_byte(cio, COMP_NUM);

    /*
     * Component:
     *  Component ID
     *  Sampling factors:   bit 0-3 vert., 4-7 hor.
     *  Quantization table No.
     */
    /* component Y */
    // Y通道的信息
    // Y通道的ID是1
    write_byte(cio, 1);
    // 0x11，高4位表示水平采样因子是1，低4位表示垂直采样因子。这里水平采样因子和垂直采样因子都是1。
    // 观察Cr和Cb两个分量的水平采样因子和垂直采样因子，发现他们的水平采样因子和垂直采样因子都是1。
    // 说明Y，Cr，Cb按1：1：1采样，因此一个MCU就是8*8（感觉助教应该是给我们降低难度了（））
    write_byte(cio, 0x11);
    // 量化表ID。Y通道是亮度，因此用亮度的量化表，量化表ID是0
    write_byte(cio, 0);


    /* component Cb */
    write_byte(cio, 2);
    write_byte(cio, 0x11);
    write_byte(cio, 1);


    /* component Cr */
    write_byte(cio, 3);
    write_byte(cio, 0x11);
    write_byte(cio, 1);
}

// 写入SOS（Start Of Scan）标记
void
write_sos(compress_io *cio) {
    write_marker(cio, M_SOS);
    write_word(cio, 2 + 1 + COMP_NUM * 2 + 3); /* length */

    // 颜色通道的个数
    write_byte(cio, COMP_NUM);

    /*
     * Component:
     *  Component ID
     *  DC & AC table No.   bits 0..3: AC table (0..3),
     *                      bits 4..7: DC table (0..3)
     */
    /* component Y */
    // 写入Y通道的信息
    // Y通道的颜色ID是1
    write_byte(cio, 1);
    // 高4位代表Y通道的AC哈夫曼编码表的ID，低4位代表Y通道的DC哈夫曼编码表的ID。两者都是0。（亮度的哈夫曼编码表）
    write_byte(cio, 0x00);
    /* component Cb */
    write_byte(cio, 2);
    write_byte(cio, 0x11);
    /* component Cr */
    write_byte(cio, 3);
    write_byte(cio, 0x11);

    write_byte(cio, 0);       /* Ss */
    write_byte(cio, 0x3F);    /* Se */
    write_byte(cio, 0);       /* Bf */
}

void
write_dqt(compress_io *cio) {
    /* index:
     *  bit 0..3: number of QT, Y = 0
     *  bit 4..7: precision of QT, 0 = 8 bit
     */
    int index;
    int i;
    write_marker(cio, M_DQT);
    // DQT标记的长度
    write_word(cio, 2 + (DCTSIZE2 + 1) * 2);

    // index：当成一个byte（取低8位），其中低4位代表量化表ID，高4位
    // 写入亮度的量化表
    index = 0;                  /* table for Y */
    write_byte(cio, index);
    for (i = 0; i < DCTSIZE2; i++)
        write_byte(cio, q_tables.lu[i]);

    // 写入色度的量化表
    index = 1;                  /* table for Cb,Cr */
    write_byte(cio, index);
    for (i = 0; i < DCTSIZE2; i++)
        write_byte(cio, q_tables.ch[i]);
}

int
get_ht_length(UINT8 *nrcodes) {
    int length = 0;
    int i;
    for (i = 1; i <= 16; i++)
        length += nrcodes[i];
    return length;
}

void
write_htable(compress_io *cio,
             UINT8 *nrcodes, UINT8 *values, int len, UINT8 index) {
    /*
     * index:
     *  bit 0..3: number of HT (0..3), for Y = 0
     *  bit 4   : type of HT, 0 = DC table, 1 = AC table
     *  bit 5..7: not used, must be 0
     */
    write_byte(cio, index);

    int i;
    for (i = 1; i <= 16; i++)
        write_byte(cio, nrcodes[i]);
    for (i = 0; i < len; i++)
        write_byte(cio, values[i]);
}

// 写入DHT标记
void
write_dht(compress_io *cio) {
    int len1, len2, len3, len4;

    write_marker(cio, M_DHT);

    len1 = get_ht_length(STD_LU_DC_NRCODES);
    len2 = get_ht_length(STD_LU_AC_NRCODES);
    len3 = get_ht_length(STD_CH_DC_NRCODES);
    len4 = get_ht_length(STD_CH_AC_NRCODES);
    write_word(cio, 2 + (1 + 16) * 4 + len1 + len2 + len3 + len4);

    // index的低4位代表哈夫曼表的id。index的高4位：0->DC哈夫曼表，1->AC哈夫曼表
    // 亮度通道，用的哈夫曼表的id是0，色度通道，用的哈夫曼表的id是1
    // 亮度分量的DC哈夫曼表（？）
    write_htable(cio, STD_LU_DC_NRCODES, STD_LU_DC_VALUES, len1, 0x00);
    // 亮度分量的AC哈夫曼表（？）
    write_htable(cio, STD_LU_AC_NRCODES, STD_LU_AC_VALUES, len2, 0x10);
    // 色度分量的DC哈夫曼表（？）
    write_htable(cio, STD_CH_DC_NRCODES, STD_CH_DC_VALUES, len3, 0x01);
    // 色度分量的AC哈夫曼表（？）
    write_htable(cio, STD_CH_AC_NRCODES, STD_CH_AC_VALUES, len4, 0x11);
}

/*
 * Write datastream header.
 * This consists of an SOI and optional APPn markers.
 */
void
write_file_header(compress_io *cio) {
    write_marker(cio, M_SOI);
    write_app0(cio);
}

/*
 * Write frame header.
 * This consists of DQT and SOFn markers.
 * Note that we do not emit the SOF until we have emitted the DQT(s).
 * This avoids compatibility problems with incorrect implementations that
 * try to error-check the quant table numbers as soon as they see the SOF.
 */
void
write_frame_header(compress_io *cio, bmp_info *binfo) {
    write_dqt(cio);
    write_sof0(cio, binfo);
}

/*
 * Write scan header.
 * This consists of DHT or DAC markers, optional DRI, and SOS.
 * Compressed rgbData will be written following the SOS.
 */
void
write_scan_header(compress_io *cio) {
    write_dht(cio);
    write_sos(cio);
}

/*
 * Write datastream trailer.
 */
void
write_file_trailer(compress_io *cio) {
    write_marker(cio, M_EOI);
}

