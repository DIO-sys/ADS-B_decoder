#pragma once
#include "aircraft.h"
#include "adsb.pb.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdint>

class TcpServer {
public:
    ~TcpServer();

    bool listen(uint16_t port);
    void broadcast(const AircraftState& ac, int64_t timestamp_ms);
    void stop();
    int clientCount() const;

private:
    int listen_fd_ = -1;
    std::vector<int> clients_;
    mutable std::mutex clients_mu_;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    void acceptLoop();
    void sendToClient(int fd, const uint8_t* data, size_t len);
    void removeClient(int fd);
};