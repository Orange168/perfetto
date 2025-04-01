#pragma once
#include <string>
#include <memory>
#include <functional>
#include <vector>

// 数据源描述符
struct DataSourceDescriptor {
    std::string name;
    std::string config;
};

// 数据源事件类型
enum class DataSourceEvent {
    SETUP,
    START,
    STOP,
    FLUSH
};

// 追踪包装器类，提供写入追踪数据的接口
class TraceWriter {
public:
    virtual ~TraceWriter() = default;
    
    // 写入追踪事件
    virtual bool WriteTraceEvent(const void* data, size_t size) = 0;
    
    // 获取当前缓冲区可用空间
    virtual size_t GetAvailableSpace() const = 0;
    
    // 刷新缓冲区
    virtual bool Flush() = 0;
};

// 数据源基类
class DataSource {
public:
    virtual ~DataSource() = default;
    
    // 初始化数据源配置
    virtual void OnSetup(const DataSourceDescriptor& config) = 0;
    
    // 开始数据收集
    virtual void OnStart() = 0;
    
    // 停止数据收集
    virtual void OnStop() = 0;
    
    // 刷新缓冲区（确保数据已写入）
    virtual void OnFlush(bool* success = nullptr) {
        if (success) *success = true;
    }
    
    // 设置追踪写入器
    void SetTraceWriter(std::shared_ptr<TraceWriter> writer) {
        trace_writer_ = writer;
    }
    
    // 获取追踪写入器
    std::shared_ptr<TraceWriter> GetTraceWriter() const {
        return trace_writer_;
    }

protected:
    // 追踪写入器，由生产者设置
    std::shared_ptr<TraceWriter> trace_writer_;
};

// 数据源工厂
using DataSourceFactory = std::function<std::unique_ptr<DataSource>()>; 