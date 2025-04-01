#pragma once
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include "memory/shared_memory.h"

// 用于跟踪块的分配状态的元数据
struct ChunkMetadata {
    uint32_t chunk_id;
    size_t size;
    bool in_use;
    uint64_t sequence_number;
};

// 更完善的共享内存池，支持块状分配和环形缓冲区逻辑
class SharedMemoryPool {
public:
    SharedMemoryPool(size_t total_size = 1024*1024, size_t chunk_size = 4096);
    ~SharedMemoryPool();

    // 获取可用的共享内存块
    std::shared_ptr<SharedMemory> AcquireChunk();
    
    // 释放一个共享内存块，使其可被重用
    void ReleaseChunk(std::shared_ptr<SharedMemory> chunk);

    // 检查是否有可用的数据块
    bool HasFreeChunks() const;
    
    // 获取使用率
    double GetUtilizationRate() const;
    
    // 获取总块数
    size_t GetTotalChunks() const;
    
    // 等待直到有可用块或超时
    bool WaitForFreeChunk(std::chrono::milliseconds timeout);

private:
    std::vector<std::shared_ptr<SharedMemory>> chunks_;
    std::vector<ChunkMetadata> metadata_;
    size_t total_size_;
    size_t chunk_size_;
    size_t num_chunks_;
    std::atomic<uint64_t> next_sequence_number_{0};
    
    mutable std::mutex pool_mutex_;
    std::condition_variable chunk_available_cv_;

    // 初始化共享内存池
    void Initialize();
    
    // 查找一个可用的块
    int FindFreeChunkLocked() const;
}; 