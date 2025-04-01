#pragma once
#include <thread>
#include <vector>
#include <set>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <condition_variable>
#include "ipc/socket_connection.h"
#include "core/data_source.h"
#include "memory/shared_memory_pool.h"

// 表示连接的客户端类型
enum class ClientType {
    UNKNOWN,
    PRODUCER,
    CONSUMER
};

// 客户端连接
struct ClientConnection {
    SocketConnection connection;
    ClientType type = ClientType::UNKNOWN;
    std::string id;
    std::thread thread;
};

// 跟踪会话配置
struct TraceSessionConfig {
    std::string name;
    uint64_t buffer_size;
    uint32_t duration_ms;
    std::vector<std::string> data_sources;
};

// 注册的数据源信息
struct RegisteredDataSource {
    std::string name;
    std::string producer_id;
};

// 追踪服务实现
class ServiceImpl {
public:
    ServiceImpl();
    ~ServiceImpl();
    
    // 启动追踪会话
    bool StartTracing(const TraceSessionConfig& config);
    
    // 停止追踪会话
    bool StopTracing();
    
    // 获取注册的数据源列表
    std::vector<RegisteredDataSource> GetRegisteredDataSources() const;
    
    // 是否有活动的追踪会话
    bool IsTracingActive() const { return tracing_active_; }

private:
    // 启动接受客户端连接的线程
    void StartAcceptThread();
    
    // 处理客户端连接
    void HandleClientConnection(ClientConnection& client);
    
    // 处理来自生产者的消息
    void HandleProducerMessage(ClientConnection& client, uint32_t message_type, const std::vector<char>& data);
    
    // 处理来自消费者的消息
    void HandleConsumerMessage(ClientConnection& client, uint32_t message_type, const std::vector<char>& data);
    
    // 处理数据源注册
    void HandleDataSourceRegistration(const std::string& producer_id, const std::string& source_name);
    
    // 向所有消费者广播追踪数据
    void BroadcastTraceData(const std::vector<char>& data, bool has_more);
    
    // 向特定客户端发送消息
    bool SendMessage(SocketConnection& connection, uint32_t message_type, const void* data, size_t data_size);

    SocketConnection server_connection_;
    std::thread accept_thread_;
    std::atomic<bool> running_{true};
    
    // 客户端连接
    std::mutex clients_mutex_;
    std::vector<ClientConnection> clients_;
    
    // 注册的数据源
    std::mutex sources_mutex_;
    std::vector<RegisteredDataSource> registered_sources_;
    
    // 追踪会话状态
    std::atomic<bool> tracing_active_{false};
    TraceSessionConfig active_config_;
    
    // 共享内存池，用于生产者和消费者之间共享数据
    SharedMemoryPool memory_pool_;
    
    // 追踪缓冲区
    std::mutex trace_buffer_mutex_;
    std::vector<char> trace_buffer_;
    std::condition_variable trace_data_cv_;
};