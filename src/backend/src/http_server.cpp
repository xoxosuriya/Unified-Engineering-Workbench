#include "http_server.hpp"
#include "stl_parser.hpp"
#include "ansys_parser.hpp"
#include "thermal_fea_1d.hpp"
#include "multicore_bench.hpp"
#include "backprop_engine.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

namespace workbench {

static inline std::string trim_h(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static std::string get_mime_type(const std::string& path) {
    if (path.rfind(".html") != std::string::npos || path.rfind(".htm") != std::string::npos) return "text/html; charset=utf-8";
    if (path.rfind(".css") != std::string::npos) return "text/css";
    if (path.rfind(".js") != std::string::npos) return "application/javascript";
    if (path.rfind(".json") != std::string::npos) return "application/json";
    if (path.rfind(".csv") != std::string::npos) return "text/csv; charset=utf-8";
    if (path.rfind(".png") != std::string::npos) return "image/png";
    if (path.rfind(".jpg") != std::string::npos || path.rfind(".jpeg") != std::string::npos) return "image/jpeg";
    if (path.rfind(".svg") != std::string::npos) return "image/svg+xml";
    if (path.rfind(".stl") != std::string::npos) return "text/plain";
    return "application/octet-stream";
}

HttpServer::HttpServer(const std::string& host, int port, const std::string& web_root_dir)
    : host_(host), preferred_port_(port), web_root_dir_(web_root_dir) {
    hw_manager_ = std::make_unique<EmbeddedHardwareManager>();
}

HttpServer::~HttpServer() {
    stop();
}

std::string HttpServer::get_base_url() const {
    return "http://" + host_ + ":" + std::to_string(actual_port_);
}

bool HttpServer::start() {
#if defined(_WIN32)
    WSADATA wsaData;
    int wsa_res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_res != 0) {
        std::cerr << "[HttpServer] WSAStartup failed: " << wsa_res << std::endl;
        return false;
    }
#endif

    SOCKET listen_sock = INVALID_SOCKET;
    int bind_port = preferred_port_;

    // Try ports preferred_port_ .. preferred_port_ + 20
    for (int port_attempt = 0; port_attempt <= 20; ++port_attempt) {
        bind_port = preferred_port_ + port_attempt;
        listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_sock == INVALID_SOCKET) {
            continue;
        }

        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(host_.c_str());
        server_addr.sin_port = htons(static_cast<u_short>(bind_port));

        if (bind(listen_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == 0) {
            if (listen(listen_sock, 32) == 0) {
                actual_port_ = bind_port;
                listen_socket_ = static_cast<uintptr_t>(listen_sock);
                break;
            }
        }

        closesocket(listen_sock);
        listen_sock = INVALID_SOCKET;
    }

    if (listen_sock == INVALID_SOCKET) {
        std::cerr << "[HttpServer] Failed to bind to any port in range " 
                  << preferred_port_ << "-" << (preferred_port_ + 20) << std::endl;
        return false;
    }

    is_running_ = true;
    server_thread_ = std::thread(&HttpServer::listen_loop, this);
    std::cout << "[HttpServer] Serving on " << get_base_url() << std::endl;
    return true;
}

void HttpServer::stop() {
    if (!is_running_) return;
    is_running_ = false;

    if (listen_socket_ != 0 && listen_socket_ != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(listen_socket_));
        listen_socket_ = 0;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

#if defined(_WIN32)
    WSACleanup();
#endif
}

void HttpServer::wait() {
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void HttpServer::listen_loop() {
    SOCKET server_fd = static_cast<SOCKET>(listen_socket_);

    while (is_running_) {
        sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_sock = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_sock == INVALID_SOCKET) {
            if (!is_running_) break;
            continue;
        }

        // Spawn detached client handler
        std::thread([this, client_sock]() {
            this->handle_client(static_cast<uintptr_t>(client_sock));
        }).detach();
    }
}

void HttpServer::handle_client(uintptr_t client_socket) {
    SOCKET sock = static_cast<SOCKET>(client_socket);

    std::vector<char> buffer(65536);
    int bytes_received = recv(sock, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);

    if (bytes_received <= 0) {
        closesocket(sock);
        return;
    }

    buffer[bytes_received] = '\0';
    std::string raw_request(buffer.data(), bytes_received);

    // Parse HTTP Request
    HttpRequest req;
    size_t header_end = raw_request.find("\r\n\r\n");
    std::string header_part = (header_end != std::string::npos) ? raw_request.substr(0, header_end) : raw_request;
    std::string body_part = (header_end != std::string::npos) ? raw_request.substr(header_end + 4) : "";

    std::istringstream stream(header_part);
    std::string request_line;
    if (std::getline(stream, request_line)) {
        std::istringstream rls(request_line);
        rls >> req.method >> req.path;
    }

    // Split query params
    size_t q_pos = req.path.find('?');
    if (q_pos != std::string::npos) {
        req.query = req.path.substr(q_pos + 1);
        req.path = req.path.substr(0, q_pos);
    }

    // Parse headers
    std::string line;
    size_t content_length = 0;
    while (std::getline(stream, line)) {
        std::string trimmed = trim_h(line);
        if (trimmed.rfind("Content-Length:", 0) == 0) {
            content_length = std::stoull(trim_h(trimmed.substr(15)));
        } else if (trimmed.rfind("Content-Type:", 0) == 0) {
            req.content_type = trim_h(trimmed.substr(13));
        }
    }

    // Read remaining body if Content-Length specified and larger than initial buffer
    if (content_length > body_part.size()) {
        size_t needed = content_length - body_part.size();
        std::vector<char> rest_buffer(needed);
        size_t total_read = 0;
        while (total_read < needed) {
            int r = recv(sock, rest_buffer.data() + total_read, static_cast<int>(needed - total_read), 0);
            if (r <= 0) break;
            total_read += r;
        }
        body_part.append(rest_buffer.data(), total_read);
    }
    req.body = body_part;

    // Route request
    HttpResponse resp = route_request(req);

    // Send HTTP response
    std::ostringstream ss;
    ss << "HTTP/1.1 " << resp.status_code << " " << (resp.status_code == 200 ? "OK" : (resp.status_code == 404 ? "Not Found" : "Error")) << "\r\n";
    ss << "Content-Type: " << resp.content_type << "\r\n";
    ss << "Content-Length: " << resp.body.size() << "\r\n";
    ss << "Access-Control-Allow-Origin: *\r\n";
    ss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    ss << "Access-Control-Allow-Headers: Content-Type\r\n";
    ss << "Connection: close\r\n\r\n";
    ss << resp.body;

    std::string full_resp = ss.str();
    send(sock, full_resp.data(), static_cast<int>(full_resp.size()), 0);
    closesocket(sock);
}

HttpResponse HttpServer::route_request(const HttpRequest& req) {
    HttpResponse resp;

    if (req.method == "OPTIONS") {
        resp.status_code = 200;
        resp.body = "";
        return resp;
    }

    // =========================================================
    // API Endpoints
    // =========================================================

    // Status
    if (req.path == "/api/status") {
        resp.status_code = 200;
        resp.content_type = "application/json";
        std::ostringstream ss;
        ss << "{"
           << "\"status\":\"ONLINE\","
           << "\"app_name\":\"PASS Science Works Unified Engineering Workbench\","
           << "\"version\":\"1.0.0\","
           << "\"cpu_model\":\"" << MulticoreBenchmarker::get_cpu_brand_string() << "\","
           << "\"hardware_concurrency\":" << MulticoreBenchmarker::get_hardware_concurrency() << ","
           << "\"active_port\":" << actual_port_
           << "}";
        resp.body = ss.str();
        return resp;
    }

    // STL Parser
    if (req.path == "/api/stl/parse" && req.method == "POST") {
        StlParseResult res;
        // Check if body is raw STL text or a filepath
        if (req.body.rfind("filepath:", 0) == 0) {
            std::string path = trim_h(req.body.substr(9));
            res = StlParser::parse_file(path);
        } else {
            res = StlParser::parse_string(req.body);
        }
        resp.status_code = res.success ? 200 : 400;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    // ANSYS CSV Parser
    if (req.path == "/api/ansys/parse" && req.method == "POST") {
        AnsysParseResult res;
        if (req.body.rfind("filepath:", 0) == 0) {
            std::string path = trim_h(req.body.substr(9));
            res = AnsysParser::parse_file(path);
        } else {
            res = AnsysParser::parse_string(req.body);
        }
        resp.status_code = res.success ? 200 : 400;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    // FEA Solve (Custom or Default)
    if (req.path == "/api/fea/solve") {
        ThermalParams1D p;
        // Parse simple URL query params if present, e.g. N=20&k=15&q=12000
        if (!req.query.empty()) {
            std::istringstream qs(req.query);
            std::string kv;
            while (std::getline(qs, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    if (k == "N") p.num_elements_N = std::stoull(v);
                    else if (k == "L") p.length_L = std::stod(v);
                    else if (k == "A") p.area_A = std::stod(v);
                    else if (k == "k") p.conductivity_k = std::stod(v);
                    else if (k == "q") p.heat_gen_q = std::stod(v);
                    else if (k == "T0") p.temp_T0 = std::stod(v);
                    else if (k == "TL") p.temp_TL = std::stod(v);
                }
            }
        }
        ThermalSolveResult1D res = ThermalFEMSolver1D::solve(p);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    // FEA Convergence Study (N = 10, 20, 40, 80)
    if (req.path == "/api/fea/convergence") {
        ConvergenceStudyResult study = ThermalFEMSolver1D::run_convergence_study();
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = study.to_json();
        return resp;
    }

    // Multicore Benchmark Execution
    if (req.path == "/api/performance/run") {
        BenchmarkConfig cfg;
        // Parse query params if provided
        if (!req.query.empty()) {
            std::istringstream qs(req.query);
            std::string kv;
            while (std::getline(qs, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    if (k == "cases") cfg.case_count = std::stoull(v);
                    else if (k == "mesh") cfg.mesh_size_N = std::stoull(v);
                    else if (k == "seed") cfg.seed = static_cast<uint32_t>(std::stoul(v));
                }
            }
        }
        BenchmarkSummary bench = MulticoreBenchmarker::run_benchmark(cfg);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = bench.to_json();
        return resp;
    }

    // Hardware Discovery
    if (req.path == "/api/hardware/discover") {
        auto list = hw_manager_->discover_devices();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) ss << ",";
            const auto& d = list[i];
            ss << "{"
               << "\"id\":\"" << d.id << "\","
               << "\"name\":\"" << d.product_name << "\","
               << "\"type\":\"" << (d.type == DeviceType::MICROCONTROLLER ? "MCU" : (d.type == DeviceType::FPGA ? "FPGA" : "JTAG")) << "\","
               << "\"manufacturer\":\"" << d.manufacturer << "\","
               << "\"vid_pid\":\"" << d.usb_vid_pid << "\","
               << "\"serial\":\"" << d.serial_number << "\","
               << "\"firmware\":\"" << d.firmware_version << "\","
               << "\"voltages\":\"" << d.voltage_domains << "\","
               << "\"connector\":\"" << d.physical_connector << "\","
               << "\"protocol\":\"" << d.interface_protocol << "\","
               << "\"is_simulated\":true"
               << "}";
        }
        ss << "]";
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = ss.str();
        return resp;
    }

    // Hardware Connect
    if (req.path == "/api/hardware/connect" && req.method == "POST") {
        std::string device_id = trim_h(req.body);
        bool ok = hw_manager_->connect_device(device_id);
        resp.status_code = ok ? 200 : 400;
        resp.content_type = "application/json";
        resp.body = "{\"success\":" + std::string(ok ? "true" : "false") + "}";
        return resp;
    }

    // Hardware Disconnect
    if (req.path == "/api/hardware/disconnect" && req.method == "POST") {
        std::string device_id = trim_h(req.body);
        bool ok = hw_manager_->disconnect_device(device_id);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = "{\"success\":" + std::string(ok ? "true" : "false") + "}";
        return resp;
    }

    // Hardware Telemetry
    if (req.path == "/api/hardware/telemetry") {
        std::string device_id = "mcu_stm32h7";
        if (!req.query.empty() && req.query.rfind("id=", 0) == 0) {
            device_id = req.query.substr(3);
        }
        TelemetryPacket pkt = hw_manager_->read_device_telemetry(device_id);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = pkt.to_json();
        return resp;
    }

    // Hardware Inject Fault
    if (req.path == "/api/hardware/inject_fault" && req.method == "POST") {
        // e.g. "device_id:fault_name"
        std::string payload = trim_h(req.body);
        size_t colon = payload.find(':');
        std::string dev_id = (colon != std::string::npos) ? payload.substr(0, colon) : payload;
        std::string fault_str = (colon != std::string::npos) ? payload.substr(colon + 1) : "timeout";

        SimulatedFaultType fault = SimulatedFaultType::COMMUNICATION_TIMEOUT;
        if (fault_str == "crc") fault = SimulatedFaultType::CRC_CHECKSUM_ERROR;
        else if (fault_str == "voltage") fault = SimulatedFaultType::TARGET_VOLTAGE_DROP;

        hw_manager_->inject_device_fault(dev_id, fault);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = "{\"success\":true,\"injected\":\"" + fault_str + "\"}";
        return resp;
    }

    // Hardware Recover Fault
    if (req.path == "/api/hardware/recover" && req.method == "POST") {
        std::string device_id = trim_h(req.body);
        bool ok = hw_manager_->recover_device_fault(device_id);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = "{\"success\":" + std::string(ok ? "true" : "false") + "}";
        return resp;
    }

    // Hardware Architecture Description
    if (req.path == "/api/hardware/architecture") {
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = hw_manager_->get_architecture_summary_json();
        return resp;
    }

    // Backprop Study
    if (req.path == "/api/backprop/study") {
        BackpropSummary bp = BackpropEngine::run_full_study();
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = bp.to_json();
        return resp;
    }

    // CSV Exports
    if (req.path == "/api/export/fea_csv") {
        ConvergenceStudyResult study = ThermalFEMSolver1D::run_convergence_study();
        resp.status_code = 200;
        resp.content_type = "text/csv; charset=utf-8";
        resp.body = study.to_csv();
        return resp;
    }

    if (req.path == "/api/export/performance_csv") {
        BenchmarkConfig cfg;
        BenchmarkSummary bench = MulticoreBenchmarker::run_benchmark(cfg);
        resp.status_code = 200;
        resp.content_type = "text/csv; charset=utf-8";
        resp.body = bench.to_csv();
        return resp;
    }

    if (req.path == "/api/export/backprop_csv") {
        BackpropSummary bp = BackpropEngine::run_full_study();
        resp.status_code = 200;
        resp.content_type = "text/csv; charset=utf-8";
        resp.body = bp.to_csv();
        return resp;
    }

    // Shutdown
    if (req.path == "/api/shutdown" && req.method == "POST") {
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = "{\"status\":\"SHUTTING_DOWN\"}";
        // Terminate server after 100ms
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            this->stop();
        }).detach();
        return resp;
    }

    // =========================================================
    // Static File Serving
    // =========================================================
    std::string rel_path = req.path;
    if (rel_path == "/" || rel_path.empty()) {
        rel_path = "/index.html";
    }

    // Normalize slashes
    for (char& c : rel_path) {
        if (c == '\\') c = '/';
    }

    std::string full_file_path = web_root_dir_ + rel_path;
    std::ifstream file(full_file_path, std::ios::in | std::ios::binary);

    if (file.is_open()) {
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string content(size, '\0');
        file.read(&content[0], size);

        resp.status_code = 200;
        resp.content_type = get_mime_type(rel_path);
        resp.body = content;
        return resp;
    }

    // Not found
    resp.status_code = 404;
    resp.content_type = "text/plain";
    resp.body = "404 Not Found: " + req.path;
    return resp;
}

} // namespace workbench
