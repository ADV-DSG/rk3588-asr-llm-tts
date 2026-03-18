#ifndef _PIPELINE_H_
#define _PIPELINE_H_

#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define PIPE_PATH_ASR_TO_TTS "/tmp/asr_to_tts_pipe"
#define PIPE_PATH_TTS_TO_ASR "/tmp/tts_to_asr_pipe"

class Pipeline {
private:
    int fd;
    std::string pipe_path;
    bool is_open;

public:
    Pipeline();
    ~Pipeline();

    // 创建管道
    int create(const std::string& path);

    // 打开管道用于读取
    int open_for_read(const std::string& path);

    // 打开管道用于写入
    int open_for_write(const std::string& path);

    // 关闭管道
    int close();

    // 发送数据
    int send(const std::string& data);

    // 接收数据
    int receive(std::string& data, size_t max_size = 1024);

    // 检查管道是否打开
    bool isOpen() const;
};

#endif // _PIPELINE_H_
