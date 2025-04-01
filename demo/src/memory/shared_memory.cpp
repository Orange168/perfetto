#include "memory/shared_memory.h"

SharedMemory::SharedMemory(const std::string& name, size_t size) 
    : name_(name), size_(size) {
    fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ != -1) {
        ftruncate(fd_, size_);
        data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    }
}

SharedMemory::~SharedMemory() {
    if (data_ != MAP_FAILED) {
        munmap(data_, size_);
    }
    if (fd_ != -1) {
        close(fd_);
        shm_unlink(name_.c_str());
    }
} 