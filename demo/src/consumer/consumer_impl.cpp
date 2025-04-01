#include "consumer/consumer_impl.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <cstring>

// 用于与服务通信的消息头
struct MessageHeader {
    uint32_t message_type;
    uint32_t payload_size;
};

// 消息类型
enum MessageType {
    ENABLE_TRACING = 1,
    DISABLE_TRACING = 2,
    QUERY_DATA_SOURCES = 3,
    TRACE_DATA = 4,
    TRACE_COMPLETE = 5
};

// TraceConfig序列化实现
std::string TraceConfig::Serialize() const {
    std::stringstream ss;
    ss << name << "\n";
    ss << buffer_size << "\n";
    ss << duration_ms << "\n";
    ss << data_sources.size() << "\n";
    for (const auto& ds : data_sources) {
        ss << ds << "\n";
    }
    return ss.str();
}

// TraceConfig反序列化实现
TraceConfig TraceConfig::Deserialize(const std::string& data) {
    TraceConfig config;
    std::stringstream ss(data);
    std::string line;
    
    // 读取会话名称
    std::getline(ss, config.name);
    
    // 读取缓冲区大小
    std::getline(ss, line);
    config.buffer_size = std::stoull(line);
    
    // 读取持续时间
    std::getline(ss, line);
    config.duration_ms = std::stoul(line);
    
    // 读取数据源数量
    std::getline(ss, line);
    size_t count = std::stoull(line);
    
    // 读取数据源列表
    for (size_t i = 0; i < count; ++i) {
        std::getline(ss, line);
        config.data_sources.push_back(line);
    }
    
    return config;
}

ConsumerImpl::ConsumerImpl() {
    // 尝试连接到服务
    if (Connect()) {
        std::cout << "Consumer connected to service" << std::endl;
        connected_ = true;
    } else {
        std::cerr << "Consumer failed to connect to service, will retry" << std::endl;
    }
    
    // 启动响应处理线程
    response_thread_ = std::thread(&ConsumerImpl::ResponseThread, this);
}

ConsumerImpl::~ConsumerImpl() {
    // 如果还在追踪，停止追踪
    if (tracing_enabled_) {
        DisableTracing();
    }
    
    // 设置运行标志为false
    running_ = false;
    
    // 等待线程结束
    if (response_thread_.joinable()) {
        response_thread_.join();
    }
    
    std::cout << "Consumer shutting down" << std::endl;
}

bool ConsumerImpl::Connect() {
    return connection_.Connect(SocketConnection::kServiceSocket);
}

bool ConsumerImpl::EnableTracing(const TraceConfig& config) {
    if (!connected_) {
        std::cerr << "Cannot enable tracing: not connected to service" << std::endl;
        return false;
    }
    
    if (tracing_enabled_) {
        std::cout << "Tracing already in progress, stopping first" << std::endl;
        DisableTracing();
    }
    
    std::cout << "Enabling tracing with config: " << config.name << std::endl;
    
    // 保存当前配置
    active_config_ = config;
    
    // 序列化配置
    std::string config_str = config.Serialize();
    
    // 创建消息头
    MessageHeader header;
    header.message_type = ENABLE_TRACING;
    header.payload_size = config_str.size();
    
    // 发送消息头
    if (!SendMessage(&header, sizeof(header))) {
        std::cerr << "Failed to send enable tracing header" << std::endl;
        return false;
    }
    
    // 发送配置
    if (!SendMessage(config_str.c_str(), config_str.size())) {
        std::cerr << "Failed to send trace config" << std::endl;
        return false;
    }
    
    // 等待追踪启动确认（这里简化处理，实际中应该等待服务的响应）
    tracing_enabled_ = true;
    
    // 清空现有追踪数据
    {
        std::lock_guard<std::mutex> lock(trace_mutex_);
        trace_buffer_.clear();
    }
    
    std::cout << "Tracing enabled" << std::endl;
    return true;
}

bool ConsumerImpl::DisableTracing() {
    if (!connected_) {
        std::cerr << "Cannot disable tracing: not connected to service" << std::endl;
        return false;
    }
    
    if (!tracing_enabled_) {
        std::cout << "No active tracing session" << std::endl;
        return false;
    }
    
    std::cout << "Disabling tracing" << std::endl;
    
    // 创建消息头
    MessageHeader header;
    header.message_type = DISABLE_TRACING;
    header.payload_size = 0;
    
    // 发送消息头
    if (!SendMessage(&header, sizeof(header))) {
        std::cerr << "Failed to send disable tracing header" << std::endl;
        return false;
    }
    
    // 等待追踪停止确认（这里简化处理）
    tracing_enabled_ = false;
    
    std::cout << "Tracing disabled" << std::endl;
    return true;
}

bool ConsumerImpl::ReadTrace(std::vector<char>& buffer) {
    if (!connected_) {
        std::cerr << "Cannot read trace: not connected to service" << std::endl;
        return false;
    }
    
    std::unique_lock<std::mutex> lock(trace_mutex_);
    
    // 如果没有数据且追踪仍在进行，等待数据到达
    if (trace_buffer_.empty() && tracing_enabled_) {
        std::cout << "Waiting for trace data..." << std::endl;
        trace_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
            return !trace_buffer_.empty() || !tracing_enabled_;
        });
    }
    
    // 复制数据到输出缓冲区
    buffer = trace_buffer_;
    
    std::cout << "Read " << buffer.size() << " bytes of trace data" << std::endl;
    return !buffer.empty();
}

void ConsumerImpl::RegisterDataCallback(TraceDataCallback callback) {
    data_callback_ = callback;
}

std::vector<std::string> ConsumerImpl::QueryAvailableDataSources() {
    std::vector<std::string> result;
    
    if (!connected_) {
        std::cerr << "Cannot query data sources: not connected to service" << std::endl;
        return result;
    }
    
    std::cout << "Querying available data sources" << std::endl;
    
    // 创建消息头
    MessageHeader header;
    header.message_type = QUERY_DATA_SOURCES;
    header.payload_size = 0;
    
    // 发送消息头
    if (!SendMessage(&header, sizeof(header))) {
        std::cerr << "Failed to send query data sources header" << std::endl;
        return result;
    }
    
    // 在实际实现中，这里应该等待服务的响应
    // 这里我们模拟一些默认的数据源
    result.push_back("cpu_stats");
    result.push_back("memory_stats");
    result.push_back("process_stats");
    result.push_back("network_stats");
    
    return result;
}

void ConsumerImpl::ResponseThread() {
    while (running_) {
        // 如果未连接，尝试重新连接
        if (!connected_) {
            if (Connect()) {
                connected_ = true;
                std::cout << "Consumer reconnected to service" << std::endl;
            } else {
                // 等待一段时间后重试
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }
        
        // 接收消息
        MessageHeader header;
        if (!connection_.Recv(&header, sizeof(header))) {
            // 连接断开
            connected_ = false;
            std::cerr << "Lost connection to service" << std::endl;
            continue;
        }
        
        // 处理消息
        switch (header.message_type) {
            case TRACE_DATA: {
                // 接收追踪数据
                std::vector<char> data(header.payload_size);
                if (!connection_.Recv(data.data(), header.payload_size)) {
                    connected_ = false;
                    std::cerr << "Failed to receive trace data" << std::endl;
                    continue;
                }
                
                // 处理追踪数据
                HandleTraceData(data, true);
                break;
            }
                
            case TRACE_COMPLETE: {
                // 追踪会话完成
                std::cout << "Trace session complete" << std::endl;
                
                // 处理最后的数据
                if (header.payload_size > 0) {
                    std::vector<char> data(header.payload_size);
                    if (!connection_.Recv(data.data(), header.payload_size)) {
                        connected_ = false;
                        std::cerr << "Failed to receive final trace data" << std::endl;
                        continue;
                    }
                    
                    // 处理最后的追踪数据
                    HandleTraceData(data, false);
                } else {
                    // 没有更多数据，但会话已完成
                    if (data_callback_) {
                        data_callback_(std::vector<char>(), false);
                    }
                }
                
                // 更新追踪状态
                tracing_enabled_ = false;
                break;
            }
                
            default:
                std::cerr << "Received unknown message type: " << header.message_type << std::endl;
                break;
        }
    }
}

void ConsumerImpl::HandleTraceData(const std::vector<char>& data, bool has_more) {
    // 追加数据到缓冲区
    {
        std::lock_guard<std::mutex> lock(trace_mutex_);
        trace_buffer_.insert(trace_buffer_.end(), data.begin(), data.end());
        
        // 通知等待的线程
        trace_cv_.notify_all();
    }
    
    // 如果注册了回调，触发回调
    if (data_callback_) {
        data_callback_(data, has_more);
    }
    
    std::cout << "Received " << data.size() << " bytes of trace data" << std::endl;
}

bool ConsumerImpl::SendMessage(const void* data, size_t size) {
    if (!connected_) {
        std::cerr << "Cannot send message: not connected to service" << std::endl;
        return false;
    }
    
    return connection_.Send(data, size);
} 