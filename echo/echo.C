#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <exception>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>

//Server which prints client message to stdout.

void usage() {
    std::cout << "Usage: net MODE PORT\n";
    std::cout << "MODE is u, t, U, or T." << std::endl;
}

class Fd {
    public:
        Fd() = default;
        explicit Fd(int fd) : _fd(fd) {}
        ~Fd() { reset(); }

        Fd(const Fd&) = delete;
        Fd& operator=(const Fd&) = delete;

        Fd(Fd&& o) noexcept : _fd(o._fd) { o._fd = -1; }
        Fd& operator=(Fd&& o) noexcept {
            if(this != &o) {
                reset();
                std::swap(_fd, o._fd);
            }
            return *this;
        }

        explicit operator bool() const { return _fd >= 0; }

        int get() const { return _fd; }

        void reset() {
            if(*this) {
                ::close(_fd);
                _fd = -1;
            }
        }
    private:
        int _fd = -1;
};

void die(const char* fn) {
    std::string msg = std::string(fn) + " failed: " + strerror(errno);
    throw std::runtime_error(msg.c_str());
}

int udp_server(const char* port) {
    struct addrinfo *res = nullptr;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    std::cout << "Starting UDP server on port: " << port << " ... "<< std::endl;

    getaddrinfo("localhost", port, &hints, &res);

    auto sock = Fd(::socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if(!sock) die("socket()");

    int reuse = 1;
    ::setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

    auto rc = ::bind(sock.get(), res->ai_addr, res->ai_addrlen);
    if(rc < 0) die("bind()");

    struct sockaddr_storage local_addr_store;
    auto* local_addr = (sockaddr*)&local_addr_store;

    for(;;) {
        socklen_t localaddr_len = sizeof(local_addr_store);
        char buf[4096];
        rc = ::recvfrom(sock.get(), buf, sizeof(buf), 0, local_addr, &localaddr_len);
        if(rc < 0) die("recvfrom()");

        buf[rc] = '\0';
        std::cout << "Received Msg: `" << buf << "'" << std::endl;
    }

    return 0;
}



int tcp_server(const char* port) {
    struct addrinfo *res = nullptr;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    std::cout << "Starting TCP server on port: " << port << " ... "<< std::endl;

    getaddrinfo("localhost", port, &hints, &res);

    auto listen_sock = Fd(::socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if(!listen_sock) die("socket()");

    int reuse = 1;
    ::setsockopt(listen_sock.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

    auto rc = ::bind(listen_sock.get(), res->ai_addr, res->ai_addrlen);
    if(rc < 0) die("bind()");

    rc = ::listen(listen_sock.get(), 20);
    if(rc < 0) die("listen()");

    std::vector<Fd> sockets;
    struct sockaddr_storage local_addr_store;
    auto* local_addr = (sockaddr*)&local_addr_store;

    for(;;) {
        socklen_t localaddr_len = sizeof(local_addr_store);

        int max_sock = -1;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_sock.get(), &fds);
        max_sock = std::max(listen_sock.get(), max_sock);
        for(auto& s: sockets) {
            FD_SET(s.get(), &fds);
            max_sock = std::max(s.get(), max_sock);
        }
        rc = select(max_sock + 1, &fds, nullptr, nullptr, nullptr);
        if(rc == -1) {
            if(errno == EINTR) {
                continue;
            }
            die("select()");
        }
        if(FD_ISSET(listen_sock.get(), &fds)) {
            auto newsock = Fd(::accept(listen_sock.get(), local_addr, &localaddr_len));
            if(!newsock) die("accept()");

            std::cout << "Received new connection from client! " << std::endl;

            sockets.push_back(std::move(newsock));
        }
        for(auto iter = sockets.begin(); iter != sockets.end();) {
            auto& s = *iter;
            if(FD_ISSET(s.get(), &fds)) {
                char buf[4096];
                rc = ::recv(s.get(), buf, sizeof(buf), 0);
                if(rc < 0) die("recv()");
                if(rc == 0) {
                    iter = sockets.erase(iter);
                    std::cout << "Closing client connection..." << std::endl;
                    continue;
                }
                buf[rc] = '\0';
                std::cout << "Received Msg: `" << buf << "'" << std::endl;
            }
            ++iter;
            continue;
        }
    }

    return 0;
}

template <bool is_tcp>
int client(const char* port) {
    struct addrinfo *res = nullptr;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = is_tcp ? SOCK_STREAM : SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;


    getaddrinfo("localhost", port, &hints, &res);

    auto sock = Fd(::socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if(!sock) die("socket()");


    int rc = 0;
    if(is_tcp) {
        std::cout << "Connecting TCP client to port: " << port << " ... "<< std::endl;

        rc = connect(sock.get(), res->ai_addr, res->ai_addrlen);
        if(rc != 0) die("connect()");
    }

    std::string line;
    while(true) {
        std::getline(std::cin, line);

        if(std::cin.eof()) {
            break;
        }

        std::cout << "Sending: `" << line << "' ..." << std::endl;

        if(is_tcp) {
            rc = ::send(sock.get(), line.c_str(), line.size(), 0);
            if(rc < 0) die("send()");
        } else {
            rc = ::sendto(sock.get(), line.c_str(), line.size(), 0, res->ai_addr, res->ai_addrlen);
            if(rc < 0) die("sendto()");
        }
    }
    return 0;
}

int udp_client(const char* port) {
    return client<false>(port);
}

int tcp_client(const char* port) {
    return client<true>(port);
}

int main(int argc, char** argv) {
    if(argc < 3) {
        usage();
        return -1;
    }
    auto mode = argv[1][0];
    auto port = argv[2];
    try {
        switch(mode) {
            case 'U':
                return udp_server(port);
            case 'T':
                return tcp_server(port);
            case 'u':
                return udp_client(port);
            case 't':
                return tcp_client(port);
            default:
                break;
        }
    } catch(std::exception& e) {
        std::cout << "Caught Exception: " << e.what() << std::endl;
        return -1;
    }
    usage();
    return -1;
}
