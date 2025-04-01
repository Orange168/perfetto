CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread -I./include
LDFLAGS = -lrt -pthread

# 源文件
SRCS = main.cpp \
       src/ipc/socket_connection.cpp \
       src/memory/shared_memory.cpp \
       src/memory/shared_memory_pool.cpp \
       src/producer/producer_impl.cpp \
       src/service/service_impl.cpp \
       src/consumer/consumer_impl.cpp

# 目标文件
OBJS = $(SRCS:.cpp=.o)
TARGET = perfetto_demo

# 创建必要的目录
DIRS = src/ipc src/memory src/producer src/service src/consumer
$(shell mkdir -p $(DIRS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean