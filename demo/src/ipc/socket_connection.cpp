#include "ipc/socket_connection.h"
#include <cstring>
#include <iostream>

#ifdef _WIN32
// Windows implementation using named pipes
#include <windows.h>
#include <string>

bool SocketConnection::Connect(const std::string& socket_name) {
    std::string pipeName = "\\\\.\\pipe\\" + socket_name;
    fd_ = (int)CreateFileA(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (fd_ == (int)INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to connect to named pipe: " << pipeName << std::endl;
        fd_ = -1;
        return false;
    }
    
    return true;
}

bool SocketConnection::Listen(const std::string& socket_name) {
    std::string pipeName = "\\\\.\\pipe\\" + socket_name;
    fd_ = (int)CreateNamedPipeA(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        4096,
        4096,
        0,
        NULL
    );
    
    if (fd_ == (int)INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create named pipe: " << pipeName << std::endl;
        fd_ = -1;
        return false;
    }
    
    return true;
}

int SocketConnection::Accept() {
    if (fd_ == -1) {
        return -1;
    }
    
    if (ConnectNamedPipe((HANDLE)fd_, NULL) == 0) {
        std::cerr << "Failed to connect client to named pipe" << std::endl;
        return -1;
    }
    
    return fd_; // Return the same pipe handle for Windows
}

bool SocketConnection::Send(const void* data, size_t len) {
    if (fd_ == -1) {
        return false;
    }
    
    DWORD bytes_written;
    return WriteFile((HANDLE)fd_, data, (DWORD)len, &bytes_written, NULL) && bytes_written == len;
}

bool SocketConnection::Recv(void* data, size_t len) {
    if (fd_ == -1) {
        return false;
    }
    
    DWORD bytes_read;
    return ReadFile((HANDLE)fd_, data, (DWORD)len, &bytes_read, NULL) && bytes_read == len;
}

SocketConnection::~SocketConnection() {
    if (fd_ != -1) {
        CloseHandle((HANDLE)fd_);
        fd_ = -1;
    }
}

#else
// Unix implementation using sockets

bool SocketConnection::Connect(const std::string& socket_name) {
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_name.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to " << socket_name << std::endl;
        close(fd_);
        fd_ = -1;
        return false;
    }
    
    return true;
}

bool SocketConnection::Listen(const std::string& socket_name) {
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    // Remove any existing socket file
    unlink(socket_name.c_str());
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_name.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to " << socket_name << std::endl;
        close(fd_);
        fd_ = -1;
        return false;
    }
    
    if (listen(fd_, 5) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(fd_);
        fd_ = -1;
        return false;
    }
    
    return true;
}

int SocketConnection::Accept() {
    if (fd_ < 0) {
        return -1;
    }
    
    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);
    return accept(fd_, (struct sockaddr*)&addr, &addr_len);
}

bool SocketConnection::Send(const void* data, size_t len) {
    if (fd_ < 0) {
        return false;
    }
    
    return send(fd_, data, len, 0) == static_cast<ssize_t>(len);
}

bool SocketConnection::Recv(void* data, size_t len) {
    if (fd_ < 0) {
        return false;
    }
    
    return recv(fd_, data, len, 0) == static_cast<ssize_t>(len);
}

SocketConnection::~SocketConnection() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}
#endif 