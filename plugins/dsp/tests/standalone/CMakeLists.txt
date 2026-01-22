cmake_minimum_required(VERSION 3.16)
project(LocalgalComprehensiveTest)

# Find JUCE at different possible locations
set(JUCE_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../external/JUCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/JUCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../external/JUCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../external/JUCE
)

set(JUCE_PATH "")
foreach(PATH ${JUCE_PATHS})
    if(EXISTS ${PATH})
        set(JUCE_PATH ${PATH})
        break()
    endif()
endforeach()

if(NOT JUCE_PATH OR NOT EXISTS ${JUCE_PATH})
    message(STATUS "JUCE not found in standard locations, building without JUCE")
    set(JUCE_PATH "")
endif()

# Add executable
add_executable(LocalgalComprehensiveTest
    LocalgalComprehensiveTest.cpp
    ../../src/dsp/LocalgalPureDSP.cpp
    ../../../../../include/dsp/LookupTables.cpp
)

# Include directories
target_include_directories(LocalgalComprehensiveTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../include
    ${JUCE_PATH}/modules
)

# C++ standard
target_compile_features(LocalgalComprehensiveTest PRIVATE cxx_std_17)

# Link libraries
target_link_libraries(LocalgalComprehensiveTest PRIVATE
    "-framework Accelerate"
    "-framework CoreFoundation"
    "-framework CoreMIDI"
    "-framework CoreAudio"
)
