#include "producer.h"
#include "service.h"
#include "consumer.h"
#include <iostream>

int main() {
    const std::string shm_name = "/perfetto_demo";
    
    // 创建三层实例
    Producer producer(shm_name);
    Service service(shm_name);
    Consumer consumer;

    // 生产者写入数据
    std::string test_data = "Hello Perfetto!";
    producer.WriteData(test_data);

    // 服务层处理数据
    service.ProcessData();

    // 消费者读取数据
    consumer.ReadData(service.GetBuffer());

    // 验证数据
    std::cout << "Read data: " << consumer.GetData() << std::endl;

    return 0;
} 