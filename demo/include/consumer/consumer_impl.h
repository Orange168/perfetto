#pragma once
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <map>
#include <functional>
#include "ipc/socket_connection.h"

// 追踪配置
struct TraceConfig {
    std::string name;                   // 跟踪会话名称
    uint64_t buffer_size = 1024*1024;   // 缓冲区大小（字节）
    uint32_t duration_ms = 0;           // 追踪持续时间（毫秒，0表示无限）
    std::vector<std::string> data_sources; // 启用的数据源
    
    // 序列化配置为字符串
    std::string Serialize() const;
    
    // 从字符串反序列化配置
    static TraceConfig Deserialize(const std::string& data);
};

// 追踪数据回调函数类型
using TraceDataCallback = std::function<void(const std::vector<char>&, bool)>;

// 消费者实现类
class ConsumerImpl {
public:
    ConsumerImpl();
    ~ConsumerImpl();

    // 启用追踪，开始收集数据
    bool EnableTracing(const TraceConfig& config);
    
    // 禁用追踪，停止数据收集
    bool DisableTracing();
    
    // 同步读取全部追踪数据
    bool ReadTrace(std::vector<char>& buffer);
    
    // 注册异步数据回调
    void RegisterDataCallback(TraceDataCallback callback);
    
    // 获取连接状态
    bool IsConnected() const { return connected_; }
    
    // 获取追踪状态
    bool IsTracingActive() const { return tracing_enabled_; }
    
    // 获取所有可用的数据源
    std::vector<std::string> QueryAvailableDataSources();

private:
    // 连接到服务
    bool Connect();
    
    // 响应处理线程
    void ResponseThread();
    
    // 处理接收到的追踪数据
    void HandleTraceData(const std::vector<char>& data, bool has_more);
    
    // 发送消息到服务
    bool SendMessage(const void* data, size_t size);
    
    SocketConnection connection_;
    std::thread response_thread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{true};
    std::atomic<bool> tracing_enabled_{false};
    
    std::vector<char> trace_buffer_;
    TraceDataCallback data_callback_;
    
    std::mutex trace_mutex_;
    std::condition_variable trace_cv_;
    
    // 当前活动的追踪配置
    TraceConfig active_config_;
}; 