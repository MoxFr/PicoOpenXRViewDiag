cmake_minimum_required(VERSION 3.20)

project(PicoOpenXRViewDiag LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(
    OpenXR_SDK
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK-Source.git
    GIT_TAG main
)

FetchContent_MakeAvailable(OpenXR_SDK)

add_library(
    PicoOpenXRViewDiag
    SHARED
    src/layer.cpp
)

target_compile_features(
    PicoOpenXRViewDiag
    PRIVATE
    cxx_std_17
)

target_include_directories(
    PicoOpenXRViewDiag
    PRIVATE
    ${openxr_sdk_SOURCE_DIR}/include
    ${openxr_sdk_SOURCE_DIR}/src
    ${openxr_sdk_SOURCE_DIR}/src/loader
)

target_compile_definitions(
    PicoOpenXRViewDiag
    PRIVATE
    XR_USE_PLATFORM_WIN32
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

set_target_properties(
    PicoOpenXRViewDiag
    PROPERTIES
    OUTPUT_NAME "PicoOpenXRViewDiag"
)
