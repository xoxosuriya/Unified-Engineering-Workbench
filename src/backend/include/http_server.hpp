#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include "embedded_hardware.hpp"

namespace workbench {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::string content_type;
    std::map<std::string, std::string> headers;
};

struct HttpResponse {
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
};

class HttpServer {
public:
    HttpServer(const std::string& host, int port, const std::string& web_root_dir);
    ~HttpServer();

    bool start();
    void stop();
    void wait();

    int get_actual_port() const { return actual_port_; }
    std::string get_base_url() const;
    std::string get_session_token() const { return session_token_; }

    static void set_process_start_time(std::chrono::steady_clock::time_point tp);
    static double get_launch_to_ready_ms();

private:
    void listen_loop();
    void handle_client(uintptr_t client_socket);
    HttpResponse route_request(const HttpRequest& req);

    std::string host_;
    int preferred_port_;
    int actual_port_ = 0;
    std::string web_root_dir_;
    std::string session_token_;

    std::atomic<bool> is_running_{false};
    std::thread server_thread_;
    uintptr_t listen_socket_ = 0;

    std::unique_ptr<EmbeddedHardwareManager> hw_manager_;
    std::mutex hw_mutex_;
    std::atomic<bool> benchmark_cancel_requested_{false};
    std::atomic<bool> benchmark_in_progress_{false};
};

} // namespace workbench
