#pragma once
#include <thread>
#include <map>
#include <functional>
#include <memory>
#include <atomic>
#include <queue>
#include <condition_variable>
#include "core/data_source.h"
#include "ipc/socket_connection.h"
#include "memory/shared_memory_pool.h"

// 从服务到生产者的命令类型
enum class ProducerCmd {
    START_TRACING,
    STOP_TRACING,
    FLUSH,
    SHUTDOWN
};

// 命令结构
struct ProducerCommand {
    ProducerCmd cmd;
    std::string target_name;  // 目标数据源名称
    std::string args;         // 命令参数
};

// 生产者实现类
class ProducerImpl {
public:
    ProducerImpl();
    ~ProducerImpl();

    // 注册数据源
    void RegisterDataSource(const std::string& name, 
                          std::function<std::unique_ptr<DataSource>()> factory);
    
    // 开始写入跟踪数据
    void StartTracing(const std::string& config);
    
    // 停止写入跟踪数据
    void StopTracing();
    
    // 刷新缓冲区
    void Flush();
    
    // 模拟数据写入 - 真实场景下数据源会直接写入
    void WriteTraceData(const std::string& source_name, const void* data, size_t data_size);

private:
    // 连接到服务
    bool Connect();
    
    // 命令处理线程
    void CommandThread();
    
    // 处理来自服务的命令
    void HandleCommand(const ProducerCommand& cmd);
    
    // 发送消息到服务
    bool SendMessage(const void* data, size_t size);
    
    // 注册所有数据源到服务
    void RegisterDataSourcesWithService();
    
    // 处理数据源回调
    void HandleDataSourceCallback(const std::string& name, DataSourceEvent event);

    SocketConnection connection_;
    std::thread command_thread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{true};
    std::atomic<bool> tracing_{false};
    
    SharedMemoryPool memory_pool_;
    
    std::map<std::string, std::function<std::unique_ptr<DataSource>()>> factories_;
    std::map<std::string, std::unique_ptr<DataSource>> active_sources_;
    
    // 命令队列和相关同步原语
    std::queue<ProducerCommand> command_queue_;
    std::mutex command_mutex_;
    std::condition_variable command_cv_;
}; 