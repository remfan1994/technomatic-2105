cmake_minimum_required(VERSION 3.22.1)
project(radio_breaker)

find_package(oboe REQUIRED CONFIG)

add_library(radio_breaker SHARED
        native-lib.cpp
        AudioEngine.cpp
        MusicEngine.cpp)

target_include_directories(radio_breaker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(radio_breaker
        PRIVATE
        oboe::oboe
        android
        log)
