cmake_minimum_required(VERSION 3.21)
project(bmp2jpeg_cmake C)

set(CMAKE_C_STANDARD 99)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
add_executable(bmp2jpeg_cmake
        cjpeg.c
        cio.c
        cmarker.c
        fdctflt.c
        rdbmp.c
        huajuan/huajuan_bmp.c
        )
