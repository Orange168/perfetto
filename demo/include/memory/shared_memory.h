#pragma once
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

class SharedMemory {
public:
    explicit SharedMemory(const std::string& name, size_t size = kDefaultSize);
    ~SharedMemory();

    void* data() { return data_; }
    size_t size() const { return size_; }

private:
    static constexpr size_t kDefaultSize = 4096;
    std::string name_;
    size_t size_;
    int fd_ = -1;
    void* data_ = MAP_FAILED;
}; 