#include "memory/shared_memory_pool.h"
#include <iostream>
#include <algorithm>
#include <sstream>

SharedMemoryPool::SharedMemoryPool(size_t total_size, size_t chunk_size)
    : total_size_(total_size), chunk_size_(chunk_size) {
    // 计算能创建的内存块数量
    num_chunks_ = total_size_ / chunk_size_;
    if (num_chunks_ == 0) num_chunks_ = 1;
    
    // 初始化共享内存池
    Initialize();
}

SharedMemoryPool::~SharedMemoryPool() {
    // 清理所有共享内存块
    chunks_.clear();
    metadata_.clear();
}

void SharedMemoryPool::Initialize() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    // 预分配所有元数据
    metadata_.resize(num_chunks_);
    chunks_.resize(num_chunks_);
    
    // 创建所有内存块并初始化元数据
    for (size_t i = 0; i < num_chunks_; ++i) {
        std::stringstream ss;
        ss << "perfetto_shm_" << i;
        
        try {
            chunks_[i] = std::make_shared<SharedMemory>(ss.str(), chunk_size_);
            
            // 设置元数据
            metadata_[i].chunk_id = i;
            metadata_[i].size = chunk_size_;
            metadata_[i].in_use = false;
            metadata_[i].sequence_number = 0;
        } catch (const std::exception& e) {
            std::cerr << "Failed to create shared memory chunk " << i << ": " << e.what() << std::endl;
        }
    }
    
    std::cout << "Initialized shared memory pool with " << num_chunks_ 
              << " chunks of " << chunk_size_ << " bytes each" << std::endl;
}

std::shared_ptr<SharedMemory> SharedMemoryPool::AcquireChunk() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    int free_index = FindFreeChunkLocked();
    if (free_index < 0) {
        std::cerr << "No free chunks available in the shared memory pool" << std::endl;
        return nullptr;
    }
    
    // 标记为使用中
    metadata_[free_index].in_use = true;
    metadata_[free_index].sequence_number = next_sequence_number_++;
    
    return chunks_[free_index];
}

void SharedMemoryPool::ReleaseChunk(std::shared_ptr<SharedMemory> chunk) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    // 查找对应的块
    auto it = std::find(chunks_.begin(), chunks_.end(), chunk);
    if (it != chunks_.end()) {
        size_t index = std::distance(chunks_.begin(), it);
        metadata_[index].in_use = false;
        
        // 通知等待的线程有新块可用
        chunk_available_cv_.notify_one();
    }
}

bool SharedMemoryPool::HasFreeChunks() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return FindFreeChunkLocked() >= 0;
}

double SharedMemoryPool::GetUtilizationRate() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    size_t used_count = 0;
    for (const auto& meta : metadata_) {
        if (meta.in_use) used_count++;
    }
    
    return static_cast<double>(used_count) / metadata_.size();
}

size_t SharedMemoryPool::GetTotalChunks() const {
    return num_chunks_;
}

bool SharedMemoryPool::WaitForFreeChunk(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
    // 如果已经有可用块，立即返回
    if (FindFreeChunkLocked() >= 0) {
        return true;
    }
    
    // 等待直到有可用块或超时
    return chunk_available_cv_.wait_for(lock, timeout, [this]() {
        return FindFreeChunkLocked() >= 0;
    });
}

int SharedMemoryPool::FindFreeChunkLocked() const {
    for (size_t i = 0; i < metadata_.size(); ++i) {
        if (!metadata_[i].in_use) {
            return static_cast<int>(i);
        }
    }
    return -1;
} 