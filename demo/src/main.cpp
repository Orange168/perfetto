#include "producer/producer_impl.h"
#include "service/service_impl.h"
#include "consumer/consumer_impl.h"
#include <iostream>
#include <chrono>
#include <thread>

class CustomDataSource : public DataSource {
public:
    void OnSetup(const DataSourceDescriptor& config) override {
        std::cout << "Setting up data source: " << config.name << std::endl;
    }

    void OnStart() override {
        std::cout << "Starting data source" << std::endl;
    }

    void OnStop() override {
        std::cout << "Stopping data source" << std::endl;
    }
};

int main() {
    try {
        ServiceImpl service;
        std::cout << "Service started" << std::endl;

        ProducerImpl producer;
        producer.RegisterDataSource("custom_source", []() {
            return std::make_unique<CustomDataSource>();
        });
        std::cout << "Producer registered data source" << std::endl;

        ConsumerImpl consumer;
        consumer.EnableTracing("trace_config");
        std::cout << "Consumer enabled tracing" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::vector<char> trace_data;
        consumer.ReadTrace(trace_data);
        std::cout << "Trace data read: " << trace_data.size() << " bytes" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 