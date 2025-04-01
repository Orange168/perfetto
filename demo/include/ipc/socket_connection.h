#pragma once
#include <string>

// Platform-specific includes
#ifdef _WIN32
// Windows
#include <windows.h>
#else
// Unix
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

class SocketConnection {
public:
    #ifdef _WIN32
    static constexpr const char* kServiceSocket = "perfetto_service";
    #else
    static constexpr const char* kServiceSocket = "/tmp/perfetto_service";
    #endif
    
    bool Connect(const std::string& socket_name);
    bool Listen(const std::string& socket_name);
    int Accept();
    bool Send(const void* data, size_t len);
    bool Recv(void* data, size_t len);
    ~SocketConnection();

private:
    int fd_ = -1;
}; 