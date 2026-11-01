#pragma once

#include <string>
#include <vector>
#include <array>
#include <sstream>

namespace workbench {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool is_finite() const;
};

struct StlTriangle {
    Vec3 normal;
    std::array<Vec3, 3> vertices;
};

struct StlParseResult {
    bool success = false;
    std::string error_message;
    std::string solid_name;
    std::vector<StlTriangle> triangles;
    Vec3 min_bound;
    Vec3 max_bound;
    double surface_area = 0.0;
    size_t facet_count = 0;
    size_t file_size_bytes = 0;

    std::string to_json() const;
};

class StlParser {
public:
    static constexpr size_t MAX_FILE_SIZE_BYTES = 50 * 1024 * 1024; // 50 MB tested limit

    // Parse ASCII STL from memory string or stream
    static StlParseResult parse_string(const std::string& content);
    
    // Parse ASCII STL from file path
    static StlParseResult parse_file(const std::string& file_path);
};

} // namespace workbench
