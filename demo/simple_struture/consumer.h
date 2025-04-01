#pragma once
#include <string>
#include <vector>

class Consumer {
public:
    void ReadData(const std::vector<char>& buffer) {
        // 从服务层读取数据
        data_.assign(buffer.begin(), buffer.end());
    }

    std::string GetData() const {
        return std::string(data_.begin(), data_.end());
    }

private:
    std::vector<char> data_;
}; 