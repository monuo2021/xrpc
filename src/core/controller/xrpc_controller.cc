#include "core/controller/xrpc_controller.h"
#include "core/common/xrpc_logger.h"
#include <mutex>

namespace xrpc {

XrpcController::XrpcController() : failed_(false), canceled_(false), cancel_callback_(nullptr) {}

XrpcController::~XrpcController() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancel_callback_ && !canceled_) {
        cancel_callback_->Run();
    }
}

void XrpcController::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    failed_ = false;
    error_text_.clear();
    canceled_ = false;
    cancel_callback_ = nullptr;
}

bool XrpcController::Failed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_;
}

std::string XrpcController::ErrorText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_text_;
}

void XrpcController::SetFailed(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    failed_ = true;
    error_text_ = reason;
    XRPC_LOG_ERROR("Request failed: {}", reason);
}

void XrpcController::StartCancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (canceled_) {
        return;
    }
    canceled_ = true;
    XRPC_LOG_INFO("Request canceled");
    if (cancel_callback_) {
        cancel_callback_->Run();
        cancel_callback_ = nullptr;
    }
}

bool XrpcController::IsCanceled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return canceled_;
}

void XrpcController::NotifyOnCancel(google::protobuf::Closure* callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_callback_ = callback;
    if (canceled_ && callback) {
        callback->Run();
        cancel_callback_ = nullptr;
    }
}

} // namespace xrpc