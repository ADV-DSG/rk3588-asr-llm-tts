cmake_minimum_required(VERSION 3.8)
project(atk_deepseek_rkllm_demo)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(TOOLCHAIN_DIR /opt/atk-dlrk3588-toolchain)
# set(TOOLCHAIN_DIR /home/allen/ssd2/program/atk-dlrk3588-toolchain)
set(TOOLCHAIN_DIR /home/allen/ssd1/rk3588_linux_sdk/buildroot/output/rockchip_atk_dlrk3588/host)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/bin/aarch64-buildroot-linux-gnu-g++)
set(CMAKE_C_COMPILER ${TOOLCHAIN_DIR}/bin/aarch64-buildroot-linux-gnu-gcc)

include_directories(${CMAKE_SOURCE_DIR}/lib/librkllm_api/include/)
set(RKLLM_RT_LIB ${CMAKE_SOURCE_DIR}/lib/librkllm_api/librkllmrt.so)
set(GOMP_LIB ${CMAKE_SOURCE_DIR}/lib/libgomp/libgomp.so)
set(GOMP_1_LIB ${CMAKE_SOURCE_DIR}/lib/libgomp/libgomp.so.1)

add_executable(deepseek_demo main.cc)
target_link_libraries(deepseek_demo ${RKLLM_RT_LIB} ${GOMP_LIB})

set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/deepseek_rkllm_demo)
install(TARGETS deepseek_demo DESTINATION ./)
install(DIRECTORY rkllm_model DESTINATION ./)
install(FILES ${RKLLM_RT_LIB} ${GOMP_LIB} ${GOMP_1_LIB} DESTINATION ./lib)
