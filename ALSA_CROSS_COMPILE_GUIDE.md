# ALSA库交叉编译指南

本指南介绍如何为aarch64-linux-gnu目标架构交叉编译ALSA库，以便在RKNN语音识别项目中使用。

## 1. 编译ALSA库

### 1.1 准备工作

确保你的系统已经安装了交叉编译工具链：

```bash
sudo apt-get update
sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

### 1.2 运行编译脚本

```bash
chmod +x build_alsa.sh
./build_alsa.sh
```

脚本会自动完成以下工作：
- 下载ALSA库源码（版本1.2.10）
- 配置交叉编译环境
- 编译ALSA库
- 安装到指定目录
- 创建适合项目使用的目录结构

### 1.3 编译结果

编译完成后，ALSA库会被安装到 `3rdparty/alsa` 目录，结构如下：

```
3rdparty/alsa/
├── include/
│   └── alsa/          # ALSA头文件
└── lib/
    └── aarch64-linux-gnu/  # ALSA库文件
        ├── libasound.a     # 静态库
        ├── libasound.so    # 动态库
        └── libasound.so.2  # 动态库
```

## 2. 集成到项目中

### 2.1 修改CMake配置

修改 `utils/CMakeLists.txt` 文件，添加以下内容：

```cmake
# 使用本地编译的ALSA库
set(ALSA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/alsa/include")
set(ALSA_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/alsa/lib/aarch64-linux-gnu/libasound.so")

# Enable ALSA support
target_compile_definitions(audioutils PRIVATE HAVE_ALSA)
target_link_libraries(audioutils ${ALSA_LIBRARY})
target_include_directories(audioutils PUBLIC ${ALSA_INCLUDE_DIR})
```

### 2.2 修改编译脚本

修改 `build_cross.sh` 文件，添加ALSA库的路径：

```bash
# 配置ALSA库路径
ALSA_INCLUDE_DIR="${PROJECT_ROOT}/3rdparty/alsa/include"
ALSA_LIBRARY="${PROJECT_ROOT}/3rdparty/alsa/lib/aarch64-linux-gnu/libasound.so"

# 在CMake命令中添加
-DALSA_INCLUDE_DIR=${ALSA_INCLUDE_DIR} \
-DALSA_LIBRARY=${ALSA_LIBRARY} \
```

## 3. 验证编译

运行项目的编译脚本，确保能正确编译：

```bash
./build_cross.sh
```

如果编译成功，会生成两个可执行文件：
- `rknn_zipformer_demo` - WAV文件识别程序
- `rknn_zipformer_demo_mic` - 麦克风实时识别程序

## 4. 运行程序

将编译好的可执行文件和模型文件复制到目标设备上，然后运行：

```bash
# 运行麦克风实时识别程序
./rknn_zipformer_demo_mic <encoder_path> <decoder_path> <joiner_path>
```

## 5. 常见问题

### 5.1 编译失败

如果编译失败，检查以下几点：
- 交叉编译工具链是否正确安装
- 网络连接是否正常（用于下载ALSA源码）
- 磁盘空间是否足够

### 5.2 运行时找不到ALSA库

在目标设备上运行时，如果出现 "error while loading shared libraries: libasound.so.2: cannot open shared object file"，可以：

1. 将编译好的ALSA库复制到目标设备的 `/usr/lib/aarch64-linux-gnu/` 目录
2. 或者设置 `LD_LIBRARY_PATH` 环境变量：
   ```bash
   export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./lib
   ```

## 6. 自定义编译

### 6.1 修改ALSA版本

编辑 `build_alsa.sh` 文件，修改 `ALSA_VERSION` 和 `ALSA_URL` 变量：

```bash
ALSA_VERSION="1.2.11"  # 修改为你需要的版本
ALSA_URL="https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2"
```

### 6.2 修改编译选项

在 `build_alsa.sh` 文件中，你可以修改 `./configure` 命令的选项，添加或移除编译特性。

## 7. 参考链接

- [ALSA Project](https://www.alsa-project.org/)
- [ALSA Library Documentation](https://www.alsa-project.org/alsa-doc/alsa-lib/)
- [交叉编译指南](https://developer.arm.com/documentation/101754/latest/)