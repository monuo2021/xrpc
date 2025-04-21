#include "core/common/xrpc_logger.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <stdexcept>

namespace xrpc {

void InitLogger(const std::string& file, LogLevel level) {
    try {
        // 创建文件和控制台输出
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        // 使用 initializer_list 传递 sinks
        spdlog::sinks_init_list sinks = {file_sink, console_sink};

        // 创建 logger
        auto logger = std::make_shared<spdlog::logger>("xrpc", sinks);
        
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

} // namespace xrpc