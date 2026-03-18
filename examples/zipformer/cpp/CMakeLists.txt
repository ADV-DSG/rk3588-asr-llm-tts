cmake_minimum_required(VERSION 3.10)

project(rknn_zipformer_demo)

if (ENABLE_ASAN)
	message(STATUS "BUILD WITH ADDRESS SANITIZER")
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
	set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif ()

set(rknpu_zipformer_file rknpu2/zipformer.cc)
if(TARGET_SOC STREQUAL "rk1808" OR TARGET_SOC STREQUAL "rv1109" OR TARGET_SOC STREQUAL "rv1126")
    set(rknpu_zipformer_file rknpu1/zipformer.cc)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/ utils.out)

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# Build original demo with file input
# add_executable(${PROJECT_NAME}
#     main.cc
#     process.cc
#     ${rknpu_zipformer_file}
# )

# Build microphone demo
add_executable(${PROJECT_NAME}_mic
    main_mic.cc
    process.cc
    ${rknpu_zipformer_file}
)

# target_link_libraries(${PROJECT_NAME}
#     fileutils
#     audioutils
#     ${LIBRKNNRT}
#     ${LIBKALDI_NATIVE_FBANK}
# )

target_link_libraries(${PROJECT_NAME}_mic
    fileutils
    audioutils
    ${LIBRKNNRT}
    ${LIBKALDI_NATIVE_FBANK}
)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_link_libraries(${PROJECT_NAME}
    log
)
    target_link_libraries(${PROJECT_NAME}_mic
    log
)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    # target_link_libraries(${PROJECT_NAME} Threads::Threads)
    target_link_libraries(${PROJECT_NAME}_mic Threads::Threads)
endif()

# target_include_directories(${PROJECT_NAME} PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${LIBRKNNRT_INCLUDES}
#     ${LIBKALDI_NATIVE_FBANK_INCLUDES}
#     ${LIBTIMER_INCLUDES}
# )

target_include_directories(${PROJECT_NAME}_mic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${LIBKALDI_NATIVE_FBANK_INCLUDES}
    ${LIBTIMER_INCLUDES}
)

# install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_mic DESTINATION .)
install(TARGETS ${PROJECT_NAME}_mic DESTINATION .)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/test.wav DESTINATION ./model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/test_zh.wav DESTINATION ./model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/test_en.wav DESTINATION ./model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/vocab.txt DESTINATION ./model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)