cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET "esp32s3" CACHE STRING "Target chip" FORCE)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sticks3_chat)
