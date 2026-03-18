cmake_minimum_required(VERSION 3.15)

project(rknn_model_zoo_utils)

# 控制是否启用 Porcupine 支持（默认启用）
option(ENABLE_PORCUPINE "Enable Porcupine wake word detection support" ON)

message(STATUS "ENABLE_PORCUPINE: ${ENABLE_PORCUPINE}")

add_library(fileutils STATIC
    file_utils.c
)
target_include_directories(fileutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(imagedrawing STATIC
    image_drawing.c
)
target_include_directories(imagedrawing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(DISABLE_RGA AND NOT (TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rv1103" OR TARGET_SOC STREQUAL "rv1103b"))
    add_definitions(-DDISABLE_RGA)
endif ()

# only RGA on rv1106 and rk3588 support handle
if (TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rk3588")
    add_definitions(-DLIBRGA_IM2D_HANDLE)
endif()

add_library(imageutils STATIC
    image_utils.c
)

target_include_directories(imageutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${STB_INCLUDES}
    ${LIBRGA_INCLUDES}
)

target_link_libraries(imageutils
    ${LIBRGA}
)

if (DISABLE_LIBJPEG)
    add_definitions(-DDISABLE_LIBJPEG)
else()
    target_link_libraries(imageutils
        ${LIBJPEG}
    )
    target_include_directories(imageutils PUBLIC
        ${LIBJPEG_INCLUDES}
    )
endif()

add_library(audioutils STATIC
    audio_utils.c
    wake_word_detector.c
)

# Use pre-built ALSA library from 3rdparty directory
set(ALSA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/alsa/install")
set(ALSA_INCLUDE_DIR "${ALSA_ROOT_DIR}/include")
set(ALSA_LIBRARY_DIR "${ALSA_ROOT_DIR}/lib/aarch64-linux-gnu")
set(ALSA_LIBRARY "${ALSA_LIBRARY_DIR}/libasound.so")
set(ALSA_STATIC_LIBRARY "${ALSA_LIBRARY_DIR}/libasound.a")

# Check if ALSA library exists
if(EXISTS ${ALSA_LIBRARY} AND EXISTS ${ALSA_INCLUDE_DIR}/alsa/asoundlib.h)
    message(STATUS "Using pre-built ALSA library:")
    message(STATUS "  ALSA Include: ${ALSA_INCLUDE_DIR}")
    message(STATUS "  ALSA Library: ${ALSA_LIBRARY}")
    message(STATUS "  ALSA Static Library: ${ALSA_STATIC_LIBRARY}")
    
    # Enable ALSA support
    target_compile_definitions(audioutils PRIVATE HAVE_ALSA)
    
    # Link with ALSA library (dynamic by default)
    target_link_libraries(audioutils ${ALSA_LIBRARY})
    
    # Include ALSA headers
    target_include_directories(audioutils PUBLIC ${ALSA_INCLUDE_DIR})
    
    # Add ALSA paths to CMake cache for external use
    set(ALSA_LIBRARY ${ALSA_LIBRARY} CACHE INTERNAL "ALSA library path")
    set(ALSA_STATIC_LIBRARY ${ALSA_STATIC_LIBRARY} CACHE INTERNAL "ALSA static library path")
    set(ALSA_INCLUDE_DIR ${ALSA_INCLUDE_DIR} CACHE INTERNAL "ALSA include path")
    set(ALSA_LIBRARY_DIR ${ALSA_LIBRARY_DIR} CACHE INTERNAL "ALSA library directory")
    set(ALSA_FOUND TRUE CACHE INTERNAL "ALSA found flag")
else()
    message(STATUS "ALSA library not found at ${ALSA_ROOT_DIR}, microphone support will be disabled")
    message(STATUS "Please run build_alsa.sh first to compile ALSA library")
    set(ALSA_FOUND FALSE CACHE INTERNAL "ALSA found flag")
    # Don't link ALSA, use dummy implementation
endif()

# Use pre-built Porcupine library from 3rdparty directory
if(ENABLE_PORCUPINE)
    set(PORCUPINE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/porcupine‌/porcupine-4.0")
    set(PORCUPINE_INCLUDE_DIR "${PORCUPINE_ROOT_DIR}/include")

    # For RK3588 (ARM Cortex-A76 aarch64)
    set(PORCUPINE_LIBRARY_DIR "${PORCUPINE_ROOT_DIR}/lib/raspberry-pi/cortex-a76-aarch64")
    set(PORCUPINE_LIBRARY "${PORCUPINE_LIBRARY_DIR}/libpv_porcupine.so")
    set(PORCUPINE_PARAMS_DIR "${PORCUPINE_ROOT_DIR}/lib/common")

    # Check if Porcupine library exists
    if(EXISTS ${PORCUPINE_LIBRARY} AND EXISTS ${PORCUPINE_INCLUDE_DIR}/pv_porcupine.h)
        message(STATUS "Using pre-built Porcupine library:")
        message(STATUS "  Porcupine Include: ${PORCUPINE_INCLUDE_DIR}")
        message(STATUS "  Porcupine Library: ${PORCUPINE_LIBRARY}")
        message(STATUS "  Porcupine Params Dir: ${PORCUPINE_PARAMS_DIR}")
        
        # Enable Porcupine support
        target_compile_definitions(audioutils PRIVATE HAVE_PORCUPINE)
        
        # Link with Porcupine library
        target_link_libraries(audioutils ${PORCUPINE_LIBRARY})
        
        # Include Porcupine headers
        target_include_directories(audioutils PUBLIC ${PORCUPINE_INCLUDE_DIR})
        
        # Add Porcupine paths to CMake cache for external use
        set(PORCUPINE_LIBRARY ${PORCUPINE_LIBRARY} CACHE INTERNAL "Porcupine library path")
        set(PORCUPINE_INCLUDE_DIR ${PORCUPINE_INCLUDE_DIR} CACHE INTERNAL "Porcupine include path")
        set(PORCUPINE_PARAMS_DIR ${PORCUPINE_PARAMS_DIR} CACHE INTERNAL "Porcupine parameters directory")
        set(PORCUPINE_FOUND TRUE CACHE INTERNAL "Porcupine found flag")
    else()
        message(STATUS "Porcupine library not found at ${PORCUPINE_ROOT_DIR}, wake word detection will be disabled")
        set(PORCUPINE_FOUND FALSE CACHE INTERNAL "Porcupine found flag")
    endif()
else()
    message(STATUS "Porcupine support disabled (ENABLE_PORCUPINE=OFF)")
    set(PORCUPINE_FOUND FALSE CACHE INTERNAL "Porcupine found flag")
endif()

target_link_libraries(audioutils
    ${LIBSNDFILE}
)

target_include_directories(audioutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBSNDFILE_INCLUDES}
)