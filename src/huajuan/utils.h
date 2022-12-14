//
// Created by HuaJuan on 2022/12/14.
//

#ifndef BMP2JPEG_CMAKE_UTILS_H
#define BMP2JPEG_CMAKE_UTILS_H

#include "../cjpeg.h"

void assert_true(bool condition, const char *errMsg) {
    if (!condition) {
        err_exit(errMsg, 1);
    }
}

#endif //BMP2JPEG_CMAKE_UTILS_H
