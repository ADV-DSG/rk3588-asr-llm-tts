# Toolchain file for cross-compiling to ARM64 on Ubuntu

# Set the CMake minimum version
cmake_minimum_required(VERSION 3.10)

# Set the target system name
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Specify the cross compiler
set(CROSS_COMPILE aarch64-linux-gnu-)
set(CMAKE_C_COMPILER ${CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)
set(CMAKE_LINKER ${CROSS_COMPILE}ld)
set(CMAKE_AR ${CROSS_COMPILE}ar)
set(CMAKE_NM ${CROSS_COMPILE}nm)
set(CMAKE_OBJCOPY ${CROSS_COMPILE}objcopy)
set(CMAKE_OBJDUMP ${CROSS_COMPILE}objdump)
set(CMAKE_STRIP ${CROSS_COMPILE}strip)
set(CMAKE_RANLIB ${CROSS_COMPILE}ranlib)

# Set the sysroot path if needed
# set(CMAKE_SYSROOT /path/to/sysroot)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# For libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Set compiler flags
set(CMAKE_C_FLAGS "-fPIC -march=armv8-a+fp+simd" CACHE STRING "C flags")
set(CMAKE_CXX_FLAGS "-fPIC -march=armv8-a+fp+simd" CACHE STRING "C++ flags")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,-rpath-link,${CMAKE_SYSROOT}/lib/aarch64-linux-gnu" CACHE STRING "Linker flags")

# Set build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()