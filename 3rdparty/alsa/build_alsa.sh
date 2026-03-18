#!/bin/bash

# ALSA Library Cross Compilation Script
# Compiles ALSA library for aarch64-linux-gnu target

set -e

echo "=========================================="
echo "ALSA Library Cross Compilation Script"
echo "=========================================="

# Configuration
ALSA_VERSION="1.2.10"
ALSA_URL="https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2"
CROSS_COMPILE=aarch64-linux-gnu-
BUILD_DIR="${PWD}/build_alsa"
INSTALL_DIR="${PWD}/install"

echo "ALSA Version: ${ALSA_VERSION}"
echo "Cross Compile: ${CROSS_COMPILE}"
echo "Build Directory: ${BUILD_DIR}"
echo "Install Directory: ${INSTALL_DIR}"

# Check cross compiler
echo "Checking cross compiler..."
if ! command -v ${CROSS_COMPILE}gcc &> /dev/null; then
    echo "Error: ${CROSS_COMPILE}gcc not found!"
    echo "Please install cross compiler: sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    exit 1
fi

echo "Cross compiler found: $(which ${CROSS_COMPILE}gcc)"

# Create directories
echo "Creating directories..."
# rm -rf ${BUILD_DIR} ${INSTALL_DIR}
mkdir -p ${BUILD_DIR} ${INSTALL_DIR}

# Download ALSA source
echo "Downloading ALSA source..."
# wget -O ${BUILD_DIR}/alsa-lib-${ALSA_VERSION}.tar.bz2 ${ALSA_URL}
# 使用引号包围变量
if [ ! -e "${BUILD_DIR}/alsa-lib-${ALSA_VERSION}.tar.bz2" ]; then
    echo "${BUILD_DIR}/alsa-lib-${ALSA_VERSION}.tar.bz2"
    echo "文件不存在，开始下载..."
    wget -O "${BUILD_DIR}/alsa-lib-${ALSA_VERSION}.tar.bz2" "${ALSA_URL}"
else
    echo "文件已存在，跳过下载"
fi
# Extract source
echo "Extracting source..."
tar -xf ${BUILD_DIR}/alsa-lib-${ALSA_VERSION}.tar.bz2 -C ${BUILD_DIR}

# Enter source directory
cd ${BUILD_DIR}/alsa-lib-${ALSA_VERSION}

# Configure for cross compilation
echo "Configuring for cross compilation..."
./configure \
    --host=aarch64-linux-gnu \
    --prefix=${INSTALL_DIR} \
    --disable-python \
    --disable-debug \
    CC=${CROSS_COMPILE}gcc \
    CXX=${CROSS_COMPILE}g++ \
    AR=${CROSS_COMPILE}ar \
    RANLIB=${CROSS_COMPILE}ranlib \
    STRIP=${CROSS_COMPILE}strip

# Compile
echo "Compiling ALSA library..."
make -j$(nproc)

# Install
echo "Installing to ${INSTALL_DIR}..."
make install

# Create directory structure for project
echo "Creating project directory structure..."
mkdir -p ${INSTALL_DIR}/lib/aarch64-linux-gnu

# Copy libraries to standard location
echo "Copying libraries..."

# Check where libraries were actually installed
LIBRARY_SOURCE_DIR="${INSTALL_DIR}/lib"
HEADER_SOURCE_DIR="${INSTALL_DIR}/include"

# Check if libraries are in install/lib instead (some configure scripts do this)
if [ ! -f "${LIBRARY_SOURCE_DIR}/libasound.so" ] && [ -d "${INSTALL_DIR}/install" ]; then
    echo "Libraries found in ${INSTALL_DIR}/install/ subdirectory"
    
    # Check if libraries are in install/lib
    if [ -f "${INSTALL_DIR}/install/lib/libasound.so" ]; then
        LIBRARY_SOURCE_DIR="${INSTALL_DIR}/install/lib"
    fi
    
    # Check if headers are in install/include
    if [ -d "${INSTALL_DIR}/install/include/alsa" ]; then
        HEADER_SOURCE_DIR="${INSTALL_DIR}/install/include"
    fi
fi

# Debug: show library search path
echo "Looking for libraries in: ${LIBRARY_SOURCE_DIR}"
echo "Looking for headers in: ${HEADER_SOURCE_DIR}"

# List contents for debugging
echo "Contents of ${LIBRARY_SOURCE_DIR}:",
ls -la ${LIBRARY_SOURCE_DIR}

# Check if libraries exist
if [ ! -f "${LIBRARY_SOURCE_DIR}/libasound.so" ]; then
    echo "Error: ALSA libraries not found!"
    echo "Contents of ${INSTALL_DIR}:",
    ls -la ${INSTALL_DIR}
    exit 1
fi

# Copy the libraries
echo "Copying shared libraries..."
cp -a ${LIBRARY_SOURCE_DIR}/libasound.so* ${INSTALL_DIR}/lib/aarch64-linux-gnu/ || echo "Error copying shared libraries!"

echo "Copying static libraries..."
cp -a ${LIBRARY_SOURCE_DIR}/libasound.a ${INSTALL_DIR}/lib/aarch64-linux-gnu/ 2>/dev/null || echo "Note: libasound.a not found, skipping static library"

# Copy header files if they're in the wrong location
if [ "${HEADER_SOURCE_DIR}" != "${INSTALL_DIR}/include" ]; then
    echo "Copying headers from ${HEADER_SOURCE_DIR} to ${INSTALL_DIR}/include..."
    mkdir -p ${INSTALL_DIR}/include
    cp -a ${HEADER_SOURCE_DIR}/alsa ${INSTALL_DIR}/include/ || echo "Error copying headers!"
fi

# Create CMake configuration file
echo "Creating CMake configuration file..."
cat > ${INSTALL_DIR}/alsa-config.cmake << EOF
# ALSA library configuration for CMake
set(ALSA_FOUND TRUE)
set(ALSA_INCLUDE_DIRS ${INSTALL_DIR}/include)
set(ALSA_LIBRARIES ${INSTALL_DIR}/lib/aarch64-linux-gnu/libasound.so)
set(ALSA_LIBRARY_DIRS ${INSTALL_DIR}/lib/aarch64-linux-gnu)
EOF

echo "=========================================="
echo "ALSA Library Compilation Complete!"
echo "=========================================="
echo "Installed to: ${INSTALL_DIR}"
echo "Include files: ${INSTALL_DIR}/include/alsa"
echo "Library files: ${INSTALL_DIR}/lib/aarch64-linux-gnu"
echo "CMake config: ${INSTALL_DIR}/alsa-config.cmake"
echo ""
echo "To use in your project:"
echo "1. Add ALSA_INCLUDE_DIRS to your include directories"
echo "2. Link with ALSA_LIBRARIES"
echo "3. Or include the CMake config file"
echo ""
echo "Example CMake usage:"
echo "set(ALSA_INCLUDE_DIR ${INSTALL_DIR}/include)"
echo "set(ALSA_LIBRARY ${INSTALL_DIR}/lib/aarch64-linux-gnu/libasound.so)"
echo "target_include_directories(your_target PRIVATE {ALSA_INCLUDE_DIR})"
echo "target_link_libraries(your_target {ALSA_LIBRARY})"
echo "=========================================="