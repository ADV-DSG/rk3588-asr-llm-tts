#include "pipeline.h"
#include <cstring>
#include <iostream>

Pipeline::Pipeline() : fd(-1), is_open(false) {
}

Pipeline::~Pipeline() {
    if (is_open) {
        close();
    }
}

int Pipeline::create(const std::string& path) {
    // 检查管道是否已存在
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        // 管道已存在，删除旧管道
        if (unlink(path.c_str()) != 0) {
            std::cerr << "Failed to remove existing pipe" << std::endl;
            return -1;
        }
    }

    // 创建新管道
    if (mkfifo(path.c_str(), 0666) != 0) {
        std::cerr << "Failed to create pipe" << std::endl;
        return -1;
    }

    pipe_path = path;
    return 0;
}

int Pipeline::open_for_read(const std::string& path) {
    // 打开管道用于读取
    fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Failed to open pipe for reading" << std::endl;
        return -1;
    }

    pipe_path = path;
    is_open = true;
    return 0;
}

int Pipeline::open_for_write(const std::string& path) {
    // 打开管道用于写入
    fd = open(path.c_str(), O_WRONLY);
    if (fd == -1) {
        std::cerr << "Failed to open pipe for writing" << std::endl;
        return -1;
    }

    pipe_path = path;
    is_open = true;
    return 0;
}

int Pipeline::close() {
    if (is_open && fd != -1) {
        ::close(fd);
        fd = -1;
        is_open = false;
    }
    return 0;
}

int Pipeline::send(const std::string& data) {
    if (!is_open || fd == -1) {
        std::cerr << "Pipe is not open for writing" << std::endl;
        return -1;
    }

    // 发送数据
    ssize_t bytes_written = write(fd, data.c_str(), data.size());
    if (bytes_written == -1) {
        std::cerr << "Failed to write to pipe" << std::endl;
        return -1;
    }

    return bytes_written;
}

int Pipeline::receive(std::string& data, size_t max_size) {
    if (!is_open || fd == -1) {
        std::cerr << "Pipe is not open for reading" << std::endl;
        return -1;
    }

    // 接收数据
    char buffer[max_size];
    ssize_t bytes_read = read(fd, buffer, max_size - 1);
    if (bytes_read == -1) {
        std::cerr << "Failed to read from pipe" << std::endl;
        return -1;
    }

    // 处理接收到的数据
    buffer[bytes_read] = '\0';
    data = buffer;
    return bytes_read;
}

bool Pipeline::isOpen() const {
    return is_open;
}
