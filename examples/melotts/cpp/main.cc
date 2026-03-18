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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "melotts.h"
#include "parse_args.h"
#include "audio_utils.h"

#include "lexicon.hpp"
#include "split.hpp"

static std::vector<int64_t> intersperse(const std::vector<int>& lst, int item) {
    std::vector<int64_t> result(lst.size() * 2 + 1, item);
    for (size_t i = 1; i < result.size(); i+=2) {
        result[i] = lst[i / 2];
    }
    return result;
}

static std::vector<int64_t> pad_or_trim(std::vector<int64_t>& vec, int max_size) {
    if (vec.size() < max_size) {
        vec.resize(max_size, 0);
    } else if (vec.size() > max_size) {
        vec.resize(max_size);
    }
    return vec;
}

const std::map<std::string, int> language_id_map = { 
    {"ZH", 0},
    {"JP", 1},
    {"EN", 2},
    {"ZH_MIX_EN", 3},
    {"KR", 4},
    {"SP", 5},
    {"ES", 5},
    {"FR", 6}
};

// 管道通信相关常量
#define PIPE_PATH_LLM_TO_TTS "/tmp/llm_to_tts_pipe"
#define PIPE_PATH_TTS_TO_ASR "/tmp/tts_to_asr_pipe"
#define PIPE_PATH_LLM_TO_TTS_FINISH "/tmp/llm_to_tts_finish_pipe"

// 从管道接收数据
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
    
    return bytes_read;
}

// 发送数据到管道（带重试机制）
int send_to_pipe(const char* pipe_path, const std::string& data) {
    int retries = 5;
    int delay_ms = 100;
    
    for (int i = 0; i < retries; i++) {
        int fd = open(pipe_path, O_WRONLY);
        if (fd == -1) {
            std::cerr << "Failed to open pipe for writing, retry " << i+1 << "/" << retries << std::endl;
            usleep(delay_ms * 1000); // 转换为微秒
            continue;
        }

        ssize_t bytes_written = write(fd, data.c_str(), data.size());
        if (bytes_written == -1) {
            std::cerr << "Failed to write to pipe, retry " << i+1 << "/" << retries << std::endl;
            close(fd);
            usleep(delay_ms * 1000);
            continue;
        }

        close(fd);
        std::cout << "Successfully sent data: " << data << std::endl;
        return bytes_written;
    }

    std::cerr << "Failed to write to pipe after " << retries << " retries" << std::endl;
    return -1;
}

// 检查管道是否存在
bool pipe_exists(const char* pipe_path) {
    struct stat st;
    return stat(pipe_path, &st) == 0;
}

int main(int argc, char **argv)
{
    int ret;
    TIMER timer;

    rknn_melotts_context_t rknn_app_ctx;
    memset(&rknn_app_ctx, 0, sizeof(rknn_melotts_context_t));

    float infer_time = 0.0;
    float audio_length = 0.0;
    float rtf = 0.0;

    std::vector<float> output_wav_data;
    std::string input_text;
    std::vector<std::string> sentences;
    bool running = true;
    
    // 文本缓存队列，用于存储从LLM接收的文本
    std::queue<std::string> text_cache;
    // LLM是否已完成的标志
    bool llm_finished = false;

    Args args = parse_args(argc, argv);
    const char *encoder_path = args.encoder_path.c_str();
    const char *decoder_path = args.decoder_path.c_str();
    // const char *bert_path = args.bert_model_path.c_str();
    const char *audio_save_path  = args.output_filename.c_str();

    // 默认ZH是ZH_MIX_EN
    if(!args.language.compare("ZH"))
    {
        args.language = "ZH_MIX_EN";
    }

    // Load lexicon
    timer.tik();
    // Lexicon lexicon(LEXICON_EN_FILE, TOKENS_EN_FILE);
    Lexicon lexicon(LEXICON_ZH_FILE, TOKENS_ZH_FILE);
    timer.tok();
    timer.print_time("Lexicon init");

    // lang_ids
    int value = 3;
    auto it = language_id_map.find(args.language);
    if (it != language_id_map.end()) {
        value = it->second;
    } else {
        std::cerr << "Language not found!" << std::endl;
        return 1;
    }

    // 声明管道文件描述符，移到可能的goto语句之前
    int text_fd = -1;
    int finish_fd = -1;

    // 声明线程对象，移到可能的goto语句之前
    std::thread receive_thread;
    std::thread inference_thread;
    std::thread audio_playback_thread;

    // 语音缓存队列，用于存储生成的语音数据
    std::queue<std::vector<float>> audio_cache;

    // 线程同步变量
    std::mutex text_cache_mutex;
    std::condition_variable text_cache_cv;
    std::mutex audio_cache_mutex;
    std::condition_variable audio_cache_cv;

    // 定义接收线程函数，负责从LLM接收文本并放入文本缓存
    auto receive_thread_func = [&]() {
        while (running) {
            // 使用select来同时监听两个管道，添加超时机制
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(text_fd, &read_fds);
            FD_SET(finish_fd, &read_fds);
            
            int max_fd = std::max(text_fd, finish_fd);
            
            // 添加500ms超时，避免在某些情况下无限期阻塞
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 500000; // 500ms
            
            int select_ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
            
            if (select_ret < 0) {
                std::cerr << "Select failed, retrying..." << std::endl;
                usleep(100000); // 100ms
                continue;
            } else if (select_ret == 0) {
                // 超时，继续循环
                usleep(10000); // 10ms
                continue;
            }

            // 检查是否收到LLM的FINISH消息
            if (FD_ISSET(finish_fd, &read_fds)) {
                std::string finish_data;
                char buffer[1024];
                ssize_t bytes_read = read(finish_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    finish_data = buffer;
                    
                    // 检查是否收到退出信号
                    if (finish_data == "EXIT") {
                        std::cout << "Received exit signal, shutting down..." << std::endl;
                        {   
                            std::lock_guard<std::mutex> lock(text_cache_mutex);
                            running = false;
                        }
                        text_cache_cv.notify_all();
                        audio_cache_cv.notify_all();
                        break;
                    }
                    
                    // 检查是否收到LLM的FINISH消息
                    if (finish_data == "FINISH") {
                        std::cout << "Received LLM finish signal" << std::endl;
                        {   
                            std::lock_guard<std::mutex> lock(text_cache_mutex);
                            llm_finished = true;
                        }
                        text_cache_cv.notify_all();
                    }
                } else if (bytes_read == 0) {
                    // 管道已关闭，重新打开
                    close(finish_fd);
                    finish_fd = open(PIPE_PATH_LLM_TO_TTS_FINISH, O_RDONLY | O_NONBLOCK);
                    if (finish_fd == -1) {
                        std::cerr << "Failed to reopen finish pipe" << std::endl;
                        usleep(100000); // 100ms
                        continue;
                    }
                }
            }

            // 检查是否收到文本数据
            if (FD_ISSET(text_fd, &read_fds)) {
                char buffer[1024];
                ssize_t bytes_read = read(text_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    std::string received_text = buffer;
                    
                    // 检查是否收到退出信号
                    if (received_text == "EXIT") {
                        std::cout << "Received exit signal, shutting down..." << std::endl;
                        {   
                            std::lock_guard<std::mutex> lock(text_cache_mutex);
                            running = false;
                        }
                        text_cache_cv.notify_all();
                        audio_cache_cv.notify_all();
                        break;
                    }
                    
                    std::cout << "Received text: " << received_text << std::endl;
                    
                    // 将文本添加到缓存队列
                    {   
                        std::lock_guard<std::mutex> lock(text_cache_mutex);
                        text_cache.push(received_text);
                    }
                    text_cache_cv.notify_all();
                } else if (bytes_read == 0) {
                    // 管道已关闭，重新打开
                    close(text_fd);
                    text_fd = open(PIPE_PATH_LLM_TO_TTS, O_RDONLY | O_NONBLOCK);
                    if (text_fd == -1) {
                        std::cerr << "Failed to reopen text pipe" << std::endl;
                        usleep(100000); // 100ms
                        continue;
                    }
                }
            }
        }
    };

    // 定义推理线程函数，负责从文本缓存中取出文本进行语音合成，并将结果放入语音缓存
    auto inference_thread_func = [&]() {
        while (running) {
            std::string text_to_process;
            bool should_process = false;
            
            // 从文本缓存队列中取出文本
            {   
                std::unique_lock<std::mutex> lock(text_cache_mutex);
                text_cache_cv.wait(lock, [&]() {
                    return !text_cache.empty() || !running || llm_finished;
                });
                
                if (!running) break;
                
                if (!text_cache.empty()) {
                    text_to_process = text_cache.front();
                    text_cache.pop();
                    should_process = true;
                }
            }
            
            // 处理文本
            if (should_process && !text_to_process.empty()) {
                std::vector<float> audio_data;
                std::vector<std::string> local_sentences;
                
                // Split sentences
                local_sentences = split_sentence(text_to_process, 40, args.language);
                
                // inference
                timer.tik();
                for (auto& s : local_sentences) {
                    printf("Split sentence: %s\n", s.c_str());
                    std::vector<float> output_data(PREDICTED_LENGTHS_MAX*PREDICTED_BATCH); 

	                // Convert sentence to phones and tones
                    s = "_" + s + "_";
                    std::vector<int> phones_bef, tones_bef;
                    lexicon.convert(s, phones_bef, tones_bef);

                    std::vector<int> lang_ids_bef(phones_bef.size(), value);

                    // Add blank between words
                    auto phones = intersperse(phones_bef, 0);
                    auto tones = intersperse(tones_bef, 0);
                    auto lang_ids = intersperse(lang_ids_bef, 0);

                    int64_t phone_len = phones.size();

                    // pad or trim
                    pad_or_trim(tones, MAX_LENGTH);
                    pad_or_trim(phones, MAX_LENGTH);
                    pad_or_trim(lang_ids, MAX_LENGTH);

                    int output_lengths = inference_melotts_model(&rknn_app_ctx, phones, phone_len, tones, lang_ids, args.speak_id, args.speed, args.disable_bert, output_data);
                    if (output_lengths < 0)
                    {
                        printf("inference_melotts_model fail! ret=%d\n", output_lengths);
                        break;
                    }

                    int actual_size = output_lengths * PREDICTED_BATCH;
                    audio_data.insert(audio_data.end(), output_data.begin(), output_data.begin() + actual_size);
                }
                timer.tok();
                timer.print_time("inference ");

                if (!audio_data.empty()) {
                    float local_audio_length = (float)audio_data.size() / SAMPLE_RATE;        // sec
                    printf("audio_length: %f", local_audio_length);
                    
                    // 将生成的语音添加到语音缓存队列
                    {   
                        std::lock_guard<std::mutex> lock(audio_cache_mutex);
                        audio_cache.push(audio_data);
                    }
                    audio_cache_cv.notify_all();
                }
            }
            
            // // 检查是否应该发送DONE信号
            // {   
            //     std::lock_guard<std::mutex> lock(text_cache_mutex);
            //     if (llm_finished && text_cache.empty()) {
            //         std::cout << "Sending done signal to ASR..." << std::endl;
            //         // 确保管道存在
            //         if (access(PIPE_PATH_TTS_TO_ASR, F_OK) != 0) {
            //             std::cerr << "Pipe does not exist, creating it..." << std::endl;
            //             int ret = mkfifo(PIPE_PATH_TTS_TO_ASR, 0666);
            //             if (ret != 0) {
            //                 std::cerr << "Failed to create pipe" << std::endl;
            //             }
            //         }
            //         // 发送完成信号
            //         send_to_pipe(PIPE_PATH_TTS_TO_ASR, "DONE");
                    
            //         // 重置LLM完成标志，准备下一轮
            //         llm_finished = false;
            //     }
            // }
        }
    };

    // 定义音频播放线程函数，负责从语音缓存中取出音频进行播放
    auto audio_playback_thread_func = [&]() {
        while (running) {
            std::vector<float> audio_to_play;
            bool should_play = false;
            
            // 从语音缓存队列中取出音频
            {   
                std::unique_lock<std::mutex> lock(audio_cache_mutex);
                audio_cache_cv.wait(lock, [&]() {
                    return !audio_cache.empty() || !running;
                });
                
                if (!running) break;
                
                if (!audio_cache.empty()) {
                    audio_to_play = audio_cache.front();
                    audio_cache.pop();
                    should_play = true;
                }
            }
            
            // 播放音频
                if (should_play && !audio_to_play.empty()) {
                    std::cout << "Playing audio..." << std::endl;
                    timer.tik();
                    // 设置音量为0.5（范围0.0-1.0），可以根据需要调整
                    float volume = 0.5f;
                    ret = play_audio(audio_to_play.data(), audio_to_play.size(), SAMPLE_RATE, 1, volume);
                    if (ret != 0)
                    {
                        printf("play_audio fail! ret=%d\n", ret);
                        continue;
                    }
                    timer.tok();
                    timer.print_time("play_audio");

                    // 继续等待下一轮音频
                    std::cout << "\nWaiting for next audio...\n";
                }

            // 检查是否应该发送DONE信号
            {   
                std::lock_guard<std::mutex> lock(audio_cache_mutex);
                if (llm_finished && audio_cache.empty()) {
                    std::cout << "Sending done signal to ASR..." << std::endl;
                    // 确保管道存在
                    if (access(PIPE_PATH_TTS_TO_ASR, F_OK) != 0) {
                        std::cerr << "Pipe does not exist, creating it..." << std::endl;
                        int ret = mkfifo(PIPE_PATH_TTS_TO_ASR, 0666);
                        if (ret != 0) {
                            std::cerr << "Failed to create pipe" << std::endl;
                        }
                    }
                    // 发送完成信号
                    send_to_pipe(PIPE_PATH_TTS_TO_ASR, "DONE");
                    
                    // 重置LLM完成标志，准备下一轮
                    llm_finished = false;
                }
            }
        }
    };

    // init encoder model
    timer.tik();
    ret = init_melotts_model(encoder_path, &rknn_app_ctx.encoder_context);
    if (ret != 0)
    {
        printf("init_melotts_model fail! ret=%d encoder_path=%s\n", ret, encoder_path);
        goto cleanup;
    }
    timer.tok();
    timer.print_time("init_melotts_encoder_model");

    // init encoder model
    timer.tik();
    ret = init_melotts_model(decoder_path, &rknn_app_ctx.decoder_context);
    if (ret != 0)
    {
        printf("init_melotts_model fail! ret=%d decoder_path=%s\n", ret, decoder_path);
        goto cleanup;
    }
    timer.tok();
    timer.print_time("init_melotts_decoder_model");

    // init bert    
    if(!args.disable_bert) {
        // TODO init_melotts_model
        goto cleanup;
    } else {
        std::cout << "disable bert model" << std::endl;
    }

    std::cout << "TTS service started. Waiting for text...\n";

    // 创建LLM到TTS的完成消息管道
    if (access(PIPE_PATH_LLM_TO_TTS_FINISH, F_OK) == 0) {
        // 管道已存在，删除旧管道
        if (unlink(PIPE_PATH_LLM_TO_TTS_FINISH) != 0) {
            std::cerr << "Failed to remove existing finish pipe" << std::endl;
        }
    }
    // 创建新管道
    if (mkfifo(PIPE_PATH_LLM_TO_TTS_FINISH, 0666) != 0) {
        std::cerr << "Failed to create finish pipe" << std::endl;
    }

    // 打开两个管道，在整个主循环中保持打开状态
    text_fd = open(PIPE_PATH_LLM_TO_TTS, O_RDONLY | O_NONBLOCK);
    finish_fd = open(PIPE_PATH_LLM_TO_TTS_FINISH, O_RDONLY | O_NONBLOCK);
    
    if (text_fd == -1 || finish_fd == -1) {
        std::cerr << "Failed to open pipes, exiting..." << std::endl;
        if (text_fd != -1) close(text_fd);
        if (finish_fd != -1) close(finish_fd);
        goto cleanup;
    }

    // 启动三个线程
    receive_thread = std::thread(receive_thread_func);
    inference_thread = std::thread(inference_thread_func);
    audio_playback_thread = std::thread(audio_playback_thread_func);

    // 等待线程结束
    if (receive_thread.joinable()) {
        receive_thread.join();
    }
    if (inference_thread.joinable()) {
        inference_thread.join();
    }
    if (audio_playback_thread.joinable()) {
        audio_playback_thread.join();
    }

    // 关闭管道
    if (text_fd != -1) close(text_fd);
    if (finish_fd != -1) close(finish_fd);

cleanup:
    // 确保管道被关闭
    if (text_fd != -1) {
        close(text_fd);
        text_fd = -1;
    }
    if (finish_fd != -1) {
        close(finish_fd);
        finish_fd = -1;
    }
    
    // 重置标志，确保线程能够退出
    running = false;
    llm_finished = true;
    text_cache_cv.notify_all();
    audio_cache_cv.notify_all();
    
    // 等待线程结束（如果线程已经启动）
    if (receive_thread.joinable()) {
        receive_thread.join();
    }
    if (inference_thread.joinable()) {
        inference_thread.join();
    }
    if (audio_playback_thread.joinable()) {
        audio_playback_thread.join();
    }
    
    // release model
    ret = release_melotts_model(&rknn_app_ctx.encoder_context);
    if (ret != 0)
    {
        printf("release_mms_tts_model encoder_context fail! ret=%d\n", ret);
    }
    ret = release_melotts_model(&rknn_app_ctx.decoder_context);
    if (ret != 0)
    {
        printf("release_ppocr_model decoder_context fail! ret=%d\n", ret);
    }
    std::cout << "TTS service stopped." << std::endl;
    return 0;
}

