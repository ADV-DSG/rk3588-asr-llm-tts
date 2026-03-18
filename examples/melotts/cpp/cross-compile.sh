#!/bin/bash

# 交叉编译脚本 for melotts
# 使用 aarch64-linux-gnu 交叉编译工具链

set -e

echo "=========================================="
echo "melotts 交叉编译脚本"
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
# 使用脚本所在目录来计算路径，而不是当前工作目录
SCRIPT_DIR=$(dirname "$0")
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../../.." && pwd)
MELOTTS_DIR="${PROJECT_ROOT}/examples/melotts"
BUILD_DIR="${MELOTTS_DIR}/cpp/build/build_cross"
INSTALL_DIR="${MELOTTS_DIR}/cpp/install_cross"

# 配置目标SOC（根据实际情况修改）
TARGET_SOC="rk3588"
echo "目标SOC：$TARGET_SOC"

# 配置ALSA库路径（使用3rdparty目录下已经编译好的ALSA库）
ALSA_ROOT_DIR="${PROJECT_ROOT}/3rdparty/alsa/install"
ALSA_INCLUDE_DIR="${ALSA_ROOT_DIR}/include"
ALSA_LIBRARY_DIR="${ALSA_ROOT_DIR}/lib"
ALSA_LIBRARY="${ALSA_LIBRARY_DIR}/libasound.so.2.0.0"

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
echo "源代码路径：${MELOTTS_DIR}/cpp"
echo "构建目录：${BUILD_DIR}"
cmake "${MELOTTS_DIR}/cpp" \
    -DCMAKE_TOOLCHAIN_FILE=${PROJECT_ROOT}/toolchain.cmake \
    -DTARGET_SOC=${TARGET_SOC} \
    -DCMAKE_C_COMPILER=${CROSS_COMPILE}gcc \
    -DCMAKE_CXX_COMPILER=${CROSS_COMPILE}g++ \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
    -DCMAKE_BUILD_TYPE=Release \
    -DLIBRKNNRT_LIB=${PROJECT_ROOT}/3rdparty/rknpu2/Linux/aarch64/librknnrt.so \
    -DLIBRKNNRT_INCLUDES=${PROJECT_ROOT}/3rdparty/rknpu2/include \
    -DALSA_INCLUDE_DIR=${ALSA_INCLUDE_DIR} \
    -DALSA_LIBRARY=${ALSA_LIBRARY} \
    -DENABLE_PORCUPINE=OFF \
    -DBUILD_SHARED_LIBS=OFF

# 编译
echo "开始编译..."
make -j$(nproc)

# 安装
echo "安装到 ${INSTALL_DIR}..."
make install

# 拷贝模型文件
echo "拷贝模型文件..."
MODEL_SRC="${MELOTTS_DIR}/model"
MODEL_DEST="${INSTALL_DIR}/model"
mkdir -p ${MODEL_DEST}

if [ -d "${MODEL_SRC}" ]; then
    cp -r ${MODEL_SRC}/*.rknn ${MODEL_DEST}/
    cp -r ${MODEL_SRC}/lexicon*.txt ${MODEL_DEST}/
    cp -r ${MODEL_SRC}/tokens*.txt ${MODEL_DEST}/
    echo "已拷贝模型文件到 ${MODEL_DEST}"
else
    echo "警告：未找到模型文件目录 ${MODEL_SRC}"
fi



# 创建lib目录
LIB_DIR="${INSTALL_DIR}/lib"
mkdir -p ${LIB_DIR}

# 跳过拷贝 Porcupine 库文件（melotts 项目不需要）
echo "跳过拷贝 Porcupine 库文件（melotts 项目不需要）"

# 创建运行脚本，方便在设备上运行
echo "创建运行脚本..."
cat > ${INSTALL_DIR}/run_melotts_demo.sh << 'EOF'
#!/bin/bash

# 设置库文件路径
export LD_LIBRARY_PATH=".:./lib:$LD_LIBRARY_PATH"

# 运行 melotts 演示
./melotts_demo
EOF

chmod +x ${INSTALL_DIR}/run_melotts_demo.sh
echo "已创建运行脚本 ${INSTALL_DIR}/run_melotts_demo.sh"

# 回到项目根目录
cd ${PROJECT_ROOT}

echo "=========================================="
echo "交叉编译完成！"
echo "=========================================="
echo "构建目录：${BUILD_DIR}"
echo "安装目录：${INSTALL_DIR}"
echo "可执行文件：${INSTALL_DIR}/melotts_demo"
echo "模型文件：${MODEL_DEST}"
echo "运行脚本：${INSTALL_DIR}/run_melotts_demo.sh"
echo "=========================================="
