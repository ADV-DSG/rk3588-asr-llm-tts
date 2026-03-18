// Copyright (c) 2024 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


/*
./rknn_zipformer_demo_mic \
    model/encoder-epoch-99-avg-1.rknn \
    model/decoder-epoch-99-avg-1.rknn \
    model/joiner-epoch-99-avg-1.rknn \
    porcupine/model/porcupine_params.pv \
    porcupine/keyword/porcupine_raspberry-pi.ppn \
    0.8

./rknn_zipformer_demo_mic \
    model/encoder-epoch-99-avg-1.rknn \
    model/decoder-epoch-99-avg-1.rknn \
    model/joiner-epoch-99-avg-1.rknn \
    porcupine/model/porcupine_params_zh.pv \
    porcupine/keyword/电脑_zh_raspberry-pi_v4_0_0.ppn \
    0.8


*/


/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "zipformer.h"
#include "audio_utils.h"
#include "wake_word_detector.h"
#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include "process.h"
#include <iomanip>
#include "kaldi-native-fbank/csrc/online-feature.h"
#include <thread>
#include <mutex>
#include <condition_variable>

/*-------------------------------------------
                  Global Variables
-------------------------------------------*/
bool g_running = true;

// 管道通信相关常量
#define PIPE_PATH_ASR_TO_LLM "/tmp/asr_to_llm_pipe"
#define PIPE_PATH_TTS_TO_ASR "/tmp/tts_to_asr_pipe"

// 终端输入相关变量
std::queue<std::string> terminal_input_queue;
std::mutex terminal_input_mutex;
std::condition_variable terminal_input_cv;

// 发送数据到管道
int send_to_pipe(const char* pipe_path, const std::string& data) {
    int fd = open(pipe_path, O_WRONLY);
    if (fd == -1) {
        std::cerr << "Failed to open pipe for writing" << std::endl;
        return -1;
    }

    ssize_t bytes_written = write(fd, data.c_str(), data.size());
    if (bytes_written == -1) {
        std::cerr << "Failed to write to pipe" << std::endl;
        close(fd);
        return -1;
    }

    close(fd);
    return bytes_written;
}

// 从管道接收数据（非阻塞方式）
int receive_from_pipe_nonblock(const char* pipe_path, std::string& data, size_t max_size = 1024) {
    int fd = open(pipe_path, O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        // 管道可能未打开，返回0表示无数据
        return 0;
    }

    char buffer[max_size];
    ssize_t bytes_read = read(fd, buffer, max_size - 1);
    if (bytes_read == -1) {
        // 无数据可读
        close(fd);
        return 0;
    }

    buffer[bytes_read] = '\0';
    data = buffer;
    close(fd);
    
    // 只有在实际读取到数据时才打印
    if (bytes_read > 0) {
        std::cout << "Successfully received data: " << data << std::endl;
    }
    
    return bytes_read;
}

// 从管道接收数据（阻塞方式）
int receive_from_pipe(const char* pipe_path, std::string& data, size_t max_size = 1024) {
    int fd = open(pipe_path, O_RDONLY);
    if (fd == -1) {
        std::cerr << "Failed to open pipe for reading" << std::endl;
        return -1;
    }

    char buffer[max_size];
    ssize_t bytes_read = read(fd, buffer, max_size - 1);
    if (bytes_read == -1) {
        std::cerr << "Failed to read from pipe" << std::endl;
        close(fd);
        return -1;
    }

    buffer[bytes_read] = '\0';
    data = buffer;
    close(fd);
    return bytes_read;
}

// 创建管道
int create_pipe(const char* pipe_path) {
    if (access(pipe_path, F_OK) == 0) {
        // 管道已存在，删除旧管道
        if (unlink(pipe_path) != 0) {
            std::cerr << "Failed to remove existing pipe" << std::endl;
            return -1;
        }
    }

    // 创建新管道
    if (mkfifo(pipe_path, 0666) != 0) {
        std::cerr << "Failed to create pipe" << std::endl;
        return -1;
    }

    return 0;
}

// 终端输入处理线程函数
void terminal_input_thread() {
    while (g_running) {
        std::string input_text;
        std::cout << "\nEnter text to send to LLM (press Enter to send): " << std::endl;
        std::getline(std::cin, input_text);
        
        if (!input_text.empty() && g_running) {
            std::cout << "Received terminal input: " << input_text << std::endl;
            
            // 将输入文本添加到队列
            {
                std::lock_guard<std::mutex> lock(terminal_input_mutex);
                terminal_input_queue.push(input_text);
            }
            // 通知主线程有新的终端输入
            terminal_input_cv.notify_one();
        }
    }
}

/*-------------------------------------------
                  Signal Handler
-------------------------------------------*/
void signal_handler(int sig)
{
    printf("\nReceived signal %d, exiting...\n", sig);
    g_running = false;
    exit(sig);
}

/*-------------------------------------------
                  Main Function
-------------------------------------------*/

int main(int argc, char **argv)
{
    if (argc != 7)
    {
        printf("%s <encoder_path> <decoder_path> <joiner_path> <porcupine_model_path> <keyword_path> <sensitivity>\n", argv[0]);
        return -1;
    }

    const char *encoder_path = argv[1];
    const char *decoder_path = argv[2];
    const char *joiner_path = argv[3];
    const char *porcupine_model_path = argv[4];
    const char *keyword_path = argv[5];
    float sensitivity = atof(argv[6]);

    int ret;
    TIMER timer;
    float infer_time = 0.0;
    float audio_length = 0.0;
    float rtf = 0.0;
    int frame_shift_ms = 10;
    int subsampling_factor = 4;
    float frame_shift_s = frame_shift_ms / 1000.0 * subsampling_factor;
    std::vector<std::string> recognized_text;
    std::vector<float> timestamp;
    rknn_zipformer_context_t rknn_app_ctx;
    VocabEntry vocab[VOCAB_NUM];
    memset(&rknn_app_ctx, 0, sizeof(rknn_zipformer_context_t));
    memset(vocab, 0, sizeof(vocab));
    
    // Initialize ALL resources and variables early to avoid goto issues
    void* mic_ctx = NULL;
    void* wake_detector_ctx = NULL;
    bool models_initialized = false;
    knf::OnlineFbank* fbank = NULL;
    knf::FbankOptions fbank_opts;
    int frame_offset = 0;
    int last_text_size = 0;
    audio_buffer_t audio_buf;
    memset(&audio_buf, 0, sizeof(audio_buffer_t));
    bool wake_word_detected = true;//false; 测试阶段，不进唤醒词状态
    float recognition_timeout = 0;
    const int MAX_RECOGNITION_SECONDS = 60000;
    bool tts_processing = false; // TTS是否正在处理
    bool tts_done = true; // TTS是否已完成处理
    float text_accumulate_timeout = 0; // 文本累积超时时间
    const float MAX_TEXT_ACCUMULATE_SECONDS = 1.0; // 最大文本累积时间（秒）
    std::thread* input_thread = NULL;

    // Set up signal handler
    signal(SIGINT, signal_handler);

    // Initialize vocabulary
    ret = read_vocab(VOCAB_PATH, vocab);
    if (ret != 0)
    {
        printf("read vocab fail! ret=%d vocab_path=%s\n", ret, VOCAB_PATH);
        goto cleanup;
    }

    // 创建管道
    ret = create_pipe(PIPE_PATH_ASR_TO_LLM);
    if (ret != 0) {
        printf("create asr to llm pipe fail! ret=%d\n", ret);
        goto cleanup;
    }

    ret = create_pipe(PIPE_PATH_TTS_TO_ASR);
    if (ret != 0) {
        printf("create tts to asr pipe fail! ret=%d\n", ret);
        goto cleanup;
    }

    printf("Created pipes: %s, %s\n", PIPE_PATH_ASR_TO_LLM, PIPE_PATH_TTS_TO_ASR);

    // Initialize models
    timer.tik();
    ret = init_zipformer_model(encoder_path, &rknn_app_ctx.encoder_context);
    if (ret != 0)
    {
        printf("init_zipformer_model fail! ret=%d encoder_path=%s\n", ret, encoder_path);
        goto cleanup;
    }
    build_input_output(&rknn_app_ctx.encoder_context);
    timer.tok();
    timer.print_time("init_zipformer_encoder_model");

    timer.tik();
    ret = init_zipformer_model(decoder_path, &rknn_app_ctx.decoder_context);
    if (ret != 0)
    {
        printf("init_zipformer_model fail! ret=%d decoder_path=%s\n", ret, decoder_path);
        goto cleanup;
    }
    build_input_output(&rknn_app_ctx.decoder_context);
    timer.tok();
    timer.print_time("init_zipformer_decoder_model");

    timer.tik();
    ret = init_zipformer_model(joiner_path, &rknn_app_ctx.joiner_context);
    if (ret != 0)
    {
        printf("init_zipformer_model fail! ret=%d joiner_path=%s\n", ret, joiner_path);
        goto cleanup;
    }
    build_input_output(&rknn_app_ctx.joiner_context);
    timer.tok();
    timer.print_time("init_zipformer_joiner_model");
    models_initialized = true;

    // Initialize wake word detector
    // wake_detector_ctx = init_wake_word_detector(porcupine_model_path, keyword_path, sensitivity);  //先不初始化，不用它
    if (!wake_detector_ctx)
    {
        printf("init wake word detector fail!\n");
        // goto cleanup;   //由于需要联网验证key，所以这里失败，也不管，先不用wake word
    }

    // Initialize microphone
    mic_ctx = init_microphone(SAMPLE_RATE, 1);
    if (!mic_ctx)
    {
        printf("init microphone fail!\n");
        goto cleanup;
    }
    printf("Microphone initialized successfully.\n");

    // Initialize feature extractor
    fbank_opts.frame_opts.samp_freq = SAMPLE_RATE;
    fbank_opts.mel_opts.num_bins = N_MELS;
    fbank_opts.mel_opts.high_freq = -400;
    fbank_opts.frame_opts.dither = 0;
    fbank_opts.frame_opts.snip_edges = false;
    fbank = new knf::OnlineFbank(fbank_opts);
    if (!fbank)
    {
        printf("init feature extractor fail!\n");
        goto cleanup;
    }

    // 启动终端输入线程
    input_thread = new std::thread(terminal_input_thread);
    input_thread->detach(); // 分离线程，让其在后台运行
    printf("Terminal input thread started. You can enter text to send to LLM at any time.\n");

    printf("\nWaiting for wake word...\n");
    printf("=========================================\n");

    // Main processing loop
    while (g_running)
    {
        // 检查是否有终端输入
        {   
            std::lock_guard<std::mutex> lock(terminal_input_mutex);
            if (!terminal_input_queue.empty() && tts_done) {
                std::string input_text = terminal_input_queue.front();
                terminal_input_queue.pop();
                
                std::cout << "\n\nProcessing terminal input: " << input_text << std::endl;
                
                // 发送终端输入到LLM工程
                printf("Sending terminal text to LLM...\n");
                send_to_pipe(PIPE_PATH_ASR_TO_LLM, input_text);
                
                // 暂停麦克风，等待TTS处理
                if (mic_ctx) {
                    pause_microphone(mic_ctx);
                    printf("Microphone paused while waiting for TTS...\n");
                }
                
                // 标记TTS正在处理
                tts_processing = true;
                tts_done = false;
                
                // 清空已识别的文本，准备下一轮识别
                recognized_text.clear();
                last_text_size = 0;
                frame_offset = 0;
                text_accumulate_timeout = 0;
                
                // 重置特征提取器，避免处理之前的音频数据
                delete fbank;
                fbank = new knf::OnlineFbank(fbank_opts);
                
                printf("\nWaiting for TTS to finish...\n");
                printf("=========================================\n");
                
                // 处理完终端输入后，跳过本次循环的其他部分
                continue;
            }
        }

        // Check if TTS is processing, if yes, skip processing
        if (tts_processing) {
            // Check if TTS has finished
            std::string response;
            // 使用阻塞读取，确保能够正确接收TTS的完成信号
            int ret = receive_from_pipe(PIPE_PATH_TTS_TO_ASR, response);
            if (ret > 0 && response == "DONE") {
                printf("TTS finished processing\n");
                
                // 恢复麦克风
                if (mic_ctx) {
                    resume_microphone(mic_ctx);
                    printf("Microphone resumed after TTS finished\n");
                }
                
                tts_processing = false;
                tts_done = true;
            } else {
                printf("Failed to receive TTS done signal, will retry\n");
            }
            continue;
        }

        // Check if TTS has finished (non-blocking)
        std::string response;
        int ret_tts = receive_from_pipe_nonblock(PIPE_PATH_TTS_TO_ASR, response);
        if (ret_tts > 0 && response == "DONE") {
            printf("TTS finished processing\n");
            
            // 恢复麦克风
            if (mic_ctx) {
                resume_microphone(mic_ctx);
                printf("Microphone resumed after TTS finished\n");
            }
            
            tts_processing = false;
            tts_done = true;
        }

        // Read audio from microphone only if it's initialized
        if (mic_ctx) {
            memset(&audio_buf, 0, sizeof(audio_buffer_t));
            ret = read_microphone(mic_ctx, &audio_buf);
            if (ret < 0)
            {
                printf("read microphone fail! ret=%d\n", ret);
                continue;
            }
            
            if (audio_buf.data && audio_buf.num_frames > 0)
            {
                if (!wake_word_detected)
                {
                    // Wait for wake word
                    ret = detect_wake_word(wake_detector_ctx, &audio_buf);
                    if (ret >= 0)
                    {
                        // Wake word detected!
                        wake_word_detected = true;
                        recognition_timeout = 0;
                        recognized_text.clear();
                        last_text_size = 0;
                        frame_offset = 0;
                        
                        // Reset feature extractor
                        delete fbank;
                        fbank = new knf::OnlineFbank(fbank_opts);
                        
                        printf("\nWake word detected! Starting speech recognition...\n");
                        printf("=========================================\n");
                    }
                }
                else
                {
                    // Process audio data for speech recognition
                    fbank->AcceptWaveform(SAMPLE_RATE, audio_buf.data, audio_buf.num_frames);
                    
                    // Run streaming inference
                    timer.tik();
                    ret = inference_zipformer_model_streaming(&rknn_app_ctx, fbank, vocab, recognized_text, timestamp, frame_offset);
                    timer.tok();
                    
                    // Check if there is new recognized text
                    bool has_new_text = recognized_text.size() > last_text_size;
                    
                    // Print new recognized text
                    if (has_new_text) {
                        for (size_t i = last_text_size; i < recognized_text.size(); ++i) {
                            std::cout << recognized_text[i];
                            std::cout.flush();
                        }
                        last_text_size = recognized_text.size();
                        recognition_timeout = 0;  // 有识别结果时重置超时计数器
                        text_accumulate_timeout = 0;  // 有新文本时重置累积超时
                    }
                    else {
                        // 计算帧持续时间
                        float frame_duration = audio_buf.num_frames / (float)SAMPLE_RATE;
                        recognition_timeout += frame_duration;
                        text_accumulate_timeout += frame_duration;
                        
                        // 如果文本累积时间超过阈值，且TTS已完成处理，发送完整文本
                        if (text_accumulate_timeout >= MAX_TEXT_ACCUMULATE_SECONDS && tts_done && !recognized_text.empty()) {
                            std::string text;
                            for (const auto &str : recognized_text) {
                                text += str;
                            }
                            std::cout << "\n\nSending text to LLM: " << text << std::endl;
                            
                            // 发送识别结果到LLM工程
                            printf("Sending text to LLM...\n");
                            send_to_pipe(PIPE_PATH_ASR_TO_LLM, text);
                            
                            // 暂停麦克风，等待TTS处理
                            if (mic_ctx) {
                                pause_microphone(mic_ctx);
                                printf("Microphone paused while waiting for TTS...\n");
                            }
                            
                            // 标记TTS正在处理
                            tts_processing = true;
                            tts_done = false;
                            
                            // 清空已识别的文本，准备下一轮识别
                            recognized_text.clear();
                            last_text_size = 0;
                            frame_offset = 0;
                            text_accumulate_timeout = 0;
                            
                            // 重置特征提取器，避免处理之前的音频数据
                            delete fbank;
                            fbank = new knf::OnlineFbank(fbank_opts);
                            
                            printf("\nWaiting for TTS to finish...\n");
                            printf("=========================================\n");
                        }
                        
                        // 检查是否超时，进入唤醒词检测模式
                        if (recognition_timeout >= MAX_RECOGNITION_SECONDS) {
                            // 语音识别超时，重置到唤醒词检测模式
                            // wake_word_detected = false; 测试阶段，不进唤醒词姿态
                            recognition_timeout = 0;  // 重置超时计数器
                            text_accumulate_timeout = 0;  // 重置累积超时
                            printf("\nRecognition timeout. Waiting for wake word...\n");
                            printf("=========================================\n");
                        }
                    }
                }
                
                // Free audio buffer
                if (audio_buf.data)
                {
                    free(audio_buf.data);
                }
            }
        }
    }

    printf("\n\n=========================================\n");
    printf("Real-time speech recognition stopped.\n");

    // Print final result
    std::cout << "\n\nFinal result: ";
    for (const auto &str : recognized_text)
    {
        std::cout << str;
    }
    std::cout << std::endl;

cleanup:
    // Clean up resources
    if (fbank)
    {
        delete fbank;
        fbank = NULL;
    }
    
    if (mic_ctx)
    {
        close_microphone(mic_ctx);
    }

    if (wake_detector_ctx)
    {
        close_wake_word_detector(wake_detector_ctx);
    }

    if (models_initialized)
    {
        ret = release_zipformer_model(&rknn_app_ctx.encoder_context);
        if (ret != 0)
        {
            printf("release_zipformer_model encoder_context fail! ret=%d\n", ret);
        }

        ret = release_zipformer_model(&rknn_app_ctx.decoder_context);
        if (ret != 0)
        {
            printf("release_zipformer_model decoder_context fail! ret=%d\n", ret);
        }

        ret = release_zipformer_model(&rknn_app_ctx.joiner_context);
        if (ret != 0)
        {
            printf("release_zipformer_model joiner_context fail! ret=%d\n", ret);
        }
    }

    for (int i = 0; i < VOCAB_NUM; i++)
    {
        if (vocab[i].token)
        {
            free(vocab[i].token);
            vocab[i].token = NULL;
        }
    }

    // 清理线程
    if (input_thread) {
        // 线程已经分离，不需要join
        delete input_thread;
        input_thread = NULL;
    }

    // 清理管道
    if (access(PIPE_PATH_ASR_TO_LLM, F_OK) == 0) {
        unlink(PIPE_PATH_ASR_TO_LLM);
    }

    if (access(PIPE_PATH_TTS_TO_ASR, F_OK) == 0) {
        unlink(PIPE_PATH_TTS_TO_ASR);
    }
    printf("Cleaned up pipes\n");

    return 0;
}