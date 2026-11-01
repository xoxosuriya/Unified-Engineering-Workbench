#include "ansys_parser.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iomanip>

namespace workbench {

static inline std::string trim_str(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static inline std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string AnsysParseResult::to_json() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);
    ss << "{";
    ss << "\"success\":" << (success ? "true" : "false") << ",";
    ss << "\"error_message\":\"";
    for (char c : error_message) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\n') ss << "\\n";
        else ss << c;
    }
    ss << "\",";
    ss << "\"is_synthetic\":" << (is_synthetic ? "true" : "false") << ",";
    ss << "\"node_count\":" << node_count << ",";
    ss << "\"file_size_bytes\":" << file_size_bytes << ",";
    ss << "\"min_temp_C\":" << min_temp_C << ",";
    ss << "\"max_temp_C\":" << max_temp_C << ",";
    ss << "\"avg_temp_C\":" << avg_temp_C << ",";
    ss << "\"bounds\":{\"min\":[" << min_bound.x << "," << min_bound.y << "," << min_bound.z << "],"
       << "\"max\":[" << max_bound.x << "," << max_bound.y << "," << max_bound.z << "]},";

    // Compact node arrays: node_ids, positions [x,y,z], temperatures
    ss << "\"node_ids\":[";
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i > 0) ss << ",";
        ss << nodes[i].node_id;
    }
    ss << "],";

    ss << "\"positions\":[";
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i > 0) ss << ",";
        ss << nodes[i].position.x << "," << nodes[i].position.y << "," << nodes[i].position.z;
    }
    ss << "],";

    ss << "\"temperatures\":[";
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i > 0) ss << ",";
        ss << nodes[i].temperature_C;
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

AnsysParseResult AnsysParser::parse_string(const std::string& content) {
    AnsysParseResult res;
    res.file_size_bytes = content.size();

    if (content.empty()) {
        res.success = false;
        res.error_message = "File is empty (0 bytes). Expected CSV format with columns: node_id,x_m,y_m,z_m,temperature_C.";
        return res;
    }

    if (content.size() > MAX_FILE_SIZE_BYTES) {
        res.success = false;
        res.error_message = "File exceeds maximum tested limit of 50 MB.";
        return res;
    }

    std::istringstream stream(content);
    std::string line;
    size_t line_num = 0;

    // Check header
    bool header_found = false;
    while (std::getline(stream, line)) {
        line_num++;
        std::string trimmed = trim_str(line);
        if (trimmed.empty()) continue;

        // Check if comment or metadata header
        if (trimmed[0] == '#') {
            if (to_lower_str(trimmed).find("synthetic") != std::string::npos) {
                res.is_synthetic = true;
            }
            continue;
        }

        std::string lower = to_lower_str(trimmed);
        // Expect header columns: node_id, x_m, y_m, z_m, temperature_c
        if (lower.find("node_id") != std::string::npos && 
            lower.find("x_m") != std::string::npos &&
            lower.find("y_m") != std::string::npos &&
            lower.find("z_m") != std::string::npos &&
            lower.find("temperature_c") != std::string::npos) {
            header_found = true;
            break;
        } else {
            res.success = false;
            res.error_message = "Line " + std::to_string(line_num) + 
                ": Invalid CSV header. Expected 'node_id,x_m,y_m,z_m,temperature_C', but found: '" + trimmed + "'";
            return res;
        }
    }

    if (!header_found) {
        res.success = false;
        res.error_message = "Malformed CSV: Header row missing or corrupted.";
        return res;
    }

    res.min_bound = { 1e30,  1e30,  1e30};
    res.max_bound = {-1e30, -1e30, -1e30};
    res.min_temp_C = 1e30;
    res.max_temp_C = -1e30;
    double sum_temp = 0.0;

    while (std::getline(stream, line)) {
        line_num++;
        std::string trimmed = trim_str(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        std::istringstream ls(trimmed);
        std::string token;
        std::vector<std::string> tokens;

        while (std::getline(ls, token, ',')) {
            tokens.push_back(trim_str(token));
        }

        if (tokens.size() != 5) {
            res.success = false;
            res.error_message = "Line " + std::to_string(line_num) + 
                ": Expected 5 comma-separated columns (node_id,x_m,y_m,z_m,temperature_C), found " + 
                std::to_string(tokens.size()) + " tokens.";
            return res;
        }

        AnsysNode node;
        try {
            node.node_id = std::stoll(tokens[0]);
            if (node.node_id <= 0) {
                res.success = false;
                res.error_message = "Line " + std::to_string(line_num) + ": node_id must be a positive integer, found: " + tokens[0];
                return res;
            }

            node.position.x = std::stod(tokens[1]);
            node.position.y = std::stod(tokens[2]);
            node.position.z = std::stod(tokens[3]);
            node.temperature_C = std::stod(tokens[4]);
        } catch (const std::exception& e) {
            res.success = false;
            res.error_message = "Line " + std::to_string(line_num) + ": Numerical parse error (" + e.what() + ") on line: '" + trimmed + "'";
            return res;
        }

        if (!node.position.is_finite()) {
            res.success = false;
            res.error_message = "Line " + std::to_string(line_num) + ": Node coordinates contain NaN or Infinite value.";
            return res;
        }

        if (!std::isfinite(node.temperature_C)) {
            res.success = false;
            res.error_message = "Line " + std::to_string(line_num) + ": Temperature value contains NaN or Infinite value.";
            return res;
        }

        if (node.temperature_C < -273.15) {
            res.success = false;
            res.error_message = "Line " + std::to_string(line_num) + ": Temperature (" + tokens[4] + " C) is below absolute zero (-273.15 C).";
            return res;
        }

        // Bounds and statistics
        res.min_bound.x = std::min(res.min_bound.x, node.position.x);
        res.min_bound.y = std::min(res.min_bound.y, node.position.y);
        res.min_bound.z = std::min(res.min_bound.z, node.position.z);
        res.max_bound.x = std::max(res.max_bound.x, node.position.x);
        res.max_bound.y = std::max(res.max_bound.y, node.position.y);
        res.max_bound.z = std::max(res.max_bound.z, node.position.z);

        res.min_temp_C = std::min(res.min_temp_C, node.temperature_C);
        res.max_temp_C = std::max(res.max_temp_C, node.temperature_C);
        sum_temp += node.temperature_C;

        res.nodes.push_back(node);
        res.node_count++;

        if (res.node_count > MAX_NODE_COUNT) {
            res.success = false;
            res.error_message = "File exceeds maximum tested capacity of " + std::to_string(MAX_NODE_COUNT) + " nodes.";
            return res;
        }
    }

    if (res.nodes.empty()) {
        res.success = false;
        res.error_message = "CSV contains a valid header but 0 data rows.";
        return res;
    }

    res.avg_temp_C = sum_temp / static_cast<double>(res.node_count);
    res.success = true;
    return res;
}

AnsysParseResult AnsysParser::parse_file(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        AnsysParseResult res;
        res.success = false;
        res.error_message = "Failed to open file: " + file_path;
        return res;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > MAX_FILE_SIZE_BYTES) {
        AnsysParseResult res;
        res.file_size_bytes = size;
        res.success = false;
        res.error_message = "File size (" + std::to_string(size) + " bytes) exceeds maximum limit of 50 MB.";
        return res;
    }

    std::string content(size, '\0');
    file.read(&content[0], size);
    return parse_string(content);
}

} // namespace workbench
