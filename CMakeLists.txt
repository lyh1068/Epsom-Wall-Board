cmake_minimum_required(VERSION 3.16)

# "Trim" the build to main and whatever it depends on.
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(epsom_wallboard)
