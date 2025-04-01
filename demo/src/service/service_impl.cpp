#include "service/service_impl.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>
#include <sstream>
#include <cstring>

// 消息类型常量
enum MessageType {
    REGISTER_DATA_SOURCE = 1,
    ENABLE_TRACING = 2,
    DISABLE_TRACING = 3,
    QUERY_DATA_SOURCES = 4,
    TRACE_DATA = 5,
    TRACE_COMPLETE = 6
};

// 消息头结构
struct MessageHeader {
    uint32_t message_type;
    uint32_t payload_size;
};

ServiceImpl::ServiceImpl() 
    : memory_pool_(10*1024*1024, 4096) {  // 10MB的共享内存，4KB块大小
    StartAcceptThread();
    std::cout << "Service started" << std::endl;
}

ServiceImpl::~ServiceImpl() {
    // 停止任何活动的追踪会话
    if (tracing_active_) {
        StopTracing();
    }
    
    // 停止接受新连接
    running_ = false;
    
    // 等待接受线程结束
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            // 等待客户端线程结束
            if (client.thread.joinable()) {
                client.thread.detach();  // 分离线程以避免阻塞
            }
        }
        clients_.clear();
    }
    
    std::cout << "Service shut down" << std::endl;
}

bool ServiceImpl::StartTracing(const TraceSessionConfig& config) {
    if (tracing_active_) {
        std::cerr << "Tracing already active, stop it first" << std::endl;
        return false;
    }
    
    std::cout << "Starting tracing session: " << config.name << std::endl;
    
    // 保存配置
    active_config_ = config;
    
    // 清空追踪缓冲区
    {
        std::lock_guard<std::mutex> lock(trace_buffer_mutex_);
        trace_buffer_.clear();
    }
    
    // 检查请求的数据源是否已注册
    {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        for (const auto& source_name : config.data_sources) {
            bool found = false;
            for (const auto& source : registered_sources_) {
                if (source.name == source_name) {
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                std::cerr << "Data source not registered: " << source_name << std::endl;
                // 在真实实现中，我们可能会跳过未注册的数据源，而不是完全失败
            }
        }
    }
    
    // 准备发送给生产者的命令
    MessageHeader header;
    header.message_type = ENABLE_TRACING;
    
    // 序列化配置
    std::stringstream ss;
    ss << config.name << "\n";
    ss << config.buffer_size << "\n";
    ss << config.duration_ms << "\n";
    
    std::string config_str = ss.str();
    header.payload_size = config_str.size();
    
    // 向所有生产者发送启动命令
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            if (client.type == ClientType::PRODUCER) {
                // 检查此生产者是否有我们需要的数据源
                bool has_needed_source = false;
                {
                    std::lock_guard<std::mutex> sources_lock(sources_mutex_);
                    for (const auto& source : registered_sources_) {
                        if (source.producer_id == client.id) {
                            // 检查此数据源是否在配置中请求
                            for (const auto& requested : config.data_sources) {
                                if (source.name == requested) {
                                    has_needed_source = true;
                                    break;
                                }
                            }
                            if (has_needed_source) break;
                        }
                    }
                }
                
                // 如果此生产者有需要的数据源，发送启动命令
                if (has_needed_source) {
                    if (!SendMessage(client.connection, ENABLE_TRACING, config_str.c_str(), config_str.size())) {
                        std::cerr << "Failed to send start tracing command to producer " << client.id << std::endl;
                    }
                }
            }
        }
    }
    
    // 设置追踪会话为活动状态
    tracing_active_ = true;
    
    // 如果配置了持续时间，启动计时器线程
    if (config.duration_ms > 0) {
        std::thread([this, duration = config.duration_ms]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(duration));
            if (tracing_active_) {
                StopTracing();
            }
        }).detach();
    }
    
    std::cout << "Tracing session started" << std::endl;
    return true;
}

bool ServiceImpl::StopTracing() {
    if (!tracing_active_) {
        std::cerr << "No active tracing session" << std::endl;
        return false;
    }
    
    std::cout << "Stopping tracing session" << std::endl;
    
    // 向所有生产者发送停止命令
    MessageHeader header;
    header.message_type = DISABLE_TRACING;
    header.payload_size = 0;
    
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            if (client.type == ClientType::PRODUCER) {
                if (!SendMessage(client.connection, DISABLE_TRACING, nullptr, 0)) {
                    std::cerr << "Failed to send stop tracing command to producer " << client.id << std::endl;
                }
            }
        }
    }
    
    // 设置完成标志
    tracing_active_ = false;
    
    // 通知所有消费者追踪会话完成
    {
        std::lock_guard<std::mutex> lock(trace_buffer_mutex_);
        BroadcastTraceData(trace_buffer_, false);
        trace_buffer_.clear();
    }
    
    std::cout << "Tracing session stopped" << std::endl;
    return true;
}

std::vector<RegisteredDataSource> ServiceImpl::GetRegisteredDataSources() const {
    std::lock_guard<std::mutex> lock(sources_mutex_);
    return registered_sources_;
}

void ServiceImpl::StartAcceptThread() {
    // 开始监听客户端连接
    if (!server_connection_.Listen(SocketConnection::kServiceSocket)) {
        std::cerr << "Failed to listen on socket" << std::endl;
        return;
    }
    
    std::cout << "Listening for client connections" << std::endl;
    
    // 启动接受线程
    accept_thread_ = std::thread([this]() {
        while (running_) {
            // 接受新连接
            int client_fd = server_connection_.Accept();
            if (client_fd >= 0) {
                // 创建新的客户端连接
                ClientConnection client;
                
                // 生成唯一ID
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(10000, 99999);
                client.id = "client_" + std::to_string(dis(gen));
                
                std::cout << "Accepted client connection: " << client.id << std::endl;
                
                // 启动客户端处理线程
                client.thread = std::thread(&ServiceImpl::HandleClientConnection, this, std::ref(client));
                
                // 添加到客户端列表
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_.push_back(std::move(client));
                }
            }
        }
    });
}

void ServiceImpl::HandleClientConnection(ClientConnection& client) {
    // 处理来自客户端的消息
    while (running_) {
        // 接收消息头
        MessageHeader header;
        if (!client.connection.Recv(&header, sizeof(header))) {
            // 连接断开
            std::cerr << "Client " << client.id << " disconnected" << std::endl;
            break;
        }
        
        // 接收消息体
        std::vector<char> payload(header.payload_size);
        if (header.payload_size > 0) {
            if (!client.connection.Recv(payload.data(), header.payload_size)) {
                std::cerr << "Failed to receive message payload from client " << client.id << std::endl;
                break;
            }
        }
        
        // 根据第一个消息确定客户端类型
        if (client.type == ClientType::UNKNOWN) {
            if (header.message_type == REGISTER_DATA_SOURCE) {
                client.type = ClientType::PRODUCER;
                std::cout << "Client " << client.id << " identified as producer" << std::endl;
            } else if (header.message_type == ENABLE_TRACING || 
                       header.message_type == QUERY_DATA_SOURCES) {
                client.type = ClientType::CONSUMER;
                std::cout << "Client " << client.id << " identified as consumer" << std::endl;
            }
        }
        
        // 根据客户端类型处理消息
        if (client.type == ClientType::PRODUCER) {
            HandleProducerMessage(client, header.message_type, payload);
        } else if (client.type == ClientType::CONSUMER) {
            HandleConsumerMessage(client, header.message_type, payload);
        } else {
            std::cerr << "Unknown client type" << std::endl;
        }
    }
    
    // 连接断开，从客户端列表中移除
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::find_if(clients_.begin(), clients_.end(), 
                             [&client](const ClientConnection& c) { return c.id == client.id; });
        if (it != clients_.end()) {
            if (it->thread.joinable()) {
                it->thread.detach();
            }
            clients_.erase(it);
        }
    }
    
    // 更新数据源注册表，移除此客户端的数据源
    if (client.type == ClientType::PRODUCER) {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        auto it = std::remove_if(registered_sources_.begin(), registered_sources_.end(),
                              [&client](const RegisteredDataSource& ds) { return ds.producer_id == client.id; });
        registered_sources_.erase(it, registered_sources_.end());
    }
}

void ServiceImpl::HandleProducerMessage(ClientConnection& client, uint32_t message_type, const std::vector<char>& data) {
    switch (message_type) {
        case REGISTER_DATA_SOURCE: {
            // 数据源注册
            if (data.empty()) {
                std::cerr << "Empty data source name" << std::endl;
                return;
            }
            
            std::string source_name(data.data(), data.size() - 1);  // 减1去掉NULL终止符
            HandleDataSourceRegistration(client.id, source_name);
            break;
        }
            
        case TRACE_DATA: {
            // 追踪数据
            if (!tracing_active_) {
                std::cerr << "Received trace data but no active tracing session" << std::endl;
                return;
            }
            
            // 在真实实现中，这里应该接收共享内存引用，而不是实际数据
            // 为了简化，这里我们直接处理数据
            
            // 添加到追踪缓冲区
            {
                std::lock_guard<std::mutex> lock(trace_buffer_mutex_);
                trace_buffer_.insert(trace_buffer_.end(), data.begin(), data.end());
                
                // 通知条件变量
                trace_data_cv_.notify_all();
            }
            
            // 广播给所有消费者
            BroadcastTraceData(data, true);
            break;
        }
            
        default:
            std::cerr << "Unknown producer message type: " << message_type << std::endl;
            break;
    }
}

void ServiceImpl::HandleConsumerMessage(ClientConnection& client, uint32_t message_type, const std::vector<char>& data) {
    switch (message_type) {
        case ENABLE_TRACING: {
            // 启动追踪
            if (data.empty()) {
                std::cerr << "Empty tracing config" << std::endl;
                return;
            }
            
            // 解析配置
            std::string config_str(data.data(), data.size());
            std::stringstream ss(config_str);
            std::string line;
            
            TraceSessionConfig config;
            
            // 读取会话名称
            std::getline(ss, config.name);
            
            // 读取缓冲区大小
            std::getline(ss, line);
            config.buffer_size = std::stoull(line);
            
            // 读取持续时间
            std::getline(ss, line);
            config.duration_ms = std::stoul(line);
            
            // 后续可能还有数据源列表，这里简化处理
            
            // 启动追踪会话
            StartTracing(config);
            break;
        }
            
        case DISABLE_TRACING:
            // 停止追踪
            StopTracing();
            break;
            
        case QUERY_DATA_SOURCES: {
            // 查询可用数据源
            std::vector<RegisteredDataSource> sources = GetRegisteredDataSources();
            
            // 序列化数据源列表
            std::stringstream ss;
            ss << sources.size() << "\n";
            for (const auto& source : sources) {
                ss << source.name << "\n";
            }
            
            std::string sources_str = ss.str();
            
            // 发送结果
            SendMessage(client.connection, QUERY_DATA_SOURCES, sources_str.c_str(), sources_str.size());
            break;
        }
            
        default:
            std::cerr << "Unknown consumer message type: " << message_type << std::endl;
            break;
    }
}

void ServiceImpl::HandleDataSourceRegistration(const std::string& producer_id, const std::string& source_name) {
    std::cout << "Registering data source: " << source_name << " from producer " << producer_id << std::endl;
    
    // 添加到注册表
    {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        
        // 检查是否已存在
        for (const auto& source : registered_sources_) {
            if (source.name == source_name && source.producer_id == producer_id) {
                std::cout << "Data source already registered" << std::endl;
                return;
            }
        }
        
        // 添加新数据源
        RegisteredDataSource source;
        source.name = source_name;
        source.producer_id = producer_id;
        registered_sources_.push_back(source);
    }
    
    std::cout << "Data source registered: " << source_name << std::endl;
}

void ServiceImpl::BroadcastTraceData(const std::vector<char>& data, bool has_more) {
    // 向所有消费者广播追踪数据
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (auto& client : clients_) {
        if (client.type == ClientType::CONSUMER) {
            SendMessage(client.connection, 
                       has_more ? TRACE_DATA : TRACE_COMPLETE, 
                       data.data(), 
                       data.size());
        }
    }
}

bool ServiceImpl::SendMessage(SocketConnection& connection, uint32_t message_type, const void* data, size_t data_size) {
    // 发送消息头
    MessageHeader header;
    header.message_type = message_type;
    header.payload_size = data_size;
    
    if (!connection.Send(&header, sizeof(header))) {
        return false;
    }
    
    // 发送消息体（如果有）
    if (data_size > 0 && data != nullptr) {
        if (!connection.Send(data, data_size)) {
            return false;
        }
    }
    
    return true;
} 