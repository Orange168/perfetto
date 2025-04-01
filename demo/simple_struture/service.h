#pragma once
#include "shared_memory.h"
#include <string>
#include <vector>
#include <memory>

class Service {
public:
    Service(const std::string& shm_name) : shm_(shm_name) {}

    void ProcessData() {
        if (shm_.data() != MAP_FAILED) {
            // 将数据从共享内存移动到内部缓冲区
            buffer_.assign(static_cast<char*>(shm_.data()),
                         static_cast<char*>(shm_.data()) + shm_.size());
        }
    }

    const std::vector<char>& GetBuffer() const { return buffer_; }

private:
    SharedMemory shm_;
    std::vector<char> buffer_;
}; 