//
// Created by HuaJuan on 2022/12/12.
//

#ifndef BMP2JPEG_CODE_CMARKER_H
#define BMP2JPEG_CODE_CMARKER_H

#include "cio.h"

void
write_file_header(compress_io *cio);

void
write_frame_header(compress_io *cio, bmp_info *binfo);

void
write_scan_header(compress_io *cio);

void
write_file_trailer(compress_io *cio);

#endif //BMP2JPEG_CODE_CMARKER_H
