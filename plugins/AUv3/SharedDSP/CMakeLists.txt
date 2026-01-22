cmake_minimum_required(VERSION 3.15)

project(LocalgalDSP)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add localgal DSP source
set(LOCALGAL_MARCO_DSP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(LOCALGAL_MARCO_DSP_SRC "${LOCALGAL_MARCO_DSP_ROOT}/plugins/dsp/src/dsp")
set(LOCALGAL_MARCO_DSP_INCLUDE "${LOCALGAL_MARCO_DSP_ROOT}/plugins/dsp/include")

# Create static library
add_library(LocalgalDSP STATIC

    # Wrapper files
    LocalgalDSP.cpp
    LocalgalDSP.h

    # Localgal Marco DSP implementation
    ${LOCALGAL_MARCO_DSP_SRC}/LocalgalPureDSP.cpp
)

# Include paths
target_include_directories(LocalgalDSP PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LOCALGAL_MARCO_DSP_INCLUDE}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../include/dsp"
)

# Compiler flags for iOS
target_compile_options(LocalgalDSP PUBLIC
    -fobjc-arc
    -fembed-bitcode
)

# Architecture settings for iOS
if(IOS_PLATFORM)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0")
endif()
