#!/bin/bash

# 交叉编译脚本 for RKNN Zipformer ASR
# 使用 aarch64-linux-gnu 交叉编译工具链

# 禁用错误退出，以便在Windows环境下能够继续执行
set +e

echo "=========================================="
echo "RKNN Zipformer ASR 交叉编译脚本"
echo "=========================================="

# 配置交叉编译环境
CROSS_COMPILE=aarch64-linux-gnu-
CROSS_COMPILE_PATH=$(which ${CROSS_COMPILE}gcc | sed 's/\/bin\/aarch64-linux-gnu-gcc//')

# 检查交叉编译工具是否存在
if [ -z "$CROSS_COMPILE_PATH" ]; then
    echo "错误：未找到 aarch64-linux-gnu-gcc 交叉编译工具！"
    echo "请先安装交叉编译工具链：sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    exit 1
fi

echo "交叉编译工具路径：$CROSS_COMPILE_PATH"
echo "交叉编译前缀：$CROSS_COMPILE"

# 配置项目路径
PROJECT_ROOT=$(pwd)
BUILD_DIR="${PROJECT_ROOT}/build_cross"
INSTALL_DIR="${PROJECT_ROOT}/install_cross"

# 配置目标SOC（根据实际情况修改）
TARGET_SOC="rk3588"
echo "目标SOC：$TARGET_SOC"

# 配置ALSA库路径（使用3rdparty目录下已经编译好的ALSA库）
ALSA_ROOT_DIR="${PROJECT_ROOT}/3rdparty/alsa/install"
ALSA_INCLUDE_DIR="${ALSA_ROOT_DIR}/include"
ALSA_LIBRARY_DIR="${ALSA_ROOT_DIR}/lib/aarch64-linux-gnu"
ALSA_LIBRARY="${ALSA_LIBRARY_DIR}/libasound.so"

# 检查ALSA库是否存在
if [ ! -d "${ALSA_ROOT_DIR}" ]; then
    echo "错误：未找到 ALSA 库目录 ${ALSA_ROOT_DIR}"
    echo "请先运行 build_alsa.sh 编译 ALSA 库"
    exit 1
fi

if [ ! -f "${ALSA_INCLUDE_DIR}/alsa/asoundlib.h" ]; then
    echo "错误：未找到 ALSA 头文件 ${ALSA_INCLUDE_DIR}/alsa/asoundlib.h"
    echo "请先运行 build_alsa.sh 编译 ALSA 库"
    exit 1
fi

if [ ! -f "${ALSA_LIBRARY}" ]; then
    echo "错误：未找到 ALSA 库文件 ${ALSA_LIBRARY}"
    echo "请先运行 build_alsa.sh 编译 ALSA 库"
    exit 1
fi

echo "使用本地编译的 ALSA 库："
echo "ALSA 头文件路径：${ALSA_INCLUDE_DIR}"
echo "ALSA 库文件路径：${ALSA_LIBRARY}"

# 清理旧的构建目录
echo "清理旧的构建目录..."
rm -rf ${BUILD_DIR} ${INSTALL_DIR}
mkdir -p ${BUILD_DIR} ${INSTALL_DIR}

# 进入构建目录
cd ${BUILD_DIR}

# 运行 CMake 配置
echo "运行 CMake 配置..."
cmake ${PROJECT_ROOT}/examples/zipformer/cpp \
    -DCMAKE_TOOLCHAIN_FILE=${PROJECT_ROOT}/toolchain.cmake \
    -DTARGET_SOC=${TARGET_SOC} \
    -DCMAKE_C_COMPILER=${CROSS_COMPILE}gcc \
    -DCMAKE_CXX_COMPILER=${CROSS_COMPILE}g++ \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
    -DCMAKE_BUILD_TYPE=Release \
    -DLIBRKNNRT_LIB=${PROJECT_ROOT}/3rdparty/rknpu2/lib/Linux/aarch64/librknnrt.so \
    -DLIBRKNNRT_INCLUDES=${PROJECT_ROOT}/3rdparty/rknpu2/include \
    -DALSA_INCLUDE_DIR=${ALSA_INCLUDE_DIR} \
    -DALSA_LIBRARY=${ALSA_LIBRARY} \
    -DBUILD_SHARED_LIBS=OFF

# 编译
echo "开始编译..."
make -j$(nproc)

# 安装
echo "安装到 ${INSTALL_DIR}..."
make install

# 拷贝 Porcupine 相关文件
echo "拷贝 Porcupine 相关文件..."

# 创建 Porcupine 目录结构
PORCUPINE_DIR="${INSTALL_DIR}/porcupine"
PORCUPINE_MODEL_DIR="${PORCUPINE_DIR}/model"
PORCUPINE_KEYWORD_DIR="${PORCUPINE_DIR}/keyword"
PORCUPINE_LIB_DIR="${PORCUPINE_DIR}/lib"

mkdir -p ${PORCUPINE_MODEL_DIR} ${PORCUPINE_KEYWORD_DIR} ${PORCUPINE_LIB_DIR}

# 拷贝 Porcupine 模型文件
PORCUPINE_MODEL_SRC="${PROJECT_ROOT}/3rdparty/porcupine/porcupine-4.0/lib/common"
if [ -d "${PORCUPINE_MODEL_SRC}" ]; then
    cp -r ${PORCUPINE_MODEL_SRC}/* ${PORCUPINE_MODEL_DIR}/
    echo "已拷贝 Porcupine 模型文件到 ${PORCUPINE_MODEL_DIR}"
else
    echo "警告：未找到 Porcupine 模型文件目录 ${PORCUPINE_MODEL_SRC}"
fi

# 拷贝 Porcupine 唤醒词文件（使用树莓派版本，适用于ARM架构）
PORCUPINE_KEYWORD_SRC="${PROJECT_ROOT}/3rdparty/porcupine/porcupine-4.0/resources/keyword_files/raspberry-pi"
if [ -d "${PORCUPINE_KEYWORD_SRC}" ]; then
    cp -r ${PORCUPINE_KEYWORD_SRC}/* ${PORCUPINE_KEYWORD_DIR}/
    echo "已拷贝 Porcupine 唤醒词文件到 ${PORCUPINE_KEYWORD_DIR}"
else
    echo "警告：未找到 Porcupine 唤醒词文件目录 ${PORCUPINE_KEYWORD_SRC}"
    # 尝试使用Android ARM64版本作为备选
    PORCUPINE_KEYWORD_SRC="${PROJECT_ROOT}/3rdparty/porcupine/porcupine-4.0/resources/keyword_files/android/arm64-v8a"
    if [ -d "${PORCUPINE_KEYWORD_SRC}" ]; then
        cp -r ${PORCUPINE_KEYWORD_SRC}/* ${PORCUPINE_KEYWORD_DIR}/
        echo "已拷贝 Android ARM64 版本的 Porcupine 唤醒词文件到 ${PORCUPINE_KEYWORD_DIR}"
    else
        echo "警告：未找到 Android ARM64 版本的 Porcupine 唤醒词文件目录 ${PORCUPINE_KEYWORD_SRC}"
    fi
fi

# 创建lib目录
LIB_DIR="${INSTALL_DIR}/lib"
mkdir -p ${LIB_DIR}

# 拷贝 Porcupine 库文件（ARM 64位版本）
PORCUPINE_LIB_SRC="${PROJECT_ROOT}/3rdparty/porcupine/porcupine-4.0/lib/raspberry-pi/cortex-a76-aarch64"
if [ -d "${PORCUPINE_LIB_SRC}" ]; then
    cp -r ${PORCUPINE_LIB_SRC}/* ${PORCUPINE_LIB_DIR}/
    echo "已拷贝 Porcupine 库文件到 ${PORCUPINE_LIB_DIR}"
    
    # 同时拷贝到lib目录，确保运行时能够找到
    cp -r ${PORCUPINE_LIB_SRC}/* ${LIB_DIR}/
    echo "已拷贝 Porcupine 库文件到 ${LIB_DIR}"
else
    echo "警告：未找到 Porcupine 库文件目录 ${PORCUPINE_LIB_SRC}"
fi

# 创建运行脚本，方便在设备上运行
echo "创建运行脚本..."
cat > ${INSTALL_DIR}/run_wakeword_demo.sh << 'EOF'
#!/bin/bash

# 设置库文件路径
export LD_LIBRARY_PATH=".:./lib:$LD_LIBRARY_PATH"

# 运行带唤醒词检测的语音识别演示
./rknn_zipformer_demo_mic \
    model/encoder-epoch-99-avg-1.rknn \
    model/decoder-epoch-99-avg-1.rknn \
    model/joiner-epoch-99-avg-1.rknn \
    porcupine/model/porcupine_params.pv \
    porcupine/keyword/porcupine_linux.ppn \
    0.5
EOF

chmod +x ${INSTALL_DIR}/run_wakeword_demo.sh
echo "已创建运行脚本 ${INSTALL_DIR}/run_wakeword_demo.sh"

# 编译 mms_tts 项目
echo "=========================================="
echo "开始编译 mms_tts 项目..."
echo "=========================================="

# 配置 mms_tts 构建目录
MMS_TTS_BUILD_DIR="${BUILD_DIR}/mms_tts"
MMS_TTS_INSTALL_DIR="${INSTALL_DIR}/mms_tts"
mkdir -p ${MMS_TTS_BUILD_DIR}

# 进入 mms_tts 构建目录
cd ${MMS_TTS_BUILD_DIR}

# 运行 CMake 配置
echo "运行 CMake 配置 for mms_tts..."
cmake ${PROJECT_ROOT}/examples/mms_tts/cpp \
    -DCMAKE_TOOLCHAIN_FILE=${PROJECT_ROOT}/toolchain.cmake \
    -DTARGET_SOC=${TARGET_SOC} \
    -DCMAKE_C_COMPILER=${CROSS_COMPILE}gcc \
    -DCMAKE_CXX_COMPILER=${CROSS_COMPILE}g++ \
    -DCMAKE_INSTALL_PREFIX=${MMS_TTS_INSTALL_DIR} \
    -DCMAKE_BUILD_TYPE=Release \
    -DLIBRKNNRT_LIB=${PROJECT_ROOT}/3rdparty/rknpu2/lib/Linux/aarch64/librknnrt.so \
    -DLIBRKNNRT_INCLUDES=${PROJECT_ROOT}/3rdparty/rknpu2/include \
    -DALSA_INCLUDE_DIR=${ALSA_INCLUDE_DIR} \
    -DALSA_LIBRARY=${ALSA_LIBRARY} \
    -DBUILD_SHARED_LIBS=OFF

# 编译
echo "开始编译 mms_tts..."
make -j$(nproc)

# 安装
echo "安装 mms_tts 到 ${MMS_TTS_INSTALL_DIR}..."
make install

# 拷贝 mms_tts 模型文件
echo "拷贝 mms_tts 模型文件..."
MMS_TTS_MODEL_SRC="${PROJECT_ROOT}/examples/mms_tts/model"
if [ -d "${MMS_TTS_MODEL_SRC}" ]; then
    MMS_TTS_MODEL_DEST="${MMS_TTS_INSTALL_DIR}/model"
    mkdir -p ${MMS_TTS_MODEL_DEST}
    cp -r ${MMS_TTS_MODEL_SRC}/*.rknn ${MMS_TTS_MODEL_DEST}/
    echo "已拷贝 mms_tts 模型文件到 ${MMS_TTS_MODEL_DEST}"
else
    echo "警告：未找到 mms_tts 模型文件目录 ${MMS_TTS_MODEL_SRC}"
fi

# 创建 mms_tts 运行脚本
echo "创建 mms_tts 运行脚本..."
cat > ${MMS_TTS_INSTALL_DIR}/run_mms_tts.sh << 'EOF'
#!/bin/bash

# 设置库文件路径
export LD_LIBRARY_PATH="../lib:$LD_LIBRARY_PATH"

# 运行文本转语音演示
./rknn_mms_tts_demo \
    model/mms_tts_eng_encoder_200.rknn \
    model/mms_tts_eng_decoder_200.rknn \
    "Hello, this is a text to speech demonstration."
EOF

chmod +x ${MMS_TTS_INSTALL_DIR}/run_mms_tts.sh
echo "已创建 mms_tts 运行脚本 ${MMS_TTS_INSTALL_DIR}/run_mms_tts.sh"

# 拷贝 Porcupine 库文件到 mms_tts 目录（解决依赖问题）
echo "拷贝 Porcupine 库文件到 mms_tts 目录..."
MMS_TTS_LIB_DIR="${MMS_TTS_INSTALL_DIR}/lib"
mkdir -p ${MMS_TTS_LIB_DIR}

if [ -d "${LIB_DIR}" ] && [ -f "${LIB_DIR}/libpv_porcupine.so" ]; then
    cp -r ${LIB_DIR}/libpv_porcupine.so ${MMS_TTS_LIB_DIR}/
    echo "已拷贝 Porcupine 库文件到 ${MMS_TTS_LIB_DIR}"
else
    echo "警告：未找到 Porcupine 库文件 ${LIB_DIR}/libpv_porcupine.so"
    echo "跳过拷贝 Porcupine 库文件到 mms_tts 目录"
fi

# 跳过 espeak-ng 编译（根据用户要求）
echo "=========================================="
echo "跳过 espeak-ng 编译..."
echo "=========================================="
echo "根据用户要求，跳过 espeak-ng 项目的编译"
echo "如果需要使用 espeak-ng，请在 RK3588 上直接安装："
echo "sudo apt-get install espeak-ng"

# 回到项目根目录
cd ${PROJECT_ROOT}

echo "=========================================="
echo "交叉编译完成！"
echo "=========================================="
echo "构建目录：${BUILD_DIR}"
echo "安装目录：${INSTALL_DIR}"
echo "可执行文件：${INSTALL_DIR}/rknn_zipformer_demo 和 ${INSTALL_DIR}/rknn_zipformer_demo_mic"
echo "Porcupine 文件：${PORCUPINE_DIR}"
echo "mms_tts 可执行文件：${MMS_TTS_INSTALL_DIR}/rknn_mms_tts_demo"
echo "=========================================="
