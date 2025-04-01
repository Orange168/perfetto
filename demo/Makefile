CXX = g++
CXXFLAGS = -std=c++11 -Wall
LDFLAGS = -lrt

SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = perfetto_demo

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) 