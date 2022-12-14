/** 
 * @file cjpeg.c
 * @brief main file, convert BMP to JPEG image.
 */

#include "cjpeg.h"
#include "cio.h"
#include "rdbmp.h"
#include "cmarker.h"
#include "huajuan/huajuan_bmp.h"
#include "fdctflt.h"
#include "huajuan/utils.h"

/* YCbCr to RGB transformation */

/*
 * precalculated tables for a faster YCbCr->RGB transformation.
 * use a INT32 table because we'll scale values by 2^16 and
 * work with integers.
 */

ycbcr_tables ycc_tables;

void
init_ycbcr_tables() {
    UINT16 i;
    for (i = 0; i < 256; i++) {
        ycc_tables.r2y[i] = (INT32) (65536 * 0.299 + 0.5) * i;
        ycc_tables.r2cb[i] = (INT32) (65536 * -0.16874 + 0.5) * i;
        ycc_tables.r2cr[i] = (INT32) (32768) * i;
        ycc_tables.g2y[i] = (INT32) (65536 * 0.587 + 0.5) * i;
        ycc_tables.g2cb[i] = (INT32) (65536 * -0.33126 + 0.5) * i;
        ycc_tables.g2cr[i] = (INT32) (65536 * -0.41869 + 0.5) * i;
        ycc_tables.b2y[i] = (INT32) (65536 * 0.114 + 0.5) * i;
        ycc_tables.b2cb[i] = (INT32) (32768) * i;
        ycc_tables.b2cr[i] = (INT32) (65536 * -0.08131 + 0.5) * i;
    }
}

/**
 * RGB转换成YCbCr
 * 在这个函数里，已经完成了将YCbCr的结果减去128的操作
 * 因此，这个函数返回的YCbCr的结果，可以直接进行离散余弦变换
 */
void
rgb_to_ycbcr(UINT8 *rgb_unit, ycbcr_unit *ycc_unit, int x, int w) {
    ycbcr_tables *tbl = &ycc_tables;
    UINT8 r, g, b;
    int src_pos = x * 3;
    int dst_pos = 0;
    int i, j;
    for (j = 0; j < DCTSIZE; j++) {
        for (i = 0; i < DCTSIZE; i++) {
            b = rgb_unit[src_pos];
            g = rgb_unit[src_pos + 1];
            r = rgb_unit[src_pos + 2];
            ycc_unit->y[dst_pos] = (INT8) ((UINT8)
                                                   ((tbl->r2y[r] + tbl->g2y[g] + tbl->b2y[b]) >> 16) - 128);
            ycc_unit->cb[dst_pos] = (INT8) ((UINT8)
                    ((tbl->r2cb[r] + tbl->g2cb[g] + tbl->b2cb[b]) >> 16));
            ycc_unit->cr[dst_pos] = (INT8) ((UINT8)
                    ((tbl->r2cr[r] + tbl->g2cr[g] + tbl->b2cr[b]) >> 16));
            src_pos += 3;
            dst_pos++;
        }
        src_pos += (w - DCTSIZE) * 3;
    }
}


/* quantization */

quant_tables q_tables;

void
init_quant_tables(UINT32 scale_factor) {
    quant_tables *tbl = &q_tables;
    int temp1, temp2;
    int i;
    for (i = 0; i < DCTSIZE2; i++) {
        temp1 = ((UINT32) STD_LU_QTABLE[i] * scale_factor + 50) / 100;
        if (temp1 < 1)
            temp1 = 1;
        if (temp1 > 255)
            temp1 = 255;
        tbl->lu[ZIGZAG[i]] = (UINT8) temp1;

        temp2 = ((UINT32) STD_CH_QTABLE[i] * scale_factor + 50) / 100;
        if (temp2 < 1)
            temp2 = 1;
        if (temp2 > 255)
            temp2 = 255;
        tbl->ch[ZIGZAG[i]] = (UINT8) temp2;
    }
}

// 将离散余弦变换的结果进行量化
void
jpeg_quant(ycbcr_unit *ycc_unit, quant_unit *q_unit) {
    quant_tables *tbl = &q_tables;
    float q_lu, q_ch;
    int x, y, i = 0;
    for (x = 0; x < DCTSIZE; x++) {
        for (y = 0; y < DCTSIZE; y++) {
            q_lu = 1.0 / ((double) tbl->lu[ZIGZAG[i]] * \
                    AAN_SCALE_FACTOR[x] * AAN_SCALE_FACTOR[y] * 8.0);
            q_ch = 1.0 / ((double) tbl->ch[ZIGZAG[i]] * \
                    AAN_SCALE_FACTOR[x] * AAN_SCALE_FACTOR[y] * 8.0);

            q_unit->y[i] = (INT16) (ycc_unit->y[i] * q_lu + 16384.5) - 16384;
            q_unit->cb[i] = (INT16) (ycc_unit->cb[i] * q_ch + 16384.5) - 16384;
            q_unit->cr[i] = (INT16) (ycc_unit->cr[i] * q_ch + 16384.5) - 16384;

            i++;
        }
    }
}


/* huffman compression */

huff_tables h_tables;

void
set_huff_table(UINT8 *nrcodes, UINT8 *values, BITS *h_table) {
    // nrcodes：长度位i的哈夫曼码字有nrcides[i]个
    // values：哈夫曼码字的解码后的值（原始值）
    // 变量名都是abcdijk，很难读懂到底在写什么。。。。。。
    // values和value，看着两个变量差不多，其实意思完全不一样。。。。。。
    int i, j, k;
    j = 0;
    // value：哈夫曼码字
    UINT16 value = 0;
    for (i = 1; i <= 16; i++) {
        // i：哈夫曼码字的长度
        // nrcodes[i]：长度位i的哈夫曼码字有多少个
        for (k = 0; k < nrcodes[i]; k++) {
            // j：当前迭代到第几个【原始值】了
            // 构建【原始值】->【哈夫曼码字】的映射
            // 设置哈夫曼码字的长度和哈夫曼码字的值
            h_table[values[j]].len = i;
            h_table[values[j]].val = value;
            j++;
            // 迭代哈夫曼码字。在哈夫曼码字长度不变的情况下，下一个哈夫曼码字就是当前的哈夫曼码字加一
            value++;
        }
        // 迭代哈夫曼码字。如果进入下一个哈夫曼码字长度（哈夫曼码字长度加1）
        // 则下一个哈夫曼码字就是当前的哈夫曼码字后面填个0，也就是左移1位！
        value <<= 1;
    }
}

void
init_huff_tables() {
    huff_tables *tbl = &h_tables;
    // 设置【原始值】->【哈夫曼码字】的映射，方便jpeg编码的时候用
    // 亮度，DC哈夫曼表
    set_huff_table(STD_LU_DC_NRCODES, STD_LU_DC_VALUES, tbl->lu_dc);
    // 亮度，AC哈夫曼表
    set_huff_table(STD_LU_AC_NRCODES, STD_LU_AC_VALUES, tbl->lu_ac);
    // 色度，DC哈夫曼表
    set_huff_table(STD_CH_DC_NRCODES, STD_CH_DC_VALUES, tbl->ch_dc);
    // 色度，AC哈夫曼表
    set_huff_table(STD_CH_AC_NRCODES, STD_CH_AC_VALUES, tbl->ch_ac);
}

void
set_bits(BITS *bits, INT16 data) {
    INT16 abs = data >= 0 ? data : -data;
    INT16 len = 0;

    // 找到从左往右数起来，第一个1所在的位置，就代表了data的绝对值的长度
    INT16 rightMoveCnt = 15;
    while (rightMoveCnt >= 0) {
        UINT16 b = (abs >> rightMoveCnt) & 0x0001;
        if (b == 1) {
            len = rightMoveCnt + 1;
            break;
        }
        rightMoveCnt--;
    }

    bits->len = len;
    // 如果data大于等于0，则幅值的码字是data；如果data小于0，则幅值的码字是data的绝对值的反码
    bits->val = data >= 0 ? abs : ~abs;
}

#ifdef DEBUG
void
print_bits(BITS bits)
{
    printf("%hu %hu\t", bits.len, bits.val);
}
#endif

/*
 * compress JPEG
 * data: data[64]，经过离散余弦变换和量化的某个颜色分量
 * dc: int * dc，指向【上一个相同颜色分量mcu的dc系数】的指针
 * dc_htable，ac_htable：dc和ac分量对应的哈夫曼表
 */
void
jpeg_compress(compress_io *cio,
              INT16 *data, INT16 *dc, BITS *dc_htable, BITS *ac_htable) {
    INT16 zigzag_data[DCTSIZE2];
    BITS bits;
    INT16 diff;
    int i, j;
    int zero_num;
    int mark;

    /* zigzag encode */
    // zig-zag 编码
    for (i = 0; i < DCTSIZE2; i++)
        zigzag_data[ZIGZAG[i]] = data[i];

    /* write DC */
    // 写入DC
    diff = zigzag_data[0] - *dc;
    *dc = zigzag_data[0];

    if (diff == 0)
        // 先写【幅值所需要的位数】对应的哈夫曼码字
        write_bits(cio, dc_htable[0]);
        // 幅值所需要的位数是0，也就不写幅值了
    else {
        // 设置bits变量，存储了【幅值】所需要的位数，和幅值的码字
        set_bits(&bits, diff);
        // 写【幅值所需要的位数】对应的哈夫曼码字
        write_bits(cio, dc_htable[bits.len]);
        // 写【幅值】对应的码字
        write_bits(cio, bits);
    }

    /* write AC */
    // 写入AC
    int end = DCTSIZE2 - 1;
    while (zigzag_data[end] == 0 && end > 0)
        // "跳过"掉zig-zag后末尾的0
        end--;

    // 从1开始，因为下标为0的是直流分量，之前已经写过了
    for (i = 1; i <= end; i++) {
        j = i;
        // "跳过"连续的0
        while (zigzag_data[j] == 0 && j <= end)
            j++;
        zero_num = j - i; // 连续的0的数目

        // 如果连续的0超过16个，对于每连续的16个0，写入一个"1111/0000"对应的哈夫曼码字，用来表示16个0
        for (mark = 0; mark < zero_num / 16; mark++)
            write_bits(cio, ac_htable[0xF0]);
        // 剩下的连续的0的数量（不满16个）
        zero_num = zero_num % 16;
        // bits变量存储了【幅值】所需要的位数，和幅值的码字
        set_bits(&bits, zigzag_data[j]);
        // 高4位表示连续0的个数，低4位表示幅值的所需要的位数，转换成哈夫曼码字后写入文件
        write_bits(cio, ac_htable[zero_num * 16 + bits.len]);
        // 写入幅值对应的码字
        write_bits(cio, bits);
        i = j;
    }

    /* write end of unit */
    // 对于尾巴上连续的0，直接写入一个EOB(0/0)
    if (end != DCTSIZE2 - 1)
        write_bits(cio, ac_htable[0]);
}


/*
 * main JPEG encoding
 */
void
jpeg_encode(compress_io *cio, bmp_info *binfo) {
    /* init tables */
    UINT32 scale = 50;
    init_ycbcr_tables();
    init_quant_tables(scale);
    init_huff_tables();

    /* write info */
    // 这里写入了SOI（Start Of Image）标记和APP0标记
    write_file_header(cio);
    // 这里写入了DQT（量化表）标记和SOF（Start Of Frame）标记
    write_frame_header(cio, binfo);
    // 这里写入了DHT（Define Huffman Table）标记和SOS（Start of Scan）标记
    write_scan_header(cio);

    // 把bmp的数据一次性读到内存里来
    struct bmp_complemented bmpComplemented;
    read_bmp_data(cio, binfo, &bmpComplemented);

    // 逐个从内存中的bmp图像中，迭代MCU
    struct mcu my_mcu;
    next_mcu(&bmpComplemented, &my_mcu);
    // 上一次的Y通道，Cb通道，Cr通道的Dc值
    INT16 lastYDc = 0;
    INT16 lastCbDc = 0;
    INT16 lastCrDc = 0;
    for (; my_mcu.rgbData != NULL; next_mcu(&bmpComplemented, &my_mcu)) {

        // 将RGB数据转换为YCbCr数据，将YCbCr的数据减去128的工作，也在这个函数里面完成了
        ycbcr_unit ycbcrUnit;
        rgb_to_ycbcr(my_mcu.rgbData, &ycbcrUnit, 0, DCTSIZE);

        // 离散余弦变换（对Y，Cb，Cr三个通道都进行离散余弦变换）
        jpeg_fdct(ycbcrUnit.y);
        jpeg_fdct(ycbcrUnit.cb);
        jpeg_fdct(ycbcrUnit.cr);

        // 将离散余弦变换的结果进行量化
        quant_unit quantUnit;
        jpeg_quant(&ycbcrUnit, &quantUnit);

        // jpeg压缩，并写入文件（分别对Y，Cb，Cr三个分量）
        jpeg_compress(cio,
                      quantUnit.y,
                      &lastYDc,
                      h_tables.lu_dc,
                      h_tables.lu_ac);
        jpeg_compress(cio,
                      quantUnit.cb,
                      &lastCbDc,
                      h_tables.ch_dc,
                      h_tables.ch_ac);
        jpeg_compress(cio,
                      quantUnit.cr,
                      &lastCrDc,
                      h_tables.ch_dc,
                      h_tables.ch_ac);


        // 更新"上一次的直流分量值"
        lastYDc = quantUnit.y[0];
        lastCbDc = quantUnit.cb[0];
        lastCrDc = quantUnit.cr[0];

        // 释放内存
        free_mcu_data(&my_mcu);
    }

    write_align_bits(cio);

    /* write file end */
    write_file_trailer(cio);

    free_bmp_data(&bmpComplemented);
}


bool
is_bmp(FILE *fp) {
    UINT8 marker[3];
    // 这里原来的代码，明明只开了三个UINT8的空间，却读取了两个UINT16的值，导致溢出，导致程序一上来就崩，我也不知道助教为什么要这样写。。。。。。
    //    if (fread(marker, sizeof(UINT16), 2, fp) != 2)
    if (fread(marker, sizeof(UINT16), 1, fp) != 1)
        err_exit(FILE_READ_ERR);
    if (marker[0] != 0x42 || marker[1] != 0x4D)
        return false;
    rewind(fp);
    return true;
}

void
err_exit(const char *error_string, int exit_num) {
    printf(error_string);
    exit(exit_num);
}


void
print_help() {
    printf("compress BMP file into JPEG file.\n");
    printf("Usage:\n");
    printf("    cjpeg {BMP} {JPEG}\n");
    printf("\n");
    printf("Author: Yu, Le <yeolar@gmail.com>\n");
    printf("Homework by Haojie Zhang (HuaJuan) 19302010021@fudan.edu.cn");
}


int
main(int argc, char *argv[]) {
    if (argc == 3) {
        /* open bmp file */
        FILE *bmp_fp = fopen(argv[1], "rb");
        if (!bmp_fp)
            err_exit(FILE_OPEN_ERR);
        if (!is_bmp(bmp_fp))
            err_exit(FILE_TYPE_ERR);

        /* open jpeg file */
        FILE *jpeg_fp = fopen(argv[2], "wb");
        if (!jpeg_fp)
            err_exit(FILE_OPEN_ERR);

        /* get bmp info */
        bmp_info binfo;
        read_bmp(bmp_fp, &binfo);
        assert_true(binfo.bitppx == 24, "很抱歉，我只能转换24位bmp");

        /* init memory for input and output */
        compress_io cio;
        // 一行的数据量。
        // 因为bmp文件中，一行的字节数必须是4的倍数，因此(binfo.width * 3 + 3) / 4 * 4就可以将binfo.width向上对齐到最近的4的倍数
        int in_size = (binfo.width * 3 + 3) / 4 * 4;
        int out_size = MEM_OUT_SIZE;
        init_mem(&cio, bmp_fp, in_size, jpeg_fp, out_size);

        struct bmp_complemented bmpComplemented;
        read_bmp_data(&cio, &binfo, &bmpComplemented);

        /* main encode process */
        jpeg_encode(&cio, &binfo);

        /* flush and free memory, close files */
        if (!(cio.out->flush_buffer)(&cio))
            err_exit(BUFFER_WRITE_ERR);
        free_mem(&cio);
        fclose(bmp_fp);
        fclose(jpeg_fp);
    } else
        print_help();
    exit(0);
}
