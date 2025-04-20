#ifndef XRPC_CONFIG_H
#define XRPC_CONFIG_H

#include <string>
#include <unordered_map>

namespace xrpc {

class XrpcConfig {
public:
    // 加载配置文件
    void Load(const std::string& file);

    // 获取配置项
    std::string Get(const std::string& key, const std::string& default_value = "") const;

private:
    std::unordered_map<std::string, std::string> config_map_;
};

} // namespace xrpc

#endif // XRPC_CONFIG_H