#include <string.h>
#include <unistd.h>
#include <string>
#include "rkllm.h"
#include <fstream>
#include <iostream>
#include <csignal>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;
LLMHandle llmHandle = nullptr;
std::string llm_output_buffer; // 用于捕获LLM输出的缓冲区
std::string last_sent_output; // 用于记录上次发送给TTS的内容


#define PROMPT_TEXT_PREFIX "<｜begin▁of▁sentence｜><｜User｜>"
#define PROMPT_TEXT_POSTFIX "<｜Assistant｜>"

// 管道通信相关常量
#define PIPE_PATH_ASR_TO_LLM "/tmp/asr_to_llm_pipe"
#define PIPE_PATH_LLM_TO_TTS "/tmp/llm_to_tts_pipe"
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
        std::cout << "\nSuccessfully sent data: " << data << std::endl;
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

void exit_handler(int signal)
{
    if (llmHandle != nullptr)
    {
        {
            cout << "程序即将退出" << endl;
            LLMHandle _tmp = llmHandle;
            llmHandle = nullptr;
            rkllm_destroy(_tmp);
        }
    }
    exit(signal);
}

// 计算字符串中的汉字数量
int count_chinese_chars(const std::string& str) {
    int count = 0;
    for (char c : str) {
        // 汉字的Unicode范围
        if (static_cast<unsigned char>(c) >= 0x80) {
            count++;
        }
    }
    return count / 2; // 每个汉字占2个字节
}

// 发送部分LLM输出给TTS
void send_partial_output_to_tts() {
    if (!llm_output_buffer.empty()) {
        // 过滤掉<think>标签及其内容
        std::string filtered_output = llm_output_buffer;
        size_t think_start = filtered_output.find("<think>");
        while (think_start != std::string::npos) {
            size_t think_end = filtered_output.find("</think>", think_start);
            if (think_end != std::string::npos) {
                // 移除<think>标签及其内容
                filtered_output.erase(think_start, think_end - think_start + 8);
            } else {
                break;
            }
            think_start = filtered_output.find("<think>");
        }
        
        // 移除多余的空白字符
        size_t start = filtered_output.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            filtered_output = filtered_output.substr(start);
        }
        size_t end = filtered_output.find_last_not_of(" \t\n\r");
        if (end != std::string::npos) {
            filtered_output = filtered_output.substr(0, end + 1);
        }
        
        // 只有在过滤后的输出不为空时才考虑发送
        if (!filtered_output.empty()) {
            // 计算当前已发送的内容长度
            size_t current_pos = last_sent_output.size();
            
            // 确保当前位置不超过过滤后输出的长度
            if (current_pos < filtered_output.size()) {
                // 从当前位置开始的剩余内容
                std::string remaining_output = filtered_output.substr(current_pos);
                
                // 计算剩余内容中的汉字数量
                int chinese_count = count_chinese_chars(remaining_output);
                
                // 当超过10个汉字，且遇到标点符号时发送
                // cout << "chinese_count" << chinese_count <<endl;
                if (chinese_count >= 50) {
                    // 查找第一个标点符号的位置（，。！？）
                    size_t punctuation_pos = remaining_output.find_last_of("，。；：！？");
                    if (punctuation_pos != std::string::npos) {
                        // 发送到第一个标点符号的结尾
                        std::string segment = remaining_output.substr(0, punctuation_pos + 1);
                        
                        // 检查内容是否为空
                        if (!segment.empty()) {
                            // cout << "segment" << segment <<endl;
                            // 检查内容是否不为空
                            if (!segment.empty()) {
                                // std::cout << "Sending segment to TTS: " << segment << std::endl;
                                send_to_pipe(PIPE_PATH_LLM_TO_TTS, segment);
                                // 更新已发送的内容
                                last_sent_output = filtered_output.substr(0, current_pos + punctuation_pos + 1);
                            }
                        }
                    }
                }
            }
        }
    }
}

void callback(RKLLMResult *result, void *userdata, LLMCallState state)
{
    if (state == RKLLM_RUN_FINISH)
    {
        printf("\n");
        // 推理完成，发送剩余的所有内容给TTS工程
        if (!llm_output_buffer.empty()) {
            // 过滤掉<think>标签及其内容
            std::string filtered_output = llm_output_buffer;
            size_t think_start = filtered_output.find("<think>");
            while (think_start != std::string::npos) {
                size_t think_end = filtered_output.find("</think>", think_start);
                if (think_end != std::string::npos) {
                    // 移除<think>标签及其内容
                    filtered_output.erase(think_start, think_end - think_start + 8);
                } else {
                    break;
                }
                think_start = filtered_output.find("<think>");
            }
            
            // 移除多余的空白字符
            size_t start = filtered_output.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) {
                filtered_output = filtered_output.substr(start);
            }
            size_t end = filtered_output.find_last_not_of(" \t\n\r");
            if (end != std::string::npos) {
                filtered_output = filtered_output.substr(0, end + 1);
            }
            
            // 发送剩余的内容
            if (!filtered_output.empty() /*&& !last_sent_output.empty()*/ && filtered_output.size() > last_sent_output.size()) {
                // 只发送新增的内容（与上次发送的内容相比）
                std::string remaining_content = filtered_output.substr(last_sent_output.size());
                
                // 检查内容是否为空，并且过滤掉乱码
                if (!remaining_content.empty()) {
                    // // 移除所有非打印字符和乱码
                    // std::string clean_content;
                    // for (char c : remaining_content) {
                    //     if (isprint(static_cast<unsigned char>(c)) || c == ' ' || c == '\n' || c == '\t') {
                    //         clean_content += c;
                    //     }
                    // }

                    
                    // 检查清理后的内容是否不为空
                    if (!remaining_content.empty()) {
                        // std::cout << "Sending remaining content to TTS: " << remaining_content << std::endl;
                        send_to_pipe(PIPE_PATH_LLM_TO_TTS, remaining_content);
                        last_sent_output = filtered_output;
                    }
                }
            }
        }
        std::cout << "LLM inference completed.\n";
        
        // 发送推理完成消息给TTS工程
        send_to_pipe(PIPE_PATH_LLM_TO_TTS_FINISH, "FINISH");
    } else if (state == RKLLM_RUN_ERROR) {
        printf("\\run error\n");
    } else if (state == RKLLM_RUN_GET_LAST_HIDDEN_LAYER) {
        /* ================================================================================================================
        若使用GET_LAST_HIDDEN_LAYER功能,callback接口会回传内存指针:last_hidden_layer,token数量:num_tokens与隐藏层大小:embd_size
        通过这三个参数可以取得last_hidden_layer中的数据
        注:需要在当前callback中获取,若未及时获取,下一次callback会将该指针释放
        ===============================================================================================================*/
        if (result->last_hidden_layer.embd_size != 0 && result->last_hidden_layer.num_tokens != 0) {
            int data_size = result->last_hidden_layer.embd_size * result->last_hidden_layer.num_tokens * sizeof(float);
            printf("\ndata_size:%d",data_size);
            std::ofstream outFile("last_hidden_layer.bin", std::ios::binary);
            if (outFile.is_open()) {
                outFile.write(reinterpret_cast<const char*>(result->last_hidden_layer.hidden_states), data_size);
                outFile.close();
                std::cout << "Data saved to output.bin successfully!" << std::endl;
            } else {
                std::cerr << "Failed to open the file for writing!" << std::endl;
            }
        }
    } else if (state == RKLLM_RUN_NORMAL) {
        // 捕获LLM输出到缓冲区
        llm_output_buffer += result->text;
        // 同时打印输出，方便调试
        printf("%s", result->text);
        
        // 只有当积累了足够的文字后才检查是否需要发送部分输出给TTS
        send_partial_output_to_tts();
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " model_path max_new_tokens max_context_len\n";
        return 1;
    }

    signal(SIGINT, exit_handler);
    printf("rkllm init start\n");

    //设置参数及初始化
    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = argv[1];

    //设置采样参数
    param.top_k = 1;
    param.top_p = 0.95;
    param.temperature = 0.8;
    param.repeat_penalty = 1.1;
    param.frequency_penalty = 0.0;
    param.presence_penalty = 0.0;

    param.max_new_tokens = std::atoi(argv[2]);
    param.max_context_len = std::atoi(argv[3]);
    param.skip_special_token = true;
    param.extend_param.base_domain_id = 0;

    int ret = rkllm_init(&llmHandle, &param, callback);
    if (ret == 0){
        printf("rkllm init success\n");
    } else {
        printf("rkllm init failed\n");
        exit_handler(-1);
    }

    // 创建管道
    ret = create_pipe(PIPE_PATH_ASR_TO_LLM);
    if (ret != 0) {
        printf("create asr to llm pipe fail! ret=%d\n", ret);
        exit_handler(-1);
    }
    ret = create_pipe(PIPE_PATH_LLM_TO_TTS);
    if (ret != 0) {
        printf("create llm to tts pipe fail! ret=%d\n", ret);
        exit_handler(-1);
    }
    ret = create_pipe(PIPE_PATH_LLM_TO_TTS_FINISH);
    if (ret != 0) {
        printf("create llm to tts finish pipe fail! ret=%d\n", ret);
        exit_handler(-1);
    }
    printf("Created pipes: %s, %s and %s\n", PIPE_PATH_ASR_TO_LLM, PIPE_PATH_LLM_TO_TTS, PIPE_PATH_LLM_TO_TTS_FINISH);

    string text;
    RKLLMInput rkllm_input;
    string asr_input;
    string llm_output;

    // 初始化 infer 参数结构体
    RKLLMInferParam rkllm_infer_params;
    memset(&rkllm_infer_params, 0, sizeof(RKLLMInferParam));  // 将所有内容初始化为 0
    rkllm_infer_params.mode = RKLLM_INFER_GENERATE;

    std::cout << "LLM service started. Waiting for text from ASR...\n";

    // 主循环，持续监听ASR输入
    while (true)
    {
        // 从ASR接收输入
        ret = receive_from_pipe(PIPE_PATH_ASR_TO_LLM, asr_input);
        if (ret < 0) {
            std::cerr << "Failed to receive text from ASR, retrying...\n";
            // 重试，避免因临时错误退出
            usleep(100000); // 100ms
            continue;
        }

        // 检查是否收到退出信号
        if (asr_input == "EXIT") {
            std::cout << "Received exit signal, shutting down...\n";
            break;
        }

        std::cout << "Received text from ASR: " << asr_input << std::endl;

        // 检查是否为拍照意图
        bool is_photo_intent = false;
        std::string input = asr_input;
        
        // 否定词列表，用于排除非拍照意图
        std::vector<std::string> negation_words = {"不要", "不想", "不想要", "不是", "不要想", "别", "别想", "别要", "不要让", "不要叫", "不要请", "不要帮", "不要给"};
        
        // 拍照意图关键词列表
        std::vector<std::string> photo_keywords = {
            "拍照", "照相", "拍照片", "帮我拍照", "拍个照片", "给我拍照", "我要拍照", "拍张照片", "拍张照","拍个照",
            "能帮我拍照吗", "可以帮我拍照吗", "麻烦帮我拍照", "请帮我拍照", "帮我照个相", "帮我拍张照",
            "我想拍照", "我想要拍照", "我需要拍照", "帮我拍一下", "给我拍一下", "拍一下照片"
        };
        
        // 检查是否包含否定词
        bool has_negation = false;
        for (const std::string &neg_word : negation_words) {
            if (input.find(neg_word) != std::string::npos) {
                has_negation = true;
                break;
            }
        }
        
        // 如果没有否定词，再检查是否包含拍照关键词
        if (!has_negation) {
            // 检查是否包含拍照关键词
            bool has_photo_keyword = false;
            for (const std::string &keyword : photo_keywords) {
                if (input.find(keyword) != std::string::npos) {
                    has_photo_keyword = true;
                    break;
                }
            }
            
            // 进一步检查上下文，确保是真正的拍照意图
            if (has_photo_keyword) {
                // 排除一些明显不是拍照意图的情况
                std::vector<std::string> exclude_patterns = {
                    "拍照技术", "拍照技巧", "拍照方法", "拍照教程", "拍照学习",
                    "拍照好看", "拍照漂亮", "拍照美", "拍照不错", "拍照好",
                    "拍照功能", "拍照模式", "拍照设置", "拍照参数", "拍照效果"
                };
                
                bool should_exclude = false;
                for (const std::string &pattern : exclude_patterns) {
                    if (input.find(pattern) != std::string::npos) {
                        should_exclude = true;
                        break;
                    }
                }
                
                if (!should_exclude) {
                    is_photo_intent = true;
                }
            }
        }
        
        if (is_photo_intent) {
            // 识别到拍照意图
            std::cout << "识别到拍照意图，执行拍照操作..." << std::endl;
            
            // 打印并发送拍照提示
            std::string photo_response = "好的，正在为你拍照";
            std::cout << photo_response << std::endl;
            
            // 发送给TTS工程
            send_to_pipe(PIPE_PATH_LLM_TO_TTS, photo_response);
            
            // 发送FINISH信号
            send_to_pipe(PIPE_PATH_LLM_TO_TTS_FINISH, "FINISH");
        } else {
            // 非拍照意图，按原逻辑处理
            // 构建LLM输入
            text = PROMPT_TEXT_PREFIX + asr_input + PROMPT_TEXT_POSTFIX;
            rkllm_input.input_type = RKLLM_INPUT_PROMPT;
            rkllm_input.prompt_input = (char *)text.c_str();
            printf("Processing with LLM...\n");

            // 清空之前的输出缓冲区
            llm_output_buffer.clear();
            last_sent_output.clear();

            // 执行LLM推理
            rkllm_run(llmHandle, &rkllm_input, &rkllm_infer_params, NULL);
        }

        // 发送LLM输出给TTS，过滤掉<think>标签及其内容
        // if (!llm_output_buffer.empty()) {
        //     // 过滤掉<think>标签及其内容
        //     std::string filtered_output = llm_output_buffer;
        //     size_t think_start = filtered_output.find("<think>");
        //     while (think_start != std::string::npos) {
        //         size_t think_end = filtered_output.find("</think>", think_start);
        //         if (think_end != std::string::npos) {
        //             // 移除<think>标签及其内容
        //             filtered_output.erase(think_start, think_end - think_start + 8);
        //         } else {
        //             break;
        //         }
        //         think_start = filtered_output.find("<think>");
        //     }
            
        //     // 移除多余的空白字符
        //     size_t start = filtered_output.find_first_not_of(" \t\n\r");
        //     if (start != std::string::npos) {
        //         filtered_output = filtered_output.substr(start);
        //     }
        //     size_t end = filtered_output.find_last_not_of(" \t\n\r");
        //     if (end != std::string::npos) {
        //         filtered_output = filtered_output.substr(0, end + 1);
        //     }
            
        //     // 发送过滤后的内容
        //     if (!filtered_output.empty()) {
        //         // std::cout << "Sending text to TTS: " << filtered_output << std::endl;
        //         send_to_pipe(PIPE_PATH_LLM_TO_TTS, filtered_output);
        //     } else {
        //         std::cerr << "Filtered LLM output is empty, skipping TTS...\n";
        //     }
        // } else {
        //     std::cerr << "LLM output is empty, skipping TTS...\n";
        // }

        // 不再发送完成信号给ASR，由TTS在播放完成后发送
        std::cout << "LLM processing completed. Waiting for next text from ASR...\n";

        // 继续等待下一轮输入
        std::cout << "\nWaiting for next text from ASR...\n";
    }

    // 清理管道
    if (access(PIPE_PATH_ASR_TO_LLM, F_OK) == 0) {
        unlink(PIPE_PATH_ASR_TO_LLM);
    }
    if (access(PIPE_PATH_LLM_TO_TTS, F_OK) == 0) {
        unlink(PIPE_PATH_LLM_TO_TTS);
    }
    if (access(PIPE_PATH_LLM_TO_TTS_FINISH, F_OK) == 0) {
        unlink(PIPE_PATH_LLM_TO_TTS_FINISH);
    }
    printf("Cleaned up pipes\n");

    rkllm_destroy(llmHandle);

    return 0;
}
