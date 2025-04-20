#include "core/common/xrpc_config.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace xrpc {

void XrpcConfig::Load(const std::string& file) {
    std::ifstream in(file);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open config file: " + file);
    }

    std::string line;
    while (std::getline(in, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // 去除首尾空白
        size_t start = line.find_first_not_of(" \t");
        size_t end = line.find_last_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        line = line.substr(start, end - start + 1);

        // 解析 key=value
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // 去除 key 和 value 中的空白
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        if (!key.empty()) {
            config_map_[key] = value;
        }
    }
    in.close();
}

std::string XrpcConfig::Get(const std::string& key, const std::string& default_value) const {
    auto it = config_map_.find(key);
    return it != config_map_.end() ? it->second : default_value;
}

} // namespace xrpc