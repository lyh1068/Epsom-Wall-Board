cmake_minimum_required(VERSION 3.16)

# "Trim" the build to main and whatever it depends on. main has no explicit
# REQUIRES, so it sees every component in the build; json (cJSON) is listed
# because nothing else pulls it in, and espcoredump for the crash summary.
set(COMPONENTS main json espcoredump)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(epsom_wallboard)
