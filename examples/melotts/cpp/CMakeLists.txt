cmake_minimum_required(VERSION 3.10)

project(melotts_demo)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 只对 C++ 文件应用 C++17 选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

# 为 filesystem 头文件添加兼容处理
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "8.0")
        message(WARNING "GCC version < 8.0 may not support C++17 filesystem fully")
        # 对于旧版本 GCC，尝试使用实验性 filesystem
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lstdc++fs")
    endif()
endif()

# 定义项目根目录
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

# 添加 3rdparty 目录
add_subdirectory(${PROJECT_ROOT}/3rdparty/ 3rdparty.out)

# 添加 utils 目录
add_subdirectory(${PROJECT_ROOT}/utils/ utils.out)

# 直接设置必要的库和包含路径
set(LIBRKNNRT ${PROJECT_ROOT}/3rdparty/rknpu2/Linux/aarch64/librknnrt.so)
set(LIBRKNNRT_INCLUDES ${PROJECT_ROOT}/3rdparty/rknpu2/include)
set(LIBTIMER_INCLUDES ${PROJECT_ROOT}/3rdparty/timer)

# 打印路径信息以调试
message(STATUS "PROJECT_ROOT: ${PROJECT_ROOT}")
message(STATUS "LIBRKNNRT: ${LIBRKNNRT}")
message(STATUS "LIBRKNNRT_INCLUDES: ${LIBRKNNRT_INCLUDES}")

# 检查文件是否存在
if(NOT EXISTS ${LIBRKNNRT})
    message(FATAL_ERROR "librknnrt.so not found at ${LIBRKNNRT}")
endif()

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

add_executable(${PROJECT_NAME}
    main.cc
    melotts.cc
    process.cc
)

target_link_libraries(${PROJECT_NAME}
    fileutils
    audioutils
    ${LIBRKNNRT}
)

# 链接 filesystem 库（对于某些编译器需要）
if(NOT MSVC)
    target_link_libraries(${PROJECT_NAME} stdc++fs)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(${PROJECT_NAME}
    log
)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${LIBTIMER_INCLUDES}
)

install(TARGETS ${PROJECT_NAME} DESTINATION .)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
file(GLOB LEXICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/lexicon*.txt")
file(GLOB TOKENS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/tokens*.txt")
install(FILES ${RKNN_FILES} ${ONNX_FILES} ${LEXICON_FILES} ${TOKENS_FILES} DESTINATION model)