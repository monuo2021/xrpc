#include "core/common/xrpc_logger.h"
#include "core/common/xrpc_config.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <stdexcept>
#include <algorithm>

namespace xrpc {

void InitLogger(const std::string& file, LogLevel level) {
    try {
        // 创建文件输出
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);

        // 创建 logger
        auto logger = std::make_shared<spdlog::logger>("xrpc", file_sink);
        
        // 设置日志级别
        switch (level) {
            case LogLevel::TRACE:
                logger->set_level(spdlog::level::trace);
                break;
            case LogLevel::DEBUG:
                logger->set_level(spdlog::level::debug);
                break;
            case LogLevel::INFO:
                logger->set_level(spdlog::level::info);
                break;
            case LogLevel::WARN:
                logger->set_level(spdlog::level::warn);
                break;
            case LogLevel::ERROR:
                logger->set_level(spdlog::level::err);
                break;
            case LogLevel::CRITICAL:
                logger->set_level(spdlog::level::critical);
                break;
        }

        // 设置默认 logger
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

        // 强制刷新 DEBUG 级别日志
        logger->flush_on(spdlog::level::debug);

        // 验证日志级别
        XRPC_LOG_INFO("Logger initialized with file: {} and level: {}", file, spdlog::level::to_string_view(logger->level()));
        XRPC_LOG_DEBUG("Debug logging enabled, testing output");
    } catch (const spdlog::spdlog_ex& ex) {
        throw std::runtime_error("Failed to initialize logger: " + std::string(ex.what()));
    }
}

void InitLoggerFromConfig(const std::string& config_file) {
    XrpcConfig config;
    try {
        config.Load(config_file);
    } catch (const std::runtime_error& ex) {
        throw std::runtime_error("Failed to load config file: " + std::string(ex.what()));
    }

    // 获取日志文件，默认为 xrpc.log
    std::string log_file = config.Get("log_file", "xrpc.log");

    // 获取日志级别，默认为 info
    std::string log_level_str = config.Get("log_level", "info");
    // 转换为小写以支持大小写混合输入
    std::transform(log_level_str.begin(), log_level_str.end(), log_level_str.begin(), ::tolower);

    LogLevel log_level;
    if (log_level_str == "trace") {
        log_level = LogLevel::TRACE;
    } else if (log_level_str == "debug") {
        log_level = LogLevel::DEBUG;
    } else if (log_level_str == "info") {
        log_level = LogLevel::INFO;
    } else if (log_level_str == "warn") {
        log_level = LogLevel::WARN;
    } else if (log_level_str == "error") {
        log_level = LogLevel::ERROR;
    } else if (log_level_str == "critical") {
        log_level = LogLevel::CRITICAL;
    } else {
        throw std::runtime_error("Invalid log_level in config: " + log_level_str);
    }

    InitLogger(log_file, log_level);
}

} // namespace xrpc