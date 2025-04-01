#pragma once
#include "shared_memory.h"
#include <string>

class Producer {
public:
    Producer(const std::string& name) : shm_(name) {}

    void WriteData(const std::string& data) {
        if (shm_.data() != MAP_FAILED) {
            std::memcpy(shm_.data(), data.c_str(), data.size());
        }
    }

private:
    SharedMemory shm_;
}; 