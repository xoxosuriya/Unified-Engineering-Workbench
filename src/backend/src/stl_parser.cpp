#include "stl_parser.hpp"
#include <cmath>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <iomanip>

namespace workbench {

bool Vec3::is_finite() const {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

static inline std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static double length(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

std::string StlParseResult::to_json() const {
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
    ss << "\"solid_name\":\"" << solid_name << "\",";
    ss << "\"facet_count\":" << facet_count << ",";
    ss << "\"file_size_bytes\":" << file_size_bytes << ",";
    if (std::isfinite(surface_area)) {
        ss << "\"surface_area\":" << surface_area << ",";
    } else {
        ss << "\"surface_area\":null,";
    }
    ss << "\"bounds\":{\"min\":[" << min_bound.x << "," << min_bound.y << "," << min_bound.z << "],"
       << "\"max\":[" << max_bound.x << "," << max_bound.y << "," << max_bound.z << "]},";
    
    // Flat vertex buffer: [x1,y1,z1, x2,y2,z2, x3,y3,z3, ...]
    ss << "\"vertices\":[";
    bool first = true;
    for (const auto& tri : triangles) {
        for (int i = 0; i < 3; ++i) {
            if (!first) ss << ",";
            ss << tri.vertices[i].x << "," << tri.vertices[i].y << "," << tri.vertices[i].z;
            first = false;
        }
    }
    ss << "],";

    // Flat normal buffer (1 normal per triangle or 3 duplicated per vertex)
    ss << "\"normals\":[";
    first = true;
    for (const auto& tri : triangles) {
        for (int i = 0; i < 3; ++i) {
            if (!first) ss << ",";
            ss << tri.normal.x << "," << tri.normal.y << "," << tri.normal.z;
            first = false;
        }
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

StlParseResult StlParser::parse_string(const std::string& content) {
    StlParseResult res;
    res.file_size_bytes = content.size();

    if (content.empty()) {
        res.success = false;
        res.error_message = "File is empty (0 bytes). An ASCII STL file must contain a 'solid ... endsolid' block.";
        return res;
    }

    if (content.size() > MAX_FILE_SIZE_BYTES) {
        res.success = false;
        res.error_message = "File exceeds maximum tested limit of 50 MB.";
        return res;
    }

    // Quick binary detection: check first 512 bytes for null characters
    size_t check_len = std::min(content.size(), size_t(512));
    for (size_t i = 0; i < check_len; ++i) {
        if (content[i] == '\0') {
            res.success = false;
            res.error_message = "Detected binary STL format. The Workbench requires an ASCII STL file according to Task B specifications.";
            return res;
        }
    }

    std::istringstream stream(content);
    std::string line;
    size_t line_num = 0;

    // Find first non-empty line
    bool found_solid = false;
    while (std::getline(stream, line)) {
        line_num++;
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        std::string lower = to_lower(trimmed);
        if (lower.rfind("solid", 0) == 0) {
            found_solid = true;
            if (trimmed.size() > 5) {
                res.solid_name = trim(trimmed.substr(5));
            } else {
                res.solid_name = "unnamed_solid";
            }
            break;
        } else {
            res.success = false;
            res.error_message = "Malformed STL: Expected 'solid <name>' on line " + std::to_string(line_num) + ", found: '" + trimmed + "'";
            return res;
        }
    }

    if (!found_solid) {
        res.success = false;
        res.error_message = "Malformed STL: File contains no 'solid' definition.";
        return res;
    }

    // State machine for facets
    enum State {
        WAIT_FACET,
        WAIT_OUTER_LOOP,
        WAIT_VERTEX_1,
        WAIT_VERTEX_2,
        WAIT_VERTEX_3,
        WAIT_ENDLOOP,
        WAIT_ENDFACET
    } state = WAIT_FACET;

    StlTriangle current_tri;
    int vertex_idx = 0;
    bool found_endsolid = false;

    res.min_bound = { 1e30,  1e30,  1e30};
    res.max_bound = {-1e30, -1e30, -1e30};

    while (std::getline(stream, line)) {
        line_num++;
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        std::string lower = to_lower(trimmed);

        if (lower.rfind("endsolid", 0) == 0) {
            found_endsolid = true;
            break;
        }

        switch (state) {
            case WAIT_FACET: {
                if (lower.rfind("facet normal", 0) == 0) {
                    std::string args = trimmed.substr(12);
                    std::string lower_args = to_lower(args);
                    if (lower_args.find("nan") != std::string::npos || lower_args.find("inf") != std::string::npos) {
                        res.success = false;
                        res.error_message = "Line " + std::to_string(line_num) + ": Normal vector contains NaN or Infinite value.";
                        return res;
                    }
                    std::istringstream ls(args);
                    double nx, ny, nz;
                    if (!(ls >> nx >> ny >> nz)) {
                        res.success = false;
                        res.error_message = "Line " + std::to_string(line_num) + ": Malformed 'facet normal' values.";
                        return res;
                    }
                    current_tri.normal = {nx, ny, nz};
                    if (!current_tri.normal.is_finite()) {
                        res.success = false;
                        res.error_message = "Line " + std::to_string(line_num) + ": Normal vector contains NaN or Infinite value.";
                        return res;
                    }
                    state = WAIT_OUTER_LOOP;
                } else {
                    res.success = false;
                    res.error_message = "Line " + std::to_string(line_num) + ": Expected 'facet normal' or 'endsolid', found: '" + trimmed + "'";
                    return res;
                }
                break;
            }

            case WAIT_OUTER_LOOP: {
                if (lower == "outer loop") {
                    state = WAIT_VERTEX_1;
                    vertex_idx = 0;
                } else {
                    res.success = false;
                    res.error_message = "Line " + std::to_string(line_num) + ": Expected 'outer loop', found: '" + trimmed + "'";
                    return res;
                }
                break;
            }

            case WAIT_VERTEX_1:
            case WAIT_VERTEX_2:
            case WAIT_VERTEX_3: {
                if (lower.rfind("vertex", 0) == 0) {
                    std::string args = trimmed.substr(6);
                    std::string lower_args = to_lower(args);
                    if (lower_args.find("nan") != std::string::npos || lower_args.find("inf") != std::string::npos) {
                        res.success = false;
                        res.error_message = "Line " + std::to_string(line_num) + ": Vertex coordinate contains NaN or Infinite value.";
                        return res;
                    }
                    std::istringstream ls(args);
                    double vx, vy, vz;
                    if (!(ls >> vx >> vy >> vz)) {
                        res.success = false;
                        res.error_message = "Line " + std::to_string(line_num) + ": Malformed 'vertex' coordinate values.";
                        return res;
                    }
                    Vec3 v{vx, vy, vz};
                    if (!v.is_finite() || std::abs(v.x) > 1e12 || std::abs(v.y) > 1e12 || std::abs(v.z) > 1e12) {
                        res.success = false;
                        res.error_message = "Line " + std::to_string(line_num) + ": Vertex coordinate is NaN, Inf, or exceeds physical range (|coord| > 1e12 m).";
                        return res;
                    }
                    current_tri.vertices[vertex_idx++] = v;

                    // Update bounding box
                    res.min_bound.x = std::min(res.min_bound.x, v.x);
                    res.min_bound.y = std::min(res.min_bound.y, v.y);
                    res.min_bound.z = std::min(res.min_bound.z, v.z);
                    res.max_bound.x = std::max(res.max_bound.x, v.x);
                    res.max_bound.y = std::max(res.max_bound.y, v.y);
                    res.max_bound.z = std::max(res.max_bound.z, v.z);

                    if (vertex_idx == 3) {
                        state = WAIT_ENDLOOP;
                    }
                } else {
                    res.success = false;
                    res.error_message = "Line " + std::to_string(line_num) + ": Expected 'vertex', found: '" + trimmed + "'";
                    return res;
                }
                break;
            }

            case WAIT_ENDLOOP: {
                if (lower == "endloop") {
                    state = WAIT_ENDFACET;
                } else {
                    res.success = false;
                    res.error_message = "Line " + std::to_string(line_num) + ": Expected 'endloop', found: '" + trimmed + "'";
                    return res;
                }
                break;
            }

            case WAIT_ENDFACET: {
                if (lower == "endfacet") {
                    // Compute area: 0.5 * |(v1 - v0) x (v2 - v0)|
                    Vec3 edge1 = {
                        current_tri.vertices[1].x - current_tri.vertices[0].x,
                        current_tri.vertices[1].y - current_tri.vertices[0].y,
                        current_tri.vertices[1].z - current_tri.vertices[0].z
                    };
                    Vec3 edge2 = {
                        current_tri.vertices[2].x - current_tri.vertices[0].x,
                        current_tri.vertices[2].y - current_tri.vertices[0].y,
                        current_tri.vertices[2].z - current_tri.vertices[0].z
                    };
                    Vec3 c = cross(edge1, edge2);
                    double tri_area = 0.5 * length(c);
                    if (!std::isfinite(tri_area)) {
                        res.success = false;
                        res.error_message = "Line " + std::to_string(line_num) + ": Facet area computation resulted in non-finite value.";
                        return res;
                    }
                    res.surface_area += tri_area;

                    // If normal is zero, calculate from cross product
                    if (length(current_tri.normal) < 1e-12 && tri_area > 1e-12) {
                        current_tri.normal = { c.x / (2.0 * tri_area), c.y / (2.0 * tri_area), c.z / (2.0 * tri_area) };
                    }

                    res.triangles.push_back(current_tri);
                    res.facet_count++;
                    state = WAIT_FACET;
                } else {
                    res.success = false;
                    res.error_message = "Line " + std::to_string(line_num) + ": Expected 'endfacet', found: '" + trimmed + "'";
                    return res;
                }
                break;
            }
        }
    }

    if (!found_endsolid) {
        res.success = false;
        res.error_message = "Malformed STL: File ended unexpectedly without 'endsolid'.";
        return res;
    }

    if (state != WAIT_FACET) {
        res.success = false;
        res.error_message = "Malformed STL: Incomplete facet definition at end of file.";
        return res;
    }

    if (res.triangles.empty()) {
        res.success = false;
        res.error_message = "STL file contains 0 facets. Valid geometry must contain at least one triangle facet.";
        return res;
    }

    res.success = true;
    return res;
}

StlParseResult StlParser::parse_file(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        StlParseResult res;
        res.success = false;
        res.error_message = "Failed to open file: " + file_path;
        return res;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > MAX_FILE_SIZE_BYTES) {
        StlParseResult res;
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
