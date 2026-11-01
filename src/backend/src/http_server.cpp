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
#include <mutex>
#include <cmath>
#include <random>
#include <iomanip>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

namespace workbench {

static std::chrono::steady_clock::time_point g_process_start_tp = std::chrono::steady_clock::now();
static double g_launch_to_ready_ms = 0.0;
static std::mutex g_log_mutex;
static std::ofstream g_server_log_file;

void log_server_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << line << std::endl;
    if (g_server_log_file.is_open()) {
        g_server_log_file << line << std::endl;
        g_server_log_file.flush();
    }
}

void HttpServer::set_process_start_time(std::chrono::steady_clock::time_point tp) {
    g_process_start_tp = tp;
}

double HttpServer::get_launch_to_ready_ms() {
    return g_launch_to_ready_ms;
}

static std::string get_executable_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0) {
        std::wstring ws(buf);
        size_t pos = ws.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            ws = ws.substr(0, pos);
        }
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), NULL, 0, NULL, NULL);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &str[0], size_needed, NULL, NULL);
        for (char& c : str) {
            if (c == '\\') c = '/';
        }
        return str;
    }
#endif
    return ".";
}

static std::string resolve_data_file(const std::string& filename) {
    std::string exe_dir = get_executable_dir();
    std::string c1 = exe_dir + "/data/" + filename;
    std::ifstream f1(c1, std::ios::binary);
    if (f1.good()) return c1;

    std::string c2 = exe_dir + "/../data/" + filename;
    std::ifstream f2(c2, std::ios::binary);
    if (f2.good()) return c2;

    return c1;
}

static inline std::string trim_h(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static std::string escape_json(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\b') ss << "\\b";
        else if (c == '\f') ss << "\\f";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else ss << c;
    }
    return ss.str();
}

static bool parse_double_safe(const std::string& str, double& out, double min_val, double max_val, std::string& err_msg, const std::string& param_name) {
    if (str.empty()) {
        err_msg = "Parameter '" + param_name + "' cannot be empty.";
        return false;
    }
    try {
        size_t idx = 0;
        double val = std::stod(str, &idx);
        if (idx != str.size() || !std::isfinite(val)) {
            err_msg = "Parameter '" + param_name + "' must be a finite numerical value, got: '" + str + "'.";
            return false;
        }
        if (val < min_val || val > max_val) {
            std::ostringstream ss;
            ss << "Parameter '" << param_name << "' value (" << val << ") out of allowed range [" << min_val << ", " << max_val << "].";
            err_msg = ss.str();
            return false;
        }
        out = val;
        return true;
    } catch (...) {
        err_msg = "Invalid numerical format for parameter '" + param_name + "': '" + str + "'.";
        return false;
    }
}

static bool parse_size_t_safe(const std::string& str, size_t& out, size_t min_val, size_t max_val, std::string& err_msg, const std::string& param_name) {
    if (str.empty()) {
        err_msg = "Parameter '" + param_name + "' cannot be empty.";
        return false;
    }
    for (size_t i = 0; i < str.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(str[i]))) {
            err_msg = "Parameter '" + param_name + "' must be a non-negative integer, got: '" + str + "'.";
            return false;
        }
    }
    try {
        size_t idx = 0;
        unsigned long long val = std::stoull(str, &idx);
        if (idx != str.size()) {
            err_msg = "Parameter '" + param_name + "' must be a valid integer, got: '" + str + "'.";
            return false;
        }
        if (val < min_val || val > max_val) {
            std::ostringstream ss;
            ss << "Parameter '" << param_name << "' value (" << val << ") out of allowed range [" << min_val << ", " << max_val << "].";
            err_msg = ss.str();
            return false;
        }
        out = static_cast<size_t>(val);
        return true;
    } catch (...) {
        err_msg = "Invalid integer format for parameter '" + param_name + "': '" + str + "'.";
        return false;
    }
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

    // Generate secure per-process session token
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << dis(gen) << std::setw(16) << dis(gen);
    session_token_ = ss.str();
}

HttpServer::~HttpServer() {
    stop();
}

std::string HttpServer::get_base_url() const {
    return "http://" + host_ + ":" + std::to_string(actual_port_);
}

bool HttpServer::start() {
    WSADATA wsaData;
    int wsa_res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_res != 0) {
        std::cerr << "[HttpServer] WSAStartup failed: " << wsa_res << std::endl;
        return false;
    }

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
        server_addr.sin_port = htons(static_cast<uint16_t>(bind_port));

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

    std::string exe_dir = get_executable_dir();
    std::string logs_dir = exe_dir + "/logs";
    CreateDirectoryA(logs_dir.c_str(), NULL);
    if (!g_server_log_file.is_open()) {
        g_server_log_file.open(logs_dir + "/server.log", std::ios::out | std::ios::app);
    }

    // Save active port for Launch.exe redirection
    std::ofstream port_file(logs_dir + "/port.txt", std::ios::out | std::ios::trunc);
    if (port_file.is_open()) {
        port_file << actual_port_ << "\n";
    }

    auto ready_tp = std::chrono::steady_clock::now();
    g_launch_to_ready_ms = std::chrono::duration<double, std::milli>(ready_tp - g_process_start_tp).count();
    if (g_launch_to_ready_ms < 0.0) g_launch_to_ready_ms = 0.0;

    is_running_ = true;
    server_thread_ = std::thread(&HttpServer::listen_loop, this);
    log_server_line("[HttpServer] Serving on " + get_base_url() + " (launch_to_ready: " + std::to_string(g_launch_to_ready_ms) + " ms)");
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

    WSACleanup();
    log_server_line("[HttpServer] Server stopped.");
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

    auto send_resp = [sock](const HttpResponse& resp) {
        std::ostringstream ss;
        ss << "HTTP/1.1 " << resp.status_code << " ";
        if (resp.status_code == 200) ss << "OK";
        else if (resp.status_code == 400) ss << "Bad Request";
        else if (resp.status_code == 403) ss << "Forbidden";
        else if (resp.status_code == 404) ss << "Not Found";
        else if (resp.status_code == 409) ss << "Conflict";
        else if (resp.status_code == 413) ss << "Payload Too Large";
        else ss << "Server Error";
        ss << "\r\n";
        ss << "Content-Type: " << resp.content_type << "\r\n";
        ss << "Content-Length: " << resp.body.size() << "\r\n";
        ss << "Connection: close\r\n\r\n";
        ss << resp.body;
        std::string full_resp = ss.str();
        send(sock, full_resp.data(), static_cast<int>(full_resp.size()), 0);
        closesocket(sock);
    };

    try {
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

        // Parse headers case-insensitively
        std::string line;
        size_t content_length = 0;
        while (std::getline(stream, line)) {
            std::string trimmed = trim_h(line);
            size_t colon = trimmed.find(':');
            if (colon != std::string::npos) {
                std::string header_name = trim_h(trimmed.substr(0, colon));
                std::string header_val = trim_h(trimmed.substr(colon + 1));
                std::string lower_name = header_name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return std::tolower(c); });
                req.headers[lower_name] = header_val;

                if (lower_name == "content-length") {
                    try {
                        content_length = std::stoull(header_val);
                    } catch (...) {
                        content_length = 0;
                    }
                } else if (lower_name == "content-type") {
                    req.content_type = header_val;
                }
            }
        }

        // Security check: Validate Host header (reject any request whose Host is not 127.0.0.1:<port> or localhost:<port>)
        auto host_it = req.headers.find("host");
        if (host_it != req.headers.end()) {
            std::string h = trim_h(host_it->second);
            std::string exp1 = "127.0.0.1:" + std::to_string(actual_port_);
            std::string exp2 = "localhost:" + std::to_string(actual_port_);
            if (h != exp1 && h != exp2) {
                HttpResponse resp;
                resp.status_code = 403;
                resp.content_type = "application/json";
                resp.body = "{\"error\":\"Forbidden: Invalid Host header: " + escape_json(h) + "\",\"status\":403}";
                log_server_line("[HttpServer] " + req.method + " " + req.path + " -> 403 (Invalid Host: " + h + ")");
                send_resp(resp);
                return;
            }
        }

        // Security check: Origin header on POST routes
        if (req.method == "POST") {
            auto orig_it = req.headers.find("origin");
            if (orig_it != req.headers.end() && !orig_it->second.empty()) {
                std::string orig = trim_h(orig_it->second);
                std::string exp_orig = "http://127.0.0.1:" + std::to_string(actual_port_);
                if (orig != exp_orig) {
                    HttpResponse resp;
                    resp.status_code = 403;
                    resp.content_type = "application/json";
                    resp.body = "{\"error\":\"Forbidden: Origin rejected: " + escape_json(orig) + "\",\"status\":403}";
                    log_server_line("[HttpServer] " + req.method + " " + req.path + " -> 403 (Origin rejected: " + orig + ")");
                    send_resp(resp);
                    return;
                }
            }
        }

        const size_t MAX_PAYLOAD_SIZE = 50 * 1024 * 1024; // 50 MB limit
        if (content_length > MAX_PAYLOAD_SIZE) {
            HttpResponse resp;
            resp.status_code = 413;
            resp.content_type = "application/json";
            resp.body = "{\"error\":\"Payload exceeds 50MB file size limit.\",\"status\":413}";
            log_server_line("[HttpServer] " + req.method + " " + req.path + " -> 413 (Payload Too Large)");
            send_resp(resp);
            return;
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

        // Route request safely
        HttpResponse resp = route_request(req);
        log_server_line("[HttpServer] " + req.method + " " + req.path + " -> " + std::to_string(resp.status_code) + " (" + std::to_string(resp.body.size()) + " bytes)");
        send_resp(resp);

    } catch (const std::exception& e) {
        HttpResponse err_resp;
        err_resp.status_code = 400;
        err_resp.content_type = "application/json";
        err_resp.body = "{\"error\":\"" + escape_json(e.what()) + "\",\"status\":400}";
        log_server_line(std::string("[HttpServer Exception] ") + e.what());
        send_resp(err_resp);
    } catch (...) {
        HttpResponse err_resp;
        err_resp.status_code = 500;
        err_resp.content_type = "application/json";
        err_resp.body = "{\"error\":\"Internal server error occurred.\",\"status\":500}";
        log_server_line("[HttpServer Unknown Exception]");
        send_resp(err_resp);
    }
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
           << "\"session_token\":\"" << session_token_ << "\","
           << "\"app_name\":\"PASS Science Works Unified Engineering Workbench\","
           << "\"version\":\"1.0.0\","
           << "\"cpu_model\":\"" << MulticoreBenchmarker::get_cpu_brand_string() << "\","
           << "\"hardware_concurrency\":" << MulticoreBenchmarker::get_hardware_concurrency() << ","
           << "\"active_port\":" << actual_port_ << ","
           << "\"launch_to_ready_ms\":" << g_launch_to_ready_ms
           << "}";
        resp.body = ss.str();
        return resp;
    }

    // Samples Endpoints (Bundled data resolved from exe directory)
    if (req.path == "/api/samples/stl" && req.method == "GET") {
        std::string sample_path = resolve_data_file("sample_bracket.stl");
        StlParseResult res = StlParser::parse_file(sample_path);
        resp.status_code = res.success ? 200 : 500;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    if (req.path == "/api/samples/ansys" && req.method == "GET") {
        std::string sample_path = resolve_data_file("ansys_thermal_synthetic.csv");
        AnsysParseResult res = AnsysParser::parse_file(sample_path);
        resp.status_code = res.success ? 200 : 500;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    // STL Parser (Raw text body only)
    if (req.path == "/api/stl/parse" && req.method == "POST") {
        StlParseResult res = StlParser::parse_string(req.body);
        resp.status_code = res.success ? 200 : 400;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    // ANSYS CSV Parser (Raw text body only)
    if (req.path == "/api/ansys/parse" && req.method == "POST") {
        AnsysParseResult res = AnsysParser::parse_string(req.body);
        resp.status_code = res.success ? 200 : 400;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    // FEA Solve (Custom or Default) with strict validation
    if (req.path == "/api/fea/solve") {
        ThermalParams1D p;
        std::string err;
        if (!req.query.empty()) {
            std::istringstream qs(req.query);
            std::string kv;
            while (std::getline(qs, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    if (k == "N") {
                        if (!parse_size_t_safe(v, p.num_elements_N, 1, 100000, err, "N (element count)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "L") {
                        if (!parse_double_safe(v, p.length_L, 1e-4, 1000.0, err, "L (rod length)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "A") {
                        if (!parse_double_safe(v, p.area_A, 1e-8, 100.0, err, "A (cross-section area)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "k") {
                        if (!parse_double_safe(v, p.conductivity_k, 1e-6, 100000.0, err, "k (conductivity)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "q") {
                        if (!parse_double_safe(v, p.heat_gen_q, 0.0, 1e9, err, "q (heat generation)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "T0") {
                        if (!parse_double_safe(v, p.temp_T0, -273.15, 1e6, err, "T0 (left temperature)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "TL") {
                        if (!parse_double_safe(v, p.temp_TL, -273.15, 1e6, err, "TL (right temperature)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    }
                }
            }
        }
        ThermalSolveResult1D res = ThermalFEMSolver1D::solve(p);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    // FEA Convergence Study (with parameter validation)
    if (req.path == "/api/fea/convergence") {
        ThermalParams1D p;
        std::string err;
        if (!req.query.empty()) {
            std::istringstream qs(req.query);
            std::string kv;
            while (std::getline(qs, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    if (k == "L") {
                        if (!parse_double_safe(v, p.length_L, 1e-4, 1000.0, err, "L (rod length)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "A") {
                        if (!parse_double_safe(v, p.area_A, 1e-8, 100.0, err, "A (cross-section area)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "k") {
                        if (!parse_double_safe(v, p.conductivity_k, 1e-6, 100000.0, err, "k (conductivity)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "q") {
                        if (!parse_double_safe(v, p.heat_gen_q, 0.0, 1e9, err, "q (heat generation)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "T0") {
                        if (!parse_double_safe(v, p.temp_T0, -273.15, 1e6, err, "T0 (left temperature)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "TL") {
                        if (!parse_double_safe(v, p.temp_TL, -273.15, 1e6, err, "TL (right temperature)")) {
                            resp.status_code = 400;
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    }
                }
            }
        }
        ConvergenceStudyResult res = ThermalFEMSolver1D::run_convergence_study(p);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = res.to_json();
        return resp;
    }

    // Multicore Performance Benchmark
    if (req.path == "/api/performance/run") {
        BenchmarkConfig cfg;
        std::string err;
        
        if (!req.query.empty()) {
            std::istringstream qs(req.query);
            std::string kv;
            while (std::getline(qs, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    if (k == "cases") {
                        if (!parse_size_t_safe(v, cfg.case_count, 1, 200000, err, "cases")) {
                            resp.status_code = 400;
                            resp.content_type = "application/json";
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "mesh") {
                        if (!parse_size_t_safe(v, cfg.mesh_size_N, 1, 5000, err, "mesh")) {
                            resp.status_code = 400;
                            resp.content_type = "application/json";
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                    } else if (k == "seed") {
                        size_t s = 42;
                        if (!parse_size_t_safe(v, s, 0, 4294967295ULL, err, "seed")) {
                            resp.status_code = 400;
                            resp.content_type = "application/json";
                            resp.body = "{\"error\":\"" + escape_json(err) + "\",\"status\":400}";
                            return resp;
                        }
                        cfg.seed = static_cast<uint32_t>(s);
                    }
                }
            }
        }

        bool expected = false;
        if (!benchmark_in_progress_.compare_exchange_strong(expected, true)) {
            resp.status_code = 409;
            resp.content_type = "application/json";
            resp.body = "{\"error\":\"A benchmark is already running\",\"status\":409}";
            return resp;
        }

        struct BenchmarkGuard {
            std::atomic<bool>& flag;
            ~BenchmarkGuard() { flag.store(false); }
        } bg_guard{benchmark_in_progress_};

        benchmark_cancel_requested_.store(false);

        BenchmarkSummary bench = MulticoreBenchmarker::run_benchmark(cfg, &benchmark_cancel_requested_);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = bench.to_json();
        return resp;
    }

    // Benchmark Cancellation
    if (req.path == "/api/performance/cancel" && req.method == "POST") {
        benchmark_cancel_requested_.store(true);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = "{\"success\":true}";
        return resp;
    }

    // Benchmark Real Progress (Thread-Safe & Non-blocking)
    if (req.path == "/api/performance/progress") {
        BenchmarkProgress prog = MulticoreBenchmarker::get_progress();
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = prog.to_json();
        return resp;
    }

    // Hardware Discovery (Thread-Safe)
    if (req.path == "/api/hardware/discover") {
        std::lock_guard<std::mutex> lock(hw_mutex_);
        auto list = hw_manager_->discover_devices();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) ss << ",";
            const auto& d = list[i];
            ss << "{"
               << "\"id\":\"" << escape_json(d.id) << "\","
               << "\"name\":\"" << escape_json(d.product_name) << "\","
               << "\"type\":\"" << (d.type == DeviceType::MICROCONTROLLER ? "MCU" : (d.type == DeviceType::FPGA ? "FPGA" : "JTAG")) << "\","
               << "\"manufacturer\":\"" << escape_json(d.manufacturer) << "\","
               << "\"vid_pid\":\"" << escape_json(d.usb_vid_pid) << "\","
               << "\"serial\":\"" << escape_json(d.serial_number) << "\","
               << "\"firmware\":\"" << escape_json(d.firmware_version) << "\","
               << "\"voltages\":\"" << escape_json(d.voltage_domains) << "\","
               << "\"connector\":\"" << escape_json(d.physical_connector) << "\","
               << "\"protocol\":\"" << escape_json(d.interface_protocol) << "\","
               << "\"is_simulated\":true"
               << "}";
        }
        ss << "]";
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = ss.str();
        return resp;
    }

    // Hardware Connect (Thread-Safe)
    if (req.path == "/api/hardware/connect" && req.method == "POST") {
        std::lock_guard<std::mutex> lock(hw_mutex_);
        std::string device_id = trim_h(req.body);
        bool ok = hw_manager_->connect_device(device_id);
        resp.status_code = ok ? 200 : 400;
        resp.content_type = "application/json";
        resp.body = "{\"success\":" + std::string(ok ? "true" : "false") + "}";
        return resp;
    }

    // Hardware Disconnect (Thread-Safe)
    if (req.path == "/api/hardware/disconnect" && req.method == "POST") {
        std::lock_guard<std::mutex> lock(hw_mutex_);
        std::string device_id = trim_h(req.body);
        bool ok = hw_manager_->disconnect_device(device_id);
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = "{\"success\":" + std::string(ok ? "true" : "false") + "}";
        return resp;
    }

    // Hardware Telemetry (Thread-Safe)
    if (req.path == "/api/hardware/telemetry") {
        std::lock_guard<std::mutex> lock(hw_mutex_);
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

    // Hardware Inject Fault (Thread-Safe)
    if (req.path == "/api/hardware/inject_fault" && req.method == "POST") {
        std::lock_guard<std::mutex> lock(hw_mutex_);
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
        resp.body = "{\"success\":true,\"injected\":\"" + escape_json(fault_str) + "\"}";
        return resp;
    }

    // Hardware Recover Fault (Thread-Safe)
    if (req.path == "/api/hardware/recover" && req.method == "POST") {
        std::lock_guard<std::mutex> lock(hw_mutex_);
        std::string dev_id = trim_h(req.body);
        bool ok = hw_manager_->recover_device_fault(dev_id);
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
    if (req.path == "/api/backprop/study" || req.path == "/api/backprop/run") {
        BackpropSummary bp = BackpropEngine::run_full_study();
        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = bp.to_json();
        return resp;
    }

    // CSV Exports (Parameter-Driven)
    if (req.path == "/api/export/fea_csv") {
        ThermalParams1D p;
        bool want_json = (req.query.find("format=json") != std::string::npos);
        if (!req.query.empty()) {
            std::istringstream qs(req.query);
            std::string kv, err;
            while (std::getline(qs, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    if (k == "L") parse_double_safe(v, p.length_L, 1e-4, 1000.0, err, "L");
                    else if (k == "A") parse_double_safe(v, p.area_A, 1e-8, 100.0, err, "A");
                    else if (k == "k") parse_double_safe(v, p.conductivity_k, 1e-6, 100000.0, err, "k");
                    else if (k == "q") parse_double_safe(v, p.heat_gen_q, 0.0, 1e9, err, "q");
                    else if (k == "T0") parse_double_safe(v, p.temp_T0, -273.15, 1e6, err, "T0");
                    else if (k == "TL") parse_double_safe(v, p.temp_TL, -273.15, 1e6, err, "TL");
                }
            }
        }
        ConvergenceStudyResult study = ThermalFEMSolver1D::run_convergence_study(p);
        resp.status_code = 200;
        if (want_json) {
            resp.content_type = "application/json";
            resp.body = "{\"status\":200,\"csv\":\"" + escape_json(study.to_csv()) + "\"}";
        } else {
            resp.content_type = "text/csv; charset=utf-8";
            resp.body = study.to_csv();
        }
        return resp;
    }

    if (req.path == "/api/export/fea_profile_csv") {
        ThermalParams1D p;
        bool want_json = (req.query.find("format=json") != std::string::npos);
        if (!req.query.empty()) {
            std::istringstream qs(req.query);
            std::string kv, err;
            while (std::getline(qs, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    if (k == "L") parse_double_safe(v, p.length_L, 1e-4, 1000.0, err, "L");
                    else if (k == "A") parse_double_safe(v, p.area_A, 1e-8, 100.0, err, "A");
                    else if (k == "k") parse_double_safe(v, p.conductivity_k, 1e-6, 100000.0, err, "k");
                    else if (k == "q") parse_double_safe(v, p.heat_gen_q, 0.0, 1e9, err, "q");
                    else if (k == "T0") parse_double_safe(v, p.temp_T0, -273.15, 1e6, err, "T0");
                    else if (k == "TL") parse_double_safe(v, p.temp_TL, -273.15, 1e6, err, "TL");
                }
            }
        }
        ConvergenceStudyResult study = ThermalFEMSolver1D::run_convergence_study(p);
        resp.status_code = 200;
        if (want_json) {
            resp.content_type = "application/json";
            resp.body = "{\"status\":200,\"csv\":\"" + escape_json(study.to_profile_csv()) + "\"}";
        } else {
            resp.content_type = "text/csv; charset=utf-8";
            resp.body = study.to_profile_csv();
        }
        return resp;
    }

    if (req.path == "/api/export/performance_csv") {
        BenchmarkConfig cfg;
        bool want_json = (req.query.find("format=json") != std::string::npos);
        if (!req.query.empty()) {
            std::istringstream qs(req.query);
            std::string kv, err;
            while (std::getline(qs, kv, '&')) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    std::string v = kv.substr(eq + 1);
                    if (k == "cases") parse_size_t_safe(v, cfg.case_count, 1, 200000, err, "cases");
                    else if (k == "mesh") parse_size_t_safe(v, cfg.mesh_size_N, 1, 5000, err, "mesh");
                    else if (k == "seed") {
                        size_t s = 42;
                        if (parse_size_t_safe(v, s, 0, 4294967295ULL, err, "seed")) {
                            cfg.seed = static_cast<uint32_t>(s);
                        }
                    }
                }
            }
        }
        BenchmarkSummary bench = MulticoreBenchmarker::run_benchmark(cfg);
        resp.status_code = 200;
        if (want_json) {
            resp.content_type = "application/json";
            resp.body = "{\"status\":200,\"csv\":\"" + escape_json(bench.to_csv()) + "\"}";
        } else {
            resp.content_type = "text/csv; charset=utf-8";
            resp.body = bench.to_csv();
        }
        return resp;
    }

    if (req.path == "/api/export/backprop_csv") {
        bool want_json = (req.query.find("format=json") != std::string::npos);
        BackpropSummary bp = BackpropEngine::run_full_study();
        resp.status_code = 200;
        if (want_json) {
            resp.content_type = "application/json";
            resp.body = "{\"status\":200,\"csv\":\"" + escape_json(bp.to_csv()) + "\"}";
        } else {
            resp.content_type = "text/csv; charset=utf-8";
            resp.body = bp.to_csv();
        }
        return resp;
    }

    // Shutdown (Requires valid session token)
    if (req.path == "/api/shutdown" && req.method == "POST") {
        auto tok_it = req.headers.find("x-session-token");
        std::string tok = (tok_it != req.headers.end()) ? tok_it->second : "";
        if (tok.empty() || tok != session_token_) {
            resp.status_code = 403;
            resp.content_type = "application/json";
            resp.body = "{\"error\":\"Forbidden: Invalid or missing X-Session-Token.\",\"status\":403}";
            return resp;
        }

        resp.status_code = 200;
        resp.content_type = "application/json";
        resp.body = "{\"status\":\"SHUTTING_DOWN\"}";
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            this->stop();
        }).detach();
        return resp;
    }

    // =========================================================
    // Static File Serving (Security: Traversal Prevention)
    // =========================================================
    std::string rel_path = req.path;
    if (rel_path == "/" || rel_path.empty()) {
        rel_path = "/index.html";
    }

    // Normalize slashes
    for (char& c : rel_path) {
        if (c == '\\') c = '/';
    }

    // Reject path traversal attacks
    if (rel_path.find("..") != std::string::npos || rel_path.find(':') != std::string::npos) {
        resp.status_code = 403;
        resp.content_type = "text/plain";
        resp.body = "403 Forbidden: Invalid file path.";
        return resp;
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
