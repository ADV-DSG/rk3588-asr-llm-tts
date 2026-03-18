#!/bin/bash

# 编译脚本 for Zipformer ASR
# 使用 aarch64-linux-gnu 交叉编译工具链

# 禁用错误退出，以便在Windows环境下能够继续执行
set +e

echo "=========================================="
echo "Zipformer ASR 编译脚本"
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
SCRIPT_DIR=$(pwd)
PROJECT_ROOT=${SCRIPT_DIR}/../..
BUILD_DIR="${SCRIPT_DIR}/build"
INSTALL_DIR="${SCRIPT_DIR}/install"

# 配置目标SOC（根据实际情况修改）
TARGET_SOC="rk3588"
echo "目标SOC：$TARGET_SOC"

# 配置ALSA库路径（使用3rdparty目录下已经编译好的ALSA库）
ALSA_ROOT_DIR="${PROJECT_ROOT}/3rdparty/alsa/install"
ALSA_INCLUDE_DIR="${ALSA_ROOT_DIR}/include"
ALSA_LIBRARY_DIR="${ALSA_ROOT_DIR}/lib/aarch64-linux-gnu"

# 尝试不同的库文件路径
ALSA_LIBRARY=""
if [ -f "${ALSA_LIBRARY_DIR}/libasound.so" ]; then
    ALSA_LIBRARY="${ALSA_LIBRARY_DIR}/libasound.so"
elif [ -f "${ALSA_LIBRARY_DIR}/libasound.so.2" ]; then
    ALSA_LIBRARY="${ALSA_LIBRARY_DIR}/libasound.so.2"
elif [ -f "${ALSA_LIBRARY_DIR}/libasound.so.2.0.0" ]; then
    ALSA_LIBRARY="${ALSA_LIBRARY_DIR}/libasound.so.2.0.0"
elif [ -f "${ALSA_ROOT_DIR}/lib/libasound.so" ]; then
    ALSA_LIBRARY="${ALSA_ROOT_DIR}/lib/libasound.so"
elif [ -f "${ALSA_ROOT_DIR}/lib/libasound.so.2" ]; then
    ALSA_LIBRARY="${ALSA_ROOT_DIR}/lib/libasound.so.2"
elif [ -f "${ALSA_ROOT_DIR}/lib/libasound.so.2.0.0" ]; then
    ALSA_LIBRARY="${ALSA_ROOT_DIR}/lib/libasound.so.2.0.0"
fi

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

if [ -z "${ALSA_LIBRARY}" ]; then
    echo "错误：未找到 ALSA 库文件"
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
cmake ${SCRIPT_DIR} \
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

# 查找实际的 porcupine 目录（处理目录名中的特殊字符）
PORCUPINE_ROOT_DIR=""
for dir in "${PROJECT_ROOT}/3rdparty"/porcupine*; do
    if [ -d "$dir" ]; then
        PORCUPINE_ROOT_DIR="$dir"
        break
    fi
done

if [ -z "$PORCUPINE_ROOT_DIR" ]; then
    echo "警告：未找到 porcupine 目录"
else
    echo "找到 porcupine 目录：$PORCUPINE_ROOT_DIR"
    
    # 拷贝 Porcupine 模型文件
    PORCUPINE_MODEL_SRC="${PORCUPINE_ROOT_DIR}/porcupine-4.0/lib/common"
    if [ -d "${PORCUPINE_MODEL_SRC}" ]; then
        cp -r ${PORCUPINE_MODEL_SRC}/* ${PORCUPINE_MODEL_DIR}/
        echo "已拷贝 Porcupine 模型文件到 ${PORCUPINE_MODEL_DIR}"
    else
        echo "警告：未找到 Porcupine 模型文件目录 ${PORCUPINE_MODEL_SRC}"
        # 尝试其他可能的路径
        PORCUPINE_MODEL_SRC="${PORCUPINE_ROOT_DIR}/porcupine-4.0/lib"
        if [ -d "${PORCUPINE_MODEL_SRC}" ]; then
            find ${PORCUPINE_MODEL_SRC} -name "*.pv" -exec cp {} ${PORCUPINE_MODEL_DIR}/ \;
            echo "已尝试从 ${PORCUPINE_MODEL_SRC} 拷贝 Porcupine 模型文件"
        fi
    fi
    
    # 拷贝 Porcupine 唤醒词文件
    PORCUPINE_KEYWORD_FOUND=false
    # 尝试树莓派版本
    PORCUPINE_KEYWORD_SRC="${PORCUPINE_ROOT_DIR}/porcupine-4.0/resources/keyword_files_zh/raspberry-pi"
    if [ -d "${PORCUPINE_KEYWORD_SRC}" ]; then
        cp -r ${PORCUPINE_KEYWORD_SRC}/* ${PORCUPINE_KEYWORD_DIR}/
        echo "已拷贝 Porcupine zh 唤醒词文件到 ${PORCUPINE_KEYWORD_DIR}"
        PORCUPINE_KEYWORD_FOUND=true
    fi
    PORCUPINE_KEYWORD_SRC="${PORCUPINE_ROOT_DIR}/porcupine-4.0/resources/keyword_files/raspberry-pi"
    if [ -d "${PORCUPINE_KEYWORD_SRC}" ]; then
        cp -r ${PORCUPINE_KEYWORD_SRC}/* ${PORCUPINE_KEYWORD_DIR}/
        echo "已拷贝 Porcupine 唤醒词文件到 ${PORCUPINE_KEYWORD_DIR}"
        PORCUPINE_KEYWORD_FOUND=true
    else
        echo "警告：未找到 Porcupine 唤醒词文件目录 ${PORCUPINE_KEYWORD_SRC}"
        # 尝试Android ARM64版本
        PORCUPINE_KEYWORD_SRC="${PORCUPINE_ROOT_DIR}/porcupine-4.0/resources/keyword_files/android/arm64-v8a"
        if [ -d "${PORCUPINE_KEYWORD_SRC}" ]; then
            cp -r ${PORCUPINE_KEYWORD_SRC}/* ${PORCUPINE_KEYWORD_DIR}/
            echo "已拷贝 Android ARM64 版本的 Porcupine 唤醒词文件到 ${PORCUPINE_KEYWORD_DIR}"
            PORCUPINE_KEYWORD_FOUND=true
        else
            echo "警告：未找到 Android ARM64 版本的 Porcupine 唤醒词文件目录 ${PORCUPINE_KEYWORD_SRC}"
            # 尝试查找任何包含.ppn文件的目录
            PPN_FILE=$(find "${PORCUPINE_ROOT_DIR}" -name "*.ppn" | head -1)
            if [ -n "$PPN_FILE" ]; then
                PPN_DIR=$(dirname "$PPN_FILE")
                cp -r "$PPN_DIR"/* ${PORCUPINE_KEYWORD_DIR}/
                echo "已从 ${PPN_DIR} 拷贝 Porcupine 唤醒词文件"
                PORCUPINE_KEYWORD_FOUND=true
            fi
        fi
    fi
    
    # 创建lib目录
    LIB_DIR="${INSTALL_DIR}/lib"
    mkdir -p ${LIB_DIR}
    
    # 拷贝 Porcupine 库文件
    PORCUPINE_LIB_FOUND=false
    # 尝试多个可能的路径
    PORCUPINE_LIB_PATHS=(
        "${PORCUPINE_ROOT_DIR}/porcupine-4.0/lib/raspberry-pi/cortex-a76-aarch64"
        "${PORCUPINE_ROOT_DIR}/porcupine-4.0/lib/raspberry-pi/cortex-a76"
        "${PORCUPINE_ROOT_DIR}/porcupine-4.0/lib/android/arm64-v8a"
        "${PORCUPINE_ROOT_DIR}/porcupine-4.0/lib/linux/aarch64"
    )
    
    for PORCUPINE_LIB_SRC in "${PORCUPINE_LIB_PATHS[@]}"; do
        if [ -d "${PORCUPINE_LIB_SRC}" ] && [ -f "${PORCUPINE_LIB_SRC}/libpv_porcupine.so" ]; then
            cp -r ${PORCUPINE_LIB_SRC}/* ${PORCUPINE_LIB_DIR}/
            echo "已拷贝 Porcupine 库文件到 ${PORCUPINE_LIB_DIR}"
            
            # 同时拷贝到lib目录，确保运行时能够找到
            cp -r ${PORCUPINE_LIB_SRC}/* ${LIB_DIR}/
            echo "已拷贝 Porcupine 库文件到 ${LIB_DIR}"
            
            PORCUPINE_LIB_FOUND=true
            break
        fi
    done
    
    if [ "$PORCUPINE_LIB_FOUND" = false ]; then
        echo "警告：未找到 Porcupine 库文件 libpv_porcupine.so"
        # 尝试查找任何包含libpv_porcupine.so的目录
        LIB_FILE=$(find "${PORCUPINE_ROOT_DIR}" -name "libpv_porcupine.so" | head -1)
        if [ -n "$LIB_FILE" ]; then
            LIB_DIR=$(dirname "$LIB_FILE")
            cp -r "$LIB_FILE" ${PORCUPINE_LIB_DIR}/
            cp -r "$LIB_FILE" ${LIB_DIR}/
            echo "已从 ${LIB_DIR} 拷贝 Porcupine 库文件"
        fi
    fi
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
    porcupine/model/porcupine_params_zh.pv \
    porcupine/keyword/电脑_zh_raspberry-pi_v4_0_0.ppn \
    0.5
EOF

chmod +x ${INSTALL_DIR}/run_wakeword_demo.sh
echo "已创建运行脚本 ${INSTALL_DIR}/run_wakeword_demo.sh"

# 回到脚本目录
cd ${SCRIPT_DIR}

echo "=========================================="
echo "编译完成！"
echo "=========================================="
echo "构建目录：${BUILD_DIR}"
echo "安装目录：${INSTALL_DIR}"
echo "可执行文件：${INSTALL_DIR}/rknn_zipformer_demo 和 ${INSTALL_DIR}/rknn_zipformer_demo_mic"
echo "Porcupine 文件：${PORCUPINE_DIR}"
echo "=========================================="
