/** 
 * @file cio.c
 * @brief memory manager and operations for compressing JPEG IO.
 */

#include <string.h>
#include "cjpeg.h"
#include "cio.h"


/*
 * flush input and output of compress IO.
 */


bool
flush_cin_buffer(void *cio) {
    mem_mgr *in = ((compress_io *) cio)->in;
    size_t len = in->end - in->set;
    memset(in->set, 0, len);
    if (fread(in->set, sizeof(UINT8), len, in->fp) != len)
        return false;
    in->pos = in->set;
    return true;
}

bool
flush_cout_buffer(void *cio) {
    mem_mgr *out = ((compress_io *) cio)->out;
    size_t len = out->pos - out->set;
    if (fwrite(out->set, sizeof(UINT8), len, out->fp) != len)
        return false;
    memset(out->set, 0, len);
    out->pos = out->set;
    return true;
}


/*
 * init memory manager.
 */

void
init_mem(compress_io *cio,
         FILE *in_fp, int in_size, FILE *out_fp, int out_size) {
    cio->in = (mem_mgr *) malloc(sizeof(mem_mgr));
    if (!cio->in)
        err_exit(BUFFER_ALLOC_ERR);
    cio->in->set = (UINT8 *) malloc(sizeof(UINT8) * in_size);
    if (!cio->in->set)
        err_exit(BUFFER_ALLOC_ERR);
    cio->in->pos = cio->in->set;
    cio->in->end = cio->in->set + in_size;
    cio->in->flush_buffer = flush_cin_buffer;
    cio->in->fp = in_fp;

    cio->out = (mem_mgr *) malloc(sizeof(mem_mgr));
    if (!cio->out)
        err_exit(BUFFER_ALLOC_ERR);
    cio->out->set = (UINT8 *) malloc(sizeof(UINT8) * out_size);
    if (!cio->out->set)
        err_exit(BUFFER_ALLOC_ERR);
    cio->out->pos = cio->out->set;
    cio->out->end = cio->out->set + out_size;
    cio->out->flush_buffer = flush_cout_buffer;
    cio->out->fp = out_fp;

    cio->temp_bits.len = 0;
    cio->temp_bits.val = 0;
}

void
free_mem(compress_io *cio) {
    fflush(cio->out->fp);
    free(cio->in->set);
    free(cio->out->set);
    free(cio->in);
    free(cio->out);
}


/*
 * write operations.
 */

void
write_byte(compress_io *cio, UINT8 val) {
    mem_mgr *out = cio->out;
    *(out->pos)++ = val & 0xFF;
    if (out->pos == out->end) {
        if (!(out->flush_buffer)(cio))
            err_exit(BUFFER_WRITE_ERR);
    }
}

void
write_word(compress_io *cio, UINT16 val) {
    write_byte(cio, (val >> 8) & 0xFF);
    write_byte(cio, val & 0xFF);
}

void
write_marker(compress_io *cio, JPEG_MARKER mark) {
    write_byte(cio, 0xFF);
    write_byte(cio, (int) mark);
}

void
write_bits(compress_io *cio, BITS bits) {
    // 传进来的bits变量，还有多少长度没有写进去
    int remainLength = bits.len;
    while (remainLength > 0) {
        // 从头到位读取bits.value的每一个位
        int b = (bits.val >> (remainLength - 1)) & 0x0001;

        // 把bit放到cio的temp_bits里
        cio->temp_bits.val = (cio->temp_bits.val << 1) + b;
        cio->temp_bits.len += 1;

        // 如果满了8个位，也就是1个字节，就准备写入到file里
        if (cio->temp_bits.len == 8) {
            UINT8 v = cio->temp_bits.val & 0xFF;
            write_byte(cio, v);
            // 判断v是不是0xFF。如果是的话，需要在后面在写入一个0x00，防止被当成jpeg标记
            if (v == 0xFF) {
                write_byte(cio, 0x00);
            }
            // 清空temp_bits
            cio->temp_bits.val = 0;
            cio->temp_bits.len = 0;
        }

        remainLength--;
    }
}

void
write_align_bits(compress_io *cio) {
    // 将temp_bits里不满8位的部分用1填充，然后写入到文件里
    while (cio->temp_bits.len < 8) {
        cio->temp_bits.val = (cio->temp_bits.val << 1) + 1;
        cio->temp_bits.len++;
    }

    UINT8 v = cio->temp_bits.val & 0xFF;
    // 判断是不是0xFF
    write_byte(cio, v);
    if (v == 0xFF) {
        write_byte(cio, 0x00);
    }
}

