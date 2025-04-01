#pragma once
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <cstring>

class SharedMemory {
public:
    static constexpr size_t kDefaultSize = 4096;  // 4KB

    SharedMemory(const std::string& name) : name_(name) {
        fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ != -1) {
            ftruncate(fd_, kDefaultSize);
            data_ = mmap(nullptr, kDefaultSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        }
    }

    ~SharedMemory() {
        if (data_ != MAP_FAILED) {
            munmap(data_, kDefaultSize);
        }
        if (fd_ != -1) {
            close(fd_);
            shm_unlink(name_.c_str());
        }
    }

    void* data() { return data_; }
    size_t size() const { return kDefaultSize; }

private:
    std::string name_;
    int fd_ = -1;
    void* data_ = MAP_FAILED;
}; 