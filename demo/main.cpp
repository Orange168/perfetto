#include "producer/producer_impl.h"
#include "service/service_impl.h"
#include "consumer/consumer_impl.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>

// 示例数据源，继承自DataSource基类
class CustomDataSource : public DataSource {
public:
    // 设置数据源
    void OnSetup(const DataSourceDescriptor& config) override {
        std::cout << "Setting up data source: " << config.name << std::endl;
        if (!config.config.empty()) {
            std::cout << "With config: " << config.config << std::endl;
        }
    }

    // 开始数据收集
    void OnStart() override {
        std::cout << "Starting data source" << std::endl;
        running_ = true;
        worker_thread_ = std::thread(&CustomDataSource::WorkerThread, this);
    }

    // 停止数据收集
    void OnStop() override {
        std::cout << "Stopping data source" << std::endl;
        running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    // 定制方法，生成随机数据
    void GenerateData() {
        // 生成示例数据
        const char* data = "Example trace data from CustomDataSource";
        
        // 如果有追踪写入器，写入数据
        if (auto writer = GetTraceWriter()) {
            writer->WriteTraceEvent(data, strlen(data) + 1);
        }
    }

private:
    // 工作线程，定期生成数据
    void WorkerThread() {
        while (running_) {
            // 生成一些追踪数据
            GenerateData();
            
            // 每500毫秒生成一次数据
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
};

// 追踪数据回调处理函数
void HandleTraceData(const std::vector<char>& data, bool has_more) {
    if (!data.empty()) {
        std::cout << "Received trace data (" << data.size() << " bytes): ";
        std::cout << data.data() << std::endl;
    }
    
    if (!has_more) {
        std::cout << "Trace session complete" << std::endl;
    }
}

int main() {
    try {
        // 启动服务（在真实应用中，这将是一个单独的进程）
        ServiceImpl service;
        std::cout << "Service started" << std::endl;
        
        // 启动生产者
        ProducerImpl producer;
        
        // 注册数据源
        producer.RegisterDataSource("custom_source", []() {
            return std::make_unique<CustomDataSource>();
        });
        std::cout << "Producer registered data source" << std::endl;
        
        // 给服务一些时间处理注册
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 启动消费者
        ConsumerImpl consumer;
        
        // 注册追踪数据回调
        consumer.RegisterDataCallback(HandleTraceData);
        
        // 查询可用数据源
        auto sources = consumer.QueryAvailableDataSources();
        std::cout << "Available data sources:" << std::endl;
        for (const auto& source : sources) {
            std::cout << " - " << source << std::endl;
        }
        
        // 创建跟踪配置
        TraceConfig config;
        config.name = "demo_trace";
        config.buffer_size = 1024 * 1024;  // 1MB 缓冲区
        config.duration_ms = 5000;        // 5秒会话
        config.data_sources.push_back("custom_source");
        
        // 启用追踪
        std::cout << "Enabling tracing..." << std::endl;
        consumer.EnableTracing(config);
        
        // 等待追踪会话完成
        std::cout << "Waiting for tracing session to complete..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(6));
        
        // 读取追踪数据
        std::vector<char> trace_data;
        if (consumer.ReadTrace(trace_data)) {
            std::cout << "Read " << trace_data.size() << " bytes of trace data" << std::endl;
        } else {
            std::cout << "No trace data available" << std::endl;
        }
        
        // 禁用追踪（如果仍在进行）
        if (consumer.IsTracingActive()) {
            consumer.DisableTracing();
        }
        
        std::cout << "Demo completed successfully" << std::endl;
    } 
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } 
    catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
    
    return 0;
} 