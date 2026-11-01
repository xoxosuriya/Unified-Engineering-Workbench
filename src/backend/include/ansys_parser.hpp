#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "stl_parser.hpp" // For Vec3

namespace workbench {

struct AnsysNode {
    int64_t node_id = 0;
    Vec3 position;
    double temperature_C = 0.0;
};

struct AnsysParseResult {
    bool success = false;
    std::string error_message;
    bool is_synthetic = false;
    size_t node_count = 0;
    size_t file_size_bytes = 0;
    std::vector<AnsysNode> nodes;
    Vec3 min_bound;
    Vec3 max_bound;
    double min_temp_C = 0.0;
    double max_temp_C = 0.0;
    double avg_temp_C = 0.0;

    std::string to_json() const;
};

class AnsysParser {
public:
    static constexpr size_t MAX_FILE_SIZE_BYTES = 50 * 1024 * 1024; // 50 MB limit
    static constexpr size_t MAX_NODE_COUNT = 500000;

    static AnsysParseResult parse_string(const std::string& content);
    static AnsysParseResult parse_file(const std::string& file_path);
};

} // namespace workbench
