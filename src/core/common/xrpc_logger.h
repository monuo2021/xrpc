#ifndef XRPC_LOGGER_H
#define XRPC_LOGGER_H

#include <spdlog/spdlog.h>
#include <memory>

namespace xrpc {

// 日志级别
enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

// 初始化日志系统（指定文件和级别）
void InitLogger(const std::string& file, LogLevel level);

// 从配置文件初始化日志系统
void InitLoggerFromConfig(const std::string& config_file);

// 日志宏
#define XRPC_LOG_TRACE(...) SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::trace, __VA_ARGS__)
#define XRPC_LOG_DEBUG(...) SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::debug, __VA_ARGS__)
#define XRPC_LOG_INFO(...) SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::info, __VA_ARGS__)
#define XRPC_LOG_WARN(...) SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::warn, __VA_ARGS__)
#define XRPC_LOG_ERROR(...) SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::err, __VA_ARGS__)
#define XRPC_LOG_CRITICAL(...) SPDLOG_LOGGER_CALL(spdlog::default_logger(), spdlog::level::critical, __VA_ARGS__)

} // namespace xrpc

#endif // XRPC_LOGGER_H