#include "http_server.hpp"
#include "stl_parser.hpp"
#include "ansys_parser.hpp"
#include "thermal_fea_1d.hpp"
#include "multicore_bench.hpp"
#include "backprop_engine.hpp"
#include "thermal_geometry_3d.hpp"
#include "thermal_solvers_3d.hpp"

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
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
using socklen_t = int;
#define WORKBENCH_INVALID_SOCKET INVALID_SOCKET
#define WORKBENCH_SOCKET_ERROR SOCKET_ERROR
#define workbench_closesocket(s) closesocket(s)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
using socket_t = int;
#define WORKBENCH_INVALID_SOCKET (-1)
#define WORKBENCH_SOCKET_ERROR (-1)
#define workbench_closesocket(s) ::close(s)
#endif

namespace workbench {

#ifdef WORKBENCH_ENABLE_TEST_HOOKS
static std::mutex g_thermal3d_hook_mutex;
static std::function<void()> g_thermal3d_test_hook = nullptr;

void HttpServer::set_thermal3d_test_hook(std::function<void()> hook) {
    std::lock_guard<std::mutex> lock(g_thermal3d_hook_mutex);
    g_thermal3d_test_hook = std::move(hook);
}
#endif

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
#elif defined(__linux__)
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        std::string path(buf);
        size_t pos = path.find_last_of('/');
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
        return path;
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

namespace {

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonVal {
    JsonType type = JsonType::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonVal> arr;
    std::map<std::string, JsonVal> obj;

    bool is_null() const { return type == JsonType::Null; }
    bool is_bool() const { return type == JsonType::Bool; }
    bool is_number() const { return type == JsonType::Number; }
    bool is_string() const { return type == JsonType::String; }
    bool is_array() const { return type == JsonType::Array; }
    bool is_object() const { return type == JsonType::Object; }

    bool is_integer() const {
        if (!is_number()) return false;
        double intpart;
        return std::isfinite(num) && std::modf(num, &intpart) == 0.0;
    }
};

class SimpleJsonParser {
public:
    static bool parse(const std::string& src, JsonVal& out, std::string& err) {
        SimpleJsonParser p(src);
        try {
            p.skip_ws();
            if (p.pos_ >= p.src_.size()) {
                err = "Request body cannot be empty.";
                return false;
            }
            out = p.parse_value();
            p.skip_ws();
            if (p.pos_ < p.src_.size()) {
                err = "Unexpected trailing characters after JSON value at position " + std::to_string(p.pos_);
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            err = e.what();
            return false;
        }
    }

private:
    explicit SimpleJsonParser(const std::string& src) : src_(src), pos_(0) {}

    const std::string& src_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                pos_++;
            } else {
                break;
            }
        }
    }

    char peek() {
        skip_ws();
        if (pos_ >= src_.size()) return '\0';
        return src_[pos_];
    }

    char get() {
        skip_ws();
        if (pos_ >= src_.size()) throw std::runtime_error("Unexpected end of JSON input");
        return src_[pos_++];
    }

    JsonVal parse_value() {
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        throw std::runtime_error(std::string("Unexpected character in JSON: '") + c + "' at position " + std::to_string(pos_));
    }

    JsonVal parse_object() {
        get(); // consume '{'
        JsonVal v;
        v.type = JsonType::Object;
        skip_ws();
        if (peek() == '}') {
            get();
            return v;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                throw std::runtime_error("Expected string key in object at position " + std::to_string(pos_));
            }
            JsonVal key = parse_string();
            skip_ws();
            if (get() != ':') {
                throw std::runtime_error("Expected ':' after key in object at position " + std::to_string(pos_));
            }
            JsonVal val = parse_value();
            if (v.obj.find(key.str) != v.obj.end()) {
                throw std::runtime_error("Duplicate key in JSON object: '" + key.str + "'");
            }
            v.obj[key.str] = val;
            skip_ws();
            char next = peek();
            if (next == '}') {
                get();
                break;
            }
            if (next == ',') {
                get();
                continue;
            }
            throw std::runtime_error("Expected ',' or '}' in object at position " + std::to_string(pos_));
        }
        return v;
    }

    JsonVal parse_array() {
        get(); // consume '['
        JsonVal v;
        v.type = JsonType::Array;
        skip_ws();
        if (peek() == ']') {
            get();
            return v;
        }
        while (true) {
            JsonVal item = parse_value();
            v.arr.push_back(item);
            skip_ws();
            char next = peek();
            if (next == ']') {
                get();
                break;
            }
            if (next == ',') {
                get();
                continue;
            }
            throw std::runtime_error("Expected ',' or ']' in array at position " + std::to_string(pos_));
        }
        return v;
    }

    JsonVal parse_string() {
        get(); // consume '"'
        std::string s;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') {
                JsonVal v;
                v.type = JsonType::String;
                v.str = s;
                return v;
            }
            if (c == '\\') {
                if (pos_ >= src_.size()) throw std::runtime_error("Unterminated escape sequence in string");
                char esc = src_[pos_++];
                if (esc == '"') s.push_back('"');
                else if (esc == '\\') s.push_back('\\');
                else if (esc == '/') s.push_back('/');
                else if (esc == 'b') s.push_back('\b');
                else if (esc == 'f') s.push_back('\f');
                else if (esc == 'n') s.push_back('\n');
                else if (esc == 'r') s.push_back('\r');
                else if (esc == 't') s.push_back('\t');
                else if (esc == 'u') {
                    if (pos_ + 4 > src_.size()) throw std::runtime_error("Incomplete unicode escape in string");
                    pos_ += 4;
                    s.push_back('?');
                } else {
                    s.push_back(esc);
                }
            } else {
                s.push_back(c);
            }
        }
        throw std::runtime_error("Unclosed string literal in JSON");
    }

    JsonVal parse_number() {
        size_t start = pos_;
        if (pos_ < src_.size() && src_[pos_] == '-') pos_++;
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                if ((c == '+' || c == '-') && (src_[pos_-1] != 'e' && src_[pos_-1] != 'E')) {
                    break;
                }
                pos_++;
            } else {
                break;
            }
        }
        std::string num_str = src_.substr(start, pos_ - start);
        size_t idx = 0;
        double val = 0.0;
        try {
            val = std::stod(num_str, &idx);
        } catch (...) {
            throw std::runtime_error("Malformed number in JSON: '" + num_str + "'");
        }
        if (idx != num_str.size() || !std::isfinite(val)) {
            throw std::runtime_error("Malformed number in JSON: '" + num_str + "'");
        }
        JsonVal v;
        v.type = JsonType::Number;
        v.num = val;
        return v;
    }

    JsonVal parse_bool() {
        if (src_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            JsonVal v;
            v.type = JsonType::Bool;
            v.b = true;
            return v;
        }
        if (src_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            JsonVal v;
            v.type = JsonType::Bool;
            v.b = false;
            return v;
        }
        throw std::runtime_error("Invalid boolean literal in JSON");
    }

    JsonVal parse_null() {
        if (src_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            JsonVal v;
            v.type = JsonType::Null;
            return v;
        }
        throw std::runtime_error("Invalid null literal in JSON");
    }
};

} // anonymous namespace

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
#if defined(_WIN32)
    WSADATA wsaData;
    int wsa_res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_res != 0) {
        std::cerr << "[HttpServer] WSAStartup failed: " << wsa_res << std::endl;
        return false;
    }
#endif

    socket_t listen_sock = WORKBENCH_INVALID_SOCKET;
    int bind_port = preferred_port_;

    // Try ports preferred_port_ .. preferred_port_ + 20
    for (int port_attempt = 0; port_attempt <= 20; ++port_attempt) {
        bind_port = preferred_port_ + port_attempt;
        listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_sock == WORKBENCH_INVALID_SOCKET) {
            continue;
        }

        int opt = 1;
#if defined(_WIN32)
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        if (host_ == "0.0.0.0") {
            server_addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            server_addr.sin_addr.s_addr = inet_addr(host_.c_str());
        }
        server_addr.sin_port = htons(static_cast<uint16_t>(bind_port));

        if (bind(listen_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == 0) {
            if (listen(listen_sock, 32) == 0) {
                actual_port_ = bind_port;
                listen_socket_ = static_cast<uintptr_t>(listen_sock);
                break;
            }
        }

        workbench_closesocket(listen_sock);
        listen_sock = WORKBENCH_INVALID_SOCKET;
    }

    if (listen_sock == WORKBENCH_INVALID_SOCKET) {
        std::cerr << "[HttpServer] Failed to bind to any port in range " 
                  << preferred_port_ << "-" << (preferred_port_ + 20) << std::endl;
        return false;
    }

    std::string exe_dir = get_executable_dir();
    std::string logs_dir = exe_dir + "/logs";
    std::error_code ec;
    std::filesystem::create_directories(logs_dir, ec);
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

    if (listen_socket_ != 0 && listen_socket_ != static_cast<uintptr_t>(WORKBENCH_INVALID_SOCKET)) {
        workbench_closesocket(static_cast<socket_t>(listen_socket_));
        listen_socket_ = 0;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

#if defined(_WIN32)
    WSACleanup();
#endif
    log_server_line("[HttpServer] Server stopped.");
}

void HttpServer::wait() {
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void HttpServer::listen_loop() {
    socket_t server_fd = static_cast<socket_t>(listen_socket_);

    while (is_running_) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        socket_t client_sock = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_sock == WORKBENCH_INVALID_SOCKET) {
            if (!is_running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Spawn detached client handler
        std::thread([this, client_sock]() {
            this->handle_client(static_cast<uintptr_t>(client_sock));
        }).detach();
    }
}

void HttpServer::handle_client(uintptr_t client_socket) {
    socket_t sock = static_cast<socket_t>(client_socket);
    std::string matched_origin = "";

    auto send_resp = [sock, &matched_origin](const HttpResponse& resp) {
        std::ostringstream ss;
        ss << "HTTP/1.1 " << resp.status_code << " ";
        if (resp.status_code == 200) ss << "OK";
        else if (resp.status_code == 204) ss << "No Content";
        else if (resp.status_code == 400) ss << "Bad Request";
        else if (resp.status_code == 403) ss << "Forbidden";
        else if (resp.status_code == 404) ss << "Not Found";
        else if (resp.status_code == 405) ss << "Method Not Allowed";
        else if (resp.status_code == 409) ss << "Conflict";
        else if (resp.status_code == 413) ss << "Payload Too Large";
        else if (resp.status_code == 500) ss << "Internal Server Error";
        else ss << "Server Error";
        ss << "\r\n";
        ss << "Content-Type: " << resp.content_type << "\r\n";
        ss << "Content-Length: " << resp.body.size() << "\r\n";
        if (!matched_origin.empty()) {
            ss << "Access-Control-Allow-Origin: " << matched_origin << "\r\n";
            ss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
            ss << "Access-Control-Allow-Headers: Content-Type, X-Session-Token, Authorization\r\n";
            ss << "Access-Control-Max-Age: 86400\r\n";
            ss << "Vary: Origin\r\n";
        }
        ss << "Connection: close\r\n\r\n";
        ss << resp.body;
        std::string full_resp = ss.str();
        send(sock, full_resp.data(), static_cast<int>(full_resp.size()), 0);
        workbench_closesocket(sock);
    };

    try {
        std::vector<char> buffer(65536);
        int bytes_received = recv(sock, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);

        if (bytes_received <= 0) {
            workbench_closesocket(sock);
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

        // CORS and Origin evaluation
        auto orig_it = req.headers.find("origin");
        if (orig_it != req.headers.end() && !orig_it->second.empty()) {
            std::string orig = trim_h(orig_it->second);
            bool origin_allowed = false;
            if (orig == "https://xoxosuriya.github.io" ||
                orig == "http://127.0.0.1:" + std::to_string(actual_port_) ||
                orig == "http://localhost:" + std::to_string(actual_port_) ||
                orig == "http://localhost:3000" ||
                orig == "http://127.0.0.1:3000" ||
                orig == "http://localhost:8080" ||
                orig == "http://127.0.0.1:8080") {
                origin_allowed = true;
            } else {
                const char* env_origs = std::getenv("ALLOWED_ORIGINS");
                if (env_origs && *env_origs) {
                    std::string allowed_str(env_origs);
                    if (allowed_str.find(orig) != std::string::npos) origin_allowed = true;
                }
            }

            if (origin_allowed) {
                matched_origin = orig;
            } else if (req.method == "POST" || req.path.compare(0, 5, "/api/") == 0) {
                HttpResponse resp;
                resp.status_code = 403;
                resp.content_type = "application/json";
                resp.body = "{\"error\":\"Forbidden: Origin rejected: " + escape_json(orig) + "\",\"status\":403}";
                log_server_line("[HttpServer] " + req.method + " " + req.path + " -> 403 (Origin rejected: " + orig + ")");
                send_resp(resp);
                return;
            }
        }

        // Handle CORS Preflight OPTIONS immediately
        if (req.method == "OPTIONS") {
            HttpResponse preflight;
            preflight.status_code = 204;
            preflight.body = "";
            send_resp(preflight);
            return;
        }

        // Host header validation
        auto host_it = req.headers.find("host");
        if (host_it != req.headers.end()) {
            std::string h = trim_h(host_it->second);
            std::string h_host = h;
            size_t colon_pos = h.find(':');
            if (colon_pos != std::string::npos) {
                h_host = h.substr(0, colon_pos);
            }

            bool host_allowed = false;
            if (h_host == "127.0.0.1" || h_host == "localhost" || h_host == "0.0.0.0") {
                host_allowed = true;
            } else {
                const char* env_hosts = std::getenv("ALLOWED_HOSTS");
                if (env_hosts && *env_hosts) {
                    std::string allowed_str(env_hosts);
                    if (allowed_str.find(h_host) != std::string::npos) host_allowed = true;
                }
                if (h_host.rfind(".onrender.com") != std::string::npos || h_host == "onrender.com") {
                    host_allowed = true;
                }
                if (host_ == "0.0.0.0" || std::getenv("PORT") != nullptr) {
                    host_allowed = true;
                }
            }

            if (!host_allowed) {
                HttpResponse resp;
                resp.status_code = 403;
                resp.content_type = "application/json";
                resp.body = "{\"error\":\"Forbidden: Invalid Host header: " + escape_json(h) + "\",\"status\":403}";
                log_server_line("[HttpServer] " + req.method + " " + req.path + " -> 403 (Invalid Host: " + h + ")");
                send_resp(resp);
                return;
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

HttpResponse HttpServer::handle_thermal3d_models(const HttpRequest& req) {
    (void)req;
    HttpResponse resp;
    resp.status_code = 200;
    resp.content_type = "application/json";

    std::ostringstream ss;
    ss << std::setprecision(10);
    ss << "{\"models\":[";

    const ModelType models[] = {ModelType::CUBE, ModelType::FINNED_HEAT_SINK};
    const std::string model_ids[] = {"CUBE", "FINNED_SINK"};
    const std::string model_names[] = {
        "Model B: 3D Continuum Analytical Cube",
        "Model C: 3D Non-Convex Finned Heat Sink"
    };
    const std::string model_descs[] = {
        "Uniform cube (0.1m x 0.1m x 0.1m) with 1D Dirichlet boundaries and 4 adiabatic Neumann faces.",
        "Finned heat sink with conductive baseplate (Dirichlet), fin convection (Robin), and insulated faces (Neumann)."
    };
    const int resolutions[] = {10, 20, 30};

    for (size_t m = 0; m < 2; ++m) {
        if (m > 0) ss << ",";
        ss << "{"
           << "\"id\":\"" << model_ids[m] << "\","
           << "\"name\":\"" << model_names[m] << "\","
           << "\"description\":\"" << model_descs[m] << "\","
           << "\"resolutions\":[";
        for (size_t r = 0; r < 3; ++r) {
            if (r > 0) ss << ",";
            int N = resolutions[r];
            ThermalGeometry3D geom(models[m], N);
            const GeometryStats& s = geom.get_stats();
            ss << "{"
               << "\"N\":" << N << ","
               << "\"grid_cells\":" << s.total_cells << ","
               << "\"active_cells_M\":" << s.active_cells << ","
               << "\"matrix_nnz\":" << s.expected_nnz << ","
               << "\"cell_size_m\":" << s.cell_size << ","
               << "\"solid_volume_m3\":" << s.solid_volume << ","
               << "\"internal_faces_E\":" << s.internal_faces << ","
               << "\"dirichlet_faces\":" << s.dirichlet_faces << ","
               << "\"robin_faces\":" << s.robin_faces << ","
               << "\"neumann_faces\":" << s.neumann_faces
               << "}";
        }
        ss << "]}";
    }

    ss << "]}";
    resp.body = ss.str();
    return resp;
}

HttpResponse HttpServer::handle_thermal3d_config(const HttpRequest& req) {
    (void)req;
    HttpResponse resp;
    resp.status_code = 200;
    resp.content_type = "application/json";

    ThermalProperties p;
    std::ostringstream ss;
    ss << "{"
       << "\"physical_properties_fixed\":true,"
       << "\"physical_properties\":{"
       << "\"thermal_conductivity_W_mK\":" << p.thermal_conductivity << ","
       << "\"volumetric_heat_gen_W_m3\":" << p.volumetric_heat_gen << ","
       << "\"heat_transfer_coeff_W_m2K\":" << p.heat_transfer_coeff << ","
       << "\"ambient_temp_degC\":" << p.ambient_temp << ","
       << "\"hot_temp_degC\":" << p.hot_temp << ","
       << "\"cold_temp_degC\":" << p.cold_temp
       << "},"
       << "\"solvers\":["
       << "{"
       << "\"id\":\"PROFILE_CHOLESKY\","
       << "\"name\":\"Custom Profile Cholesky (with RCM)\","
       << "\"type\":\"DIRECT\","
       << "\"description\":\"Skyline profile Cholesky with reverse Cuthill-McKee bandwidth minimization.\","
       << "\"relative_residual_threshold\":1e-12"
       << "},"
       << "{"
       << "\"id\":\"PCG\","
       << "\"name\":\"Eigen PCG (Jacobi)\","
       << "\"type\":\"ITERATIVE\","
       << "\"description\":\"Conjugate Gradient with diagonal Jacobi preconditioning via Eigen 3.4.0.\","
       << "\"default_tolerance\":1e-8,"
       << "\"default_max_iterations\":5000,"
       << "\"tolerance_range\":[1e-14,1.0],"
       << "\"max_iterations_range\":[1,100000]"
       << "},"
       << "{"
       << "\"id\":\"SOR\","
       << "\"name\":\"Custom SOR\","
       << "\"type\":\"ITERATIVE\","
       << "\"description\":\"Successive Over-Relaxation with 4-tier MECE termination.\","
       << "\"default_omega\":1.0,"
       << "\"omega_range\":[0.0001,1.9999],"
       << "\"default_tolerance\":1e-8,"
       << "\"default_max_iterations\":10000,"
       << "\"tolerance_range\":[1e-14,1.0],"
       << "\"max_iterations_range\":[1,100000]"
       << "}"
       << "]"
       << "}";
    resp.body = ss.str();
    return resp;
}

HttpResponse HttpServer::handle_thermal3d_health(const HttpRequest& req) {
    (void)req;
    HttpResponse resp;
    resp.status_code = 200;
    resp.content_type = "application/json";

    std::ostringstream ss;
    ss << "{"
       << "\"status\":\"ONLINE\","
       << "\"eigen_version\":\"3.4.0\","
       << "\"solvers_available\":[\"PROFILE_CHOLESKY\",\"PCG\",\"SOR\"],"
       << "\"solve_in_progress\":" << (thermal3d_solve_in_progress_.load() ? "true" : "false")
       << "}";
    resp.body = ss.str();
    return resp;
}

HttpResponse HttpServer::handle_thermal3d_solve(const HttpRequest& req) {
    HttpResponse resp;
    resp.content_type = "application/json";

    std::string trimmed_body = trim_h(req.body);
    if (trimmed_body.empty()) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Request body cannot be empty.\",\"status\":400}";
        return resp;
    }

    JsonVal root;
    std::string json_err;
    if (!SimpleJsonParser::parse(trimmed_body, root, json_err)) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Malformed JSON: " + escape_json(json_err) + "\",\"status\":400}";
        return resp;
    }

    if (!root.is_object()) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Request body must be a JSON object.\",\"status\":400}";
        return resp;
    }

    // Check for unknown root fields
    for (const auto& kv : root.obj) {
        if (kv.first != "model" && kv.first != "resolution" && kv.first != "solver" && kv.first != "solver_options") {
            resp.status_code = 400;
            resp.body = "{\"error\":\"Unknown field in request payload: '" + escape_json(kv.first) + "'. Allowed fields: model, resolution, solver, solver_options.\",\"status\":400}";
            return resp;
        }
    }

    // Required field: model
    if (root.obj.find("model") == root.obj.end()) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Missing required field 'model'. Must be 'CUBE' or 'FINNED_SINK'.\",\"status\":400}";
        return resp;
    }
    const auto& m_val = root.obj.at("model");
    if (!m_val.is_string()) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Field 'model' must be a JSON string.\",\"status\":400}";
        return resp;
    }
    std::string model_str = m_val.str;
    if (model_str != "CUBE" && model_str != "FINNED_SINK") {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Invalid model: '" + escape_json(model_str) + "'. Supported models: 'CUBE', 'FINNED_SINK'.\",\"status\":400}";
        return resp;
    }

    // Required field: resolution
    if (root.obj.find("resolution") == root.obj.end()) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Missing required field 'resolution'. Must be 10, 20, or 30.\",\"status\":400}";
        return resp;
    }
    const auto& r_val = root.obj.at("resolution");
    if (!r_val.is_number() || !r_val.is_integer()) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Field 'resolution' must be an integer.\",\"status\":400}";
        return resp;
    }
    int N = static_cast<int>(r_val.num);
    if (N != 10 && N != 20 && N != 30) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Invalid resolution: " + std::to_string(N) + ". Supported resolutions: 10, 20, 30.\",\"status\":400}";
        return resp;
    }

    // Required field: solver
    if (root.obj.find("solver") == root.obj.end()) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Missing required field 'solver'. Must be 'PROFILE_CHOLESKY', 'PCG', or 'SOR'.\",\"status\":400}";
        return resp;
    }
    const auto& s_val = root.obj.at("solver");
    if (!s_val.is_string()) {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Field 'solver' must be a JSON string.\",\"status\":400}";
        return resp;
    }
    std::string solver_str = s_val.str;
    if (solver_str != "PROFILE_CHOLESKY" && solver_str != "PCG" && solver_str != "SOR") {
        resp.status_code = 400;
        resp.body = "{\"error\":\"Invalid solver: '" + escape_json(solver_str) + "'. Supported solvers: 'PROFILE_CHOLESKY', 'PCG', 'SOR'.\",\"status\":400}";
        return resp;
    }

    // Optional field: solver_options
    double tolerance = 1e-8;
    int max_iterations = (solver_str == "SOR") ? 10000 : 5000;
    double omega = 1.0;

    if (root.obj.find("solver_options") != root.obj.end()) {
        const auto& opt_val = root.obj.at("solver_options");
        if (!opt_val.is_object()) {
            resp.status_code = 400;
            resp.body = "{\"error\":\"Field 'solver_options' must be a JSON object.\",\"status\":400}";
            return resp;
        }
        for (const auto& kv : opt_val.obj) {
            if (kv.first != "tolerance" && kv.first != "max_iterations" && kv.first != "omega") {
                resp.status_code = 400;
                resp.body = "{\"error\":\"Unknown option in 'solver_options': '" + escape_json(kv.first) + "'. Allowed options: tolerance, max_iterations, omega.\",\"status\":400}";
                return resp;
            }
        }
        if (opt_val.obj.find("tolerance") != opt_val.obj.end()) {
            const auto& t_val = opt_val.obj.at("tolerance");
            if (!t_val.is_number() || !std::isfinite(t_val.num) || t_val.num < 1e-14 || t_val.num > 1.0) {
                resp.status_code = 400;
                resp.body = "{\"error\":\"Invalid 'tolerance' value. Must be a finite number in range [1e-14, 1.0].\",\"status\":400}";
                return resp;
            }
            tolerance = t_val.num;
        }
        if (opt_val.obj.find("max_iterations") != opt_val.obj.end()) {
            const auto& mi_val = opt_val.obj.at("max_iterations");
            if (!mi_val.is_number() || !mi_val.is_integer() || mi_val.num < 1.0 || mi_val.num > 100000.0) {
                resp.status_code = 400;
                resp.body = "{\"error\":\"Invalid 'max_iterations' value. Must be an integer in range [1, 100000].\",\"status\":400}";
                return resp;
            }
            max_iterations = static_cast<int>(mi_val.num);
        }
        if (opt_val.obj.find("omega") != opt_val.obj.end()) {
            const auto& w_val = opt_val.obj.at("omega");
            if (!w_val.is_number() || !std::isfinite(w_val.num) || w_val.num <= 0.0 || w_val.num >= 2.0) {
                resp.status_code = 400;
                resp.body = "{\"error\":\"Invalid 'omega' relaxation factor. Must be a finite number in open interval (0.0, 2.0).\",\"status\":400}";
                return resp;
            }
            omega = w_val.num;
        }
    }

    // Atomic concurrency guard (HTTP 409 Conflict)
    bool expected = false;
    if (!thermal3d_solve_in_progress_.compare_exchange_strong(expected, true)) {
        resp.status_code = 409;
        resp.body = "{\"error\":\"A 3D thermal solve is already in progress.\",\"status\":409}";
        return resp;
    }

    struct Thermal3DSolveGuard {
        std::atomic<bool>& flag;
        ~Thermal3DSolveGuard() { flag.store(false); }
    } solve_guard{thermal3d_solve_in_progress_};

#ifdef WORKBENCH_ENABLE_TEST_HOOKS
    {
        std::function<void()> hook_copy = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_thermal3d_hook_mutex);
            hook_copy = g_thermal3d_test_hook;
        }
        if (hook_copy) {
            hook_copy();
        }
    }
#endif

    try {
        ModelType mtype = (model_str == "CUBE") ? ModelType::CUBE : ModelType::FINNED_HEAT_SINK;
        ThermalProperties props; // Approved fixed blueprint physical properties
        ThermalGeometry3D geom(mtype, N, props);
        const GeometryStats& stats = geom.get_stats();

        CSRMatrix A;
        std::vector<double> b;
        double assembly_time_ms = 0.0;
        geom.assemble_system(A, b, &assembly_time_ms);

        std::unique_ptr<IThermalSolver3D> solver;
        if (solver_str == "PROFILE_CHOLESKY") {
            solver = std::make_unique<ProfileCholeskySolver>(/*use_rcm=*/true);
        } else if (solver_str == "PCG") {
            PCGConfig pcg_cfg;
            pcg_cfg.tolerance = tolerance;
            pcg_cfg.max_iterations = max_iterations;
            solver = std::make_unique<EigenPCGSolver>(pcg_cfg);
        } else if (solver_str == "SOR") {
            SORConfig sor_cfg;
            sor_cfg.omega = omega;
            sor_cfg.tolerance = tolerance;
            sor_cfg.max_iterations = max_iterations;
            solver = std::make_unique<SORSolver>(sor_cfg);
        }

        std::vector<double> T;
        SolverResult s_res = solver->solve(A, b, T);

        // Evaluate r = b - Ax, ||r||_2, ||b||_2, and relative residual ||r||_2 / ||b||_2
        double b_norm = 0.0;
        for (double val : b) b_norm += val * val;
        b_norm = std::sqrt(b_norm);

        double final_residual = 0.0;
        double relative_residual = 0.0;
        if (T.size() == stats.active_cells) {
            std::vector<double> Ax(b.size(), 0.0);
            A.matvec(T.data(), Ax.data());
            double r_sq = 0.0;
            for (size_t i = 0; i < b.size(); ++i) {
                double diff = b[i] - Ax[i];
                r_sq += diff * diff;
            }
            final_residual = std::sqrt(r_sq);
            if (std::isfinite(b_norm) && b_norm > 0.0 && std::isfinite(final_residual)) {
                relative_residual = final_residual / b_norm;
            }
        }

        // Apply direct Profile Cholesky relative residual acceptance threshold: 1e-12
        if (solver_str == "PROFILE_CHOLESKY") {
            if (s_res.status == SolverStatus::SUCCESS) {
                if (!std::isfinite(relative_residual) || relative_residual > 1e-12 || b_norm <= 0.0 || !std::isfinite(b_norm)) {
                    s_res.status = SolverStatus::NUMERICAL_BREAKDOWN;
                    s_res.error_message = "Profile Cholesky relative residual exceeded acceptance threshold 1e-12";
                }
            }
        }

        std::string status_str;
        bool usable_field = false;
        switch (s_res.status) {
            case SolverStatus::SUCCESS:
                if (solver_str == "PCG") {
                    status_str = "CONVERGED";
                } else {
                    status_str = "SUCCESS";
                }
                usable_field = true;
                break;
            case SolverStatus::CONVERGED:
                status_str = "CONVERGED";
                usable_field = true;
                break;
            case SolverStatus::MAX_ITERATIONS_REACHED:
                status_str = "MAX_ITERATIONS_REACHED";
                usable_field = false;
                break;
            case SolverStatus::DIVERGED_RESIDUAL:
                status_str = "DIVERGED_RESIDUAL";
                usable_field = false;
                break;
            case SolverStatus::DIVERGED_NUMERICAL:
            case SolverStatus::NUMERICAL_BREAKDOWN:
                status_str = "DIVERGED_NUMERICAL";
                usable_field = false;
                break;
            default:
                status_str = "FAILED";
                usable_field = false;
                break;
        }

        // Validate temperature vector: must be exact expected size and contain only finite numbers
        if (usable_field) {
            if (T.size() != stats.active_cells) {
                usable_field = false;
                status_str = "DIVERGED_NUMERICAL";
            } else {
                for (double val : T) {
                    if (!std::isfinite(val)) {
                        usable_field = false;
                        status_str = "DIVERGED_NUMERICAL";
                        break;
                    }
                }
            }
        }

        double max_analytical_error = -1.0;
        bool has_analytical_error = false;
        if (mtype == ModelType::CUBE && usable_field && T.size() == stats.active_cells) {
            max_analytical_error = geom.compute_max_error_cube(T);
            if (std::isfinite(max_analytical_error)) {
                has_analytical_error = true;
            }
        }

        double energy_balance_rel_err = -1.0;
        bool has_energy_balance_error = false;
        if (mtype == ModelType::FINNED_HEAT_SINK && usable_field && T.size() == stats.active_cells) {
            double Q_gen = 0.0, Q_base = 0.0, Q_conv = 0.0;
            bool balance_ok = geom.verify_energy_balance_finned(T, Q_gen, Q_base, Q_conv, energy_balance_rel_err);
            static_cast<void>(balance_ok);
            if (std::isfinite(energy_balance_rel_err)) {
                has_energy_balance_error = true;
            }
        }

        double min_T = 0.0, max_T = 0.0, avg_T = 0.0;
        bool has_temp_stats = false;
        if (usable_field && T.size() == stats.active_cells && !T.empty()) {
            min_T = T[0];
            max_T = T[0];
            double sum = 0.0;
            for (double v : T) {
                if (v < min_T) min_T = v;
                if (v > max_T) max_T = v;
                sum += v;
            }
            avg_T = sum / static_cast<double>(T.size());
            if (std::isfinite(min_T) && std::isfinite(max_T) && std::isfinite(avg_T)) {
                has_temp_stats = true;
            }
        }

        double assembly_ms = std::isfinite(assembly_time_ms) && assembly_time_ms >= 0.0 ? assembly_time_ms : 0.0;
        double setup_ms = std::isfinite(s_res.setup_time_ms) && s_res.setup_time_ms >= 0.0 ? s_res.setup_time_ms : 0.0;
        double solve_ms = std::isfinite(s_res.solve_time_ms) && s_res.solve_time_ms >= 0.0 ? s_res.solve_time_ms : 0.0;
        double conv_ms = std::isfinite(s_res.conversion_time_ms) && s_res.conversion_time_ms >= 0.0 ? s_res.conversion_time_ms : 0.0;
        double total_time_ms = assembly_ms + setup_ms + solve_ms + conv_ms;

        std::ostringstream ss;
        ss << std::setprecision(10);
        ss << "{"
           << "\"model\":\"" << model_str << "\","
           << "\"resolution\":" << N << ","
           << "\"solver\":\"" << escape_json(s_res.solver_name) << "\","
           << "\"mesh\":{"
           << "\"active_cells_M\":" << stats.active_cells << ","
           << "\"total_cells\":" << stats.total_cells << ","
           << "\"matrix_nnz\":" << stats.expected_nnz << ","
           << "\"cell_size_m\":" << stats.cell_size << ","
           << "\"solid_volume_m3\":" << stats.solid_volume
           << "},"
           << "\"metrics\":{"
           << "\"status\":\"" << status_str << "\","
           << "\"usable_field\":" << (usable_field ? "true" : "false") << ",";

        if (solver_str == "PROFILE_CHOLESKY") {
            ss << "\"iterations\":\"N/A\",";
            ss << "\"initial_residual\":null,";
        } else {
            ss << "\"iterations\":" << s_res.iterations << ",";
            if (std::isfinite(s_res.initial_residual)) {
                ss << "\"initial_residual\":" << s_res.initial_residual << ",";
            } else {
                ss << "\"initial_residual\":null,";
            }
        }

        if (std::isfinite(final_residual)) {
            ss << "\"final_residual\":" << final_residual << ",";
        } else {
            ss << "\"final_residual\":null,";
        }

        if (std::isfinite(relative_residual)) {
            ss << "\"relative_residual\":" << relative_residual << ",";
        } else {
            ss << "\"relative_residual\":null,";
        }

        if (has_analytical_error) {
            ss << "\"max_analytical_error_degC\":" << max_analytical_error << ",";
        } else {
            ss << "\"max_analytical_error_degC\":null,";
        }

        if (has_energy_balance_error) {
            ss << "\"energy_balance_rel_error\":" << energy_balance_rel_err;
        } else {
            ss << "\"energy_balance_rel_error\":null";
        }
        ss << "},"
           << "\"timings_ms\":{"
           << "\"assembly_time_ms\":" << assembly_ms << ","
           << "\"setup_time_ms\":" << setup_ms << ","
           << "\"solve_time_ms\":" << solve_ms << ","
           << "\"conversion_time_ms\":" << conv_ms << ","
           << "\"total_time_ms\":" << total_time_ms
           << "},"
           << "\"thermal_field\":{";

        if (usable_field && T.size() == stats.active_cells && has_temp_stats) {
            ss << "\"count\":" << stats.active_cells << ","
               << "\"min_temp_degC\":" << min_T << ","
               << "\"max_temp_degC\":" << max_T << ","
               << "\"avg_temp_degC\":" << avg_T << ","
               << "\"coordinates\":[";

            for (size_t i = 0; i < stats.active_cells; ++i) {
                if (i > 0) ss << ",";
                double x = 0.0, y = 0.0, z = 0.0;
                geom.get_cell_centroid(i, x, y, z);
                ss << "[" << x << "," << y << "," << z << "]";
            }
            ss << "],\"temperatures\":[";
            for (size_t i = 0; i < stats.active_cells; ++i) {
                if (i > 0) ss << ",";
                ss << T[i];
            }
            ss << "]}";
        } else {
            ss << "\"count\":0,"
               << "\"min_temp_degC\":null,"
               << "\"max_temp_degC\":null,"
               << "\"avg_temp_degC\":null,"
               << "\"coordinates\":[],"
               << "\"temperatures\":[]}";
        }
        ss << "}";

        resp.status_code = 200;
        resp.body = ss.str();
        return resp;

    } catch (const std::exception& e) {
        resp.status_code = 500;
        resp.body = "{\"error\":\"Solve failed with internal error: " + escape_json(e.what()) + "\",\"status\":500}";
        return resp;
    } catch (...) {
        resp.status_code = 500;
        resp.body = "{\"error\":\"Solve failed with unknown internal exception.\",\"status\":500}";
        return resp;
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

    // 3D Thermal Models
    if (req.path == "/api/thermal3d/models") {
        if (req.method != "GET") {
            resp.status_code = 405;
            resp.content_type = "application/json";
            resp.body = "{\"error\":\"Method Not Allowed. Use GET.\",\"status\":405}";
            return resp;
        }
        return handle_thermal3d_models(req);
    }

    // 3D Thermal Config
    if (req.path == "/api/thermal3d/config") {
        if (req.method != "GET") {
            resp.status_code = 405;
            resp.content_type = "application/json";
            resp.body = "{\"error\":\"Method Not Allowed. Use GET.\",\"status\":405}";
            return resp;
        }
        return handle_thermal3d_config(req);
    }

    // 3D Thermal Health
    if (req.path == "/api/thermal3d/health") {
        if (req.method != "GET") {
            resp.status_code = 405;
            resp.content_type = "application/json";
            resp.body = "{\"error\":\"Method Not Allowed. Use GET.\",\"status\":405}";
            return resp;
        }
        return handle_thermal3d_health(req);
    }

    // 3D Thermal Solve
    if (req.path == "/api/thermal3d/solve") {
        if (req.method != "POST") {
            resp.status_code = 405;
            resp.content_type = "application/json";
            resp.body = "{\"error\":\"Method Not Allowed. Use POST.\",\"status\":405}";
            return resp;
        }
        return handle_thermal3d_solve(req);
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

    // Shutdown (Requires valid session token; disabled in cloud/production)
    if (req.path == "/api/shutdown" && req.method == "POST") {
        const char* env_prod = std::getenv("WORKBENCH_ENV");
        const char* env_dis = std::getenv("DISABLE_SHUTDOWN");
        if ((env_prod && std::string(env_prod) == "production") || (env_dis && std::string(env_dis) == "1")) {
            resp.status_code = 403;
            resp.content_type = "application/json";
            resp.body = "{\"error\":\"Shutdown endpoint is disabled in cloud production deployment.\",\"status\":403}";
            return resp;
        }

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
