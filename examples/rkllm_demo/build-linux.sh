set -e

ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )
BUILD_DIR=${ROOT_PWD}/build/build_linux_aarch64
INSTALL_DIR=${ROOT_PWD}/install/deepseek_rkllm_demo

if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}
cmake ../..
make -j4
make install
cd -

# 生成运行脚本
echo "生成运行脚本..."
cat > ${INSTALL_DIR}/run_demo.sh << 'EOF'
#!/bin/bash

# 设置库文件路径
export LD_LIBRARY_PATH=".:./lib:$LD_LIBRARY_PATH"

# 运行RKLLM演示程序
# 参数1: 模型路径
# 参数2: 最大新token数
# 参数3: 最大上下文长度
./deepseek_demo \
    rkllm_model/deepseek-1.5b-w8a8-rk3588.rkllm \
    5000 \
    5000
EOF

chmod +x ${INSTALL_DIR}/run_demo.sh
echo "运行脚本已生成: ${INSTALL_DIR}/run_demo.sh"
echo "可以通过以下命令运行演示程序:"
echo "cd ${INSTALL_DIR} && ./run_demo.sh"


