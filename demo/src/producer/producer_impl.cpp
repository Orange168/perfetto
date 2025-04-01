#include "producer/producer_impl.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>

// 用于IPC消息的头部
struct MessageHeader {
    uint32_t message_type;
    uint32_t payload_size;
};

// 消息类型
enum MessageType {
    REGISTER_DATA_SOURCE = 1,
    START_TRACING = 2,
    STOP_TRACING = 3,
    FLUSH = 4,
    TRACE_DATA = 5
};

ProducerImpl::ProducerImpl() 
    : memory_pool_(1024*1024, 4096) {  // 1MB池，4KB块大小
    
    // 尝试连接到服务
    if (Connect()) {
        std::cout << "Producer connected to service" << std::endl;
        connected_ = true;
    } else {
        std::cerr << "Producer failed to connect to service, will retry" << std::endl;
    }
    
    // 启动命令处理线程
    command_thread_ = std::thread(&ProducerImpl::CommandThread, this);
}

ProducerImpl::~ProducerImpl() {
    // 设置运行标志为false
    running_ = false;
    
    // 通知命令线程退出
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        ProducerCommand cmd;
        cmd.cmd = ProducerCmd::SHUTDOWN;
        command_queue_.push(cmd);
        command_cv_.notify_one();
    }
    
    // 等待线程结束
    if (command_thread_.joinable()) {
        command_thread_.join();
    }
    
    // 停止所有数据源
    if (tracing_) {
        StopTracing();
    }
    
    std::cout << "Producer shutting down" << std::endl;
}

bool ProducerImpl::Connect() {
    // 连接到服务
    bool success = connection_.Connect(SocketConnection::kServiceSocket);
    if (!success) {
        return false;
    }
    
    // 在连接成功后注册数据源
    RegisterDataSourcesWithService();
    return true;
}

void ProducerImpl::RegisterDataSource(const std::string& name, 
                                     std::function<std::unique_ptr<DataSource>()> factory) {
    std::cout << "Registering data source: " << name << std::endl;
    
    // 存储工厂函数
    factories_[name] = factory;
    
    // 如果已连接，立即向服务注册
    if (connected_) {
        // 创建注册消息
        MessageHeader header;
        header.message_type = REGISTER_DATA_SOURCE;
        header.payload_size = name.size() + 1;  // +1为了NULL终止符
        
        // 发送消息头
        if (!SendMessage(&header, sizeof(header))) {
            std::cerr << "Failed to send data source registration header" << std::endl;
            return;
        }
        
        // 发送数据源名称
        if (!SendMessage(name.c_str(), name.size() + 1)) {
            std::cerr << "Failed to send data source name" << std::endl;
            return;
        }
        
        std::cout << "Data source registered with service: " << name << std::endl;
    }
}

void ProducerImpl::RegisterDataSourcesWithService() {
    // 向服务注册所有数据源
    for (const auto& entry : factories_) {
        // 创建注册消息
        MessageHeader header;
        header.message_type = REGISTER_DATA_SOURCE;
        header.payload_size = entry.first.size() + 1;  // +1为了NULL终止符
        
        // 发送消息头
        if (!SendMessage(&header, sizeof(header))) {
            std::cerr << "Failed to send data source registration header" << std::endl;
            continue;
        }
        
        // 发送数据源名称
        if (!SendMessage(entry.first.c_str(), entry.first.size() + 1)) {
            std::cerr << "Failed to send data source name" << std::endl;
            continue;
        }
        
        std::cout << "Data source registered with service: " << entry.first << std::endl;
    }
}

void ProducerImpl::StartTracing(const std::string& config) {
    if (tracing_) {
        std::cout << "Tracing already in progress" << std::endl;
        return;
    }
    
    std::cout << "Starting tracing with config: " << config << std::endl;
    
    // 激活所有数据源
    for (const auto& entry : factories_) {
        // 创建数据源实例
        auto ds = entry.second();
        if (ds) {
            // 设置数据源
            DataSourceDescriptor desc;
            desc.name = entry.first;
            desc.config = config;
            ds->OnSetup(desc);
            
            // 启动数据源
            ds->OnStart();
            
            // 存储活动数据源
            active_sources_[entry.first] = std::move(ds);
        }
    }
    
    tracing_ = true;
    std::cout << "Tracing started" << std::endl;
}

void ProducerImpl::StopTracing() {
    if (!tracing_) {
        std::cout << "No active tracing session" << std::endl;
        return;
    }
    
    std::cout << "Stopping tracing" << std::endl;
    
    // 停止所有活动数据源
    for (auto& entry : active_sources_) {
        if (entry.second) {
            entry.second->OnStop();
        }
    }
    
    // 清空活动数据源列表
    active_sources_.clear();
    
    tracing_ = false;
    std::cout << "Tracing stopped" << std::endl;
}

void ProducerImpl::Flush() {
    std::cout << "Flushing trace buffers" << std::endl;
    
    // 这里应该执行实际的刷新逻辑
    // 在真实的实现中，这将确保所有数据都被写入并可用于消费者
}

void ProducerImpl::WriteTraceData(const std::string& source_name, const void* data, size_t data_size) {
    if (!tracing_) {
        std::cerr << "Cannot write trace data: tracing not active" << std::endl;
        return;
    }
    
    // 获取一个共享内存块
    auto chunk = memory_pool_.AcquireChunk();
    if (!chunk) {
        std::cerr << "Failed to acquire memory chunk for trace data" << std::endl;
        return;
    }
    
    // 检查数据大小
    if (data_size > chunk->size()) {
        std::cerr << "Trace data too large for memory chunk" << std::endl;
        memory_pool_.ReleaseChunk(chunk);
        return;
    }
    
    // 复制数据到共享内存
    memcpy(chunk->data(), data, data_size);
    
    // 创建头部消息
    MessageHeader header;
    header.message_type = TRACE_DATA;
    header.payload_size = sizeof(uint32_t) + source_name.size() + 1;  // chunk ID + source name
    
    // 发送头部
    if (!SendMessage(&header, sizeof(header))) {
        std::cerr << "Failed to send trace data header" << std::endl;
        memory_pool_.ReleaseChunk(chunk);
        return;
    }
    
    // 发送chunk信息和源名称
    // 在真实实现中，我们只发送块的引用，不发送实际数据（数据在共享内存中）
    uint32_t chunk_id = 1;  // 示例ID，实际中会有真实的ID
    if (!SendMessage(&chunk_id, sizeof(chunk_id))) {
        std::cerr << "Failed to send chunk ID" << std::endl;
        memory_pool_.ReleaseChunk(chunk);
        return;
    }
    
    if (!SendMessage(source_name.c_str(), source_name.size() + 1)) {
        std::cerr << "Failed to send source name" << std::endl;
        memory_pool_.ReleaseChunk(chunk);
        return;
    }
    
    std::cout << "Wrote " << data_size << " bytes of trace data from source " 
              << source_name << std::endl;
    
    // 这里我们模拟处理完成，释放块
    // 在真实的实现中，服务会告诉我们何时可以释放
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    memory_pool_.ReleaseChunk(chunk);
}

bool ProducerImpl::SendMessage(const void* data, size_t size) {
    if (!connected_) {
        std::cerr << "Cannot send message: not connected to service" << std::endl;
        return false;
    }
    
    return connection_.Send(data, size);
}

void ProducerImpl::CommandThread() {
    while (running_) {
        // 如果未连接，尝试重新连接
        if (!connected_) {
            if (Connect()) {
                connected_ = true;
                std::cout << "Producer reconnected to service" << std::endl;
            } else {
                // 等待一段时间后重试
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }
        
        // 检查命令队列
        ProducerCommand cmd;
        {
            std::unique_lock<std::mutex> lock(command_mutex_);
            if (command_queue_.empty()) {
                // 等待新命令或超时
                command_cv_.wait_for(lock, std::chrono::milliseconds(100));
                if (command_queue_.empty()) {
                    // 尝试从服务接收命令
                    MessageHeader header;
                    if (connection_.Recv(&header, sizeof(header))) {
                        // 解析命令
                        switch (header.message_type) {
                            case START_TRACING: {
                                cmd.cmd = ProducerCmd::START_TRACING;
                                // 读取配置
                                if (header.payload_size > 0) {
                                    std::vector<char> config_buffer(header.payload_size);
                                    if (connection_.Recv(config_buffer.data(), header.payload_size)) {
                                        cmd.args = std::string(config_buffer.data());
                                    }
                                }
                                break;
                            }
                            case STOP_TRACING:
                                cmd.cmd = ProducerCmd::STOP_TRACING;
                                break;
                            case FLUSH:
                                cmd.cmd = ProducerCmd::FLUSH;
                                break;
                            default:
                                // 未知命令，继续循环
                                continue;
                        }
                    } else {
                        // 读取失败，可能连接断开
                        connected_ = false;
                        std::cerr << "Lost connection to service" << std::endl;
                        continue;
                    }
                } else {
                    // 从队列获取命令
                    cmd = command_queue_.front();
                    command_queue_.pop();
                }
            } else {
                // 从队列获取命令
                cmd = command_queue_.front();
                command_queue_.pop();
            }
        }
        
        // 处理命令
        HandleCommand(cmd);
    }
}

void ProducerImpl::HandleCommand(const ProducerCommand& cmd) {
    switch (cmd.cmd) {
        case ProducerCmd::START_TRACING:
            StartTracing(cmd.args);
            break;
            
        case ProducerCmd::STOP_TRACING:
            StopTracing();
            break;
            
        case ProducerCmd::FLUSH:
            Flush();
            break;
            
        case ProducerCmd::SHUTDOWN:
            // 已在析构函数中处理
            break;
    }
}

void ProducerImpl::HandleDataSourceCallback(const std::string& name, DataSourceEvent event) {
    std::cout << "Data source event: " << name << ", event: " << static_cast<int>(event) << std::endl;
    
    // 在真实实现中，根据事件类型执行特定操作
} 