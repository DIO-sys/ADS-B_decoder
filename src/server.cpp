#include "server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>

TcpServer::~TcpServer() { stop(); }

bool TcpServer::listen(uint16_t port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        perror("[TcpServer] socket");
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[TcpServer] bind");
        close(listen_fd_);
        return false;
    }

    if (::listen(listen_fd_, 8) < 0) {
        perror("[TcpServer] listen");
        close(listen_fd_);
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&TcpServer::acceptLoop, this);

    std::printf("[TcpServer] Listening on port %d\n", port);
    return true;
}

void TcpServer::acceptLoop() {
    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_fd_, &fds);

        timeval tv{1, 0};
        int ret = select(listen_fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (ret <= 0) continue;

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        std::lock_guard<std::mutex> lock(clients_mu_);
        clients_.push_back(client_fd);
        std::printf("[TcpServer] Client connected from %s (fd=%d, total=%zu)\n",
            inet_ntoa(client_addr.sin_addr), client_fd, clients_.size());
    }
}

void TcpServer::broadcast(const AircraftState& ac, int64_t timestamp_ms) {
    adsb::AircraftRecord record;
    record.set_icao_address(ac.icao_address);
    record.set_callsign(ac.callsign);
    record.set_latitude(ac.latitude);
    record.set_longitude(ac.longitude);
    record.set_altitude_ft(ac.altitude_ft);
    record.set_ground_speed(ac.ground_speed);
    record.set_heading(ac.heading);
    record.set_vertical_rate(ac.vertical_rate);
    record.set_timestamp_ms(timestamp_ms);
    record.set_signal_power(ac.signal_power);
    record.set_crc_valid(true);

    std::string serialized;
    record.SerializeToString(&serialized);

    // Length-prefix framing: 4 bytes big-endian length + payload
    uint32_t len = htonl(serialized.size());

    std::lock_guard<std::mutex> lock(clients_mu_);

    // Iterate in reverse so we can safely remove disconnected clients
    for (int i = clients_.size() - 1; i >= 0; i--) {
        int fd = clients_[i];
        bool ok = true;

        // Send length prefix
        size_t sent = 0;
        while (sent < 4 && ok) {
            ssize_t n = send(fd, reinterpret_cast<const uint8_t*>(&len) + sent, 4 - sent, MSG_NOSIGNAL);
            if (n <= 0) { ok = false; break; }
            sent += n;
        }

        // Send payload
        sent = 0;
        while (sent < serialized.size() && ok) {
            ssize_t n = send(fd, serialized.data() + sent, serialized.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) { ok = false; break; }
            sent += n;
        }

        if (!ok) {
            std::printf("[TcpServer] Client disconnected (fd=%d)\n", fd);
            close(fd);
            clients_.erase(clients_.begin() + i);
        }
    }
}

void TcpServer::stop() {
    running_ = false;
    if (accept_thread_.joinable()) accept_thread_.join();

    std::lock_guard<std::mutex> lock(clients_mu_);
    for (int fd : clients_) close(fd);
    clients_.clear();

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    std::printf("[TcpServer] Stopped\n");
}

int TcpServer::clientCount() const {
    std::lock_guard<std::mutex> lock(clients_mu_);
    return clients_.size();
}