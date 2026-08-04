cmake_minimum_required(VERSION 3.22.1)
project(technomatic_2105)

find_package(oboe REQUIRED CONFIG)

add_library(technomatic_2105 SHARED
        native-lib.cpp
        AudioEngine.cpp
        MusicEngine.cpp)

target_include_directories(technomatic_2105 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(technomatic_2105
        PRIVATE
        oboe::oboe
        android
        log)
