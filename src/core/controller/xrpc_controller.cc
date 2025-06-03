#include "core/controller/xrpc_controller.h"
#include "core/common/xrpc_logger.h"

namespace xrpc {

XrpcController::XrpcController() : failed_(false), canceled_(false), cancel_callback_(nullptr) {}

void XrpcController::Reset() {
    failed_ = false;
    error_text_.clear();
    canceled_ = false;
    cancel_callback_ = nullptr;
}

bool XrpcController::Failed() const {
    return failed_;
}

std::string XrpcController::ErrorText() const {
    return error_text_;
}

void XrpcController::SetFailed(const std::string& reason) {
    failed_ = true;
    error_text_ = reason;
    XRPC_LOG_ERROR("Request failed: {}", reason);
}

void XrpcController::StartCancel() {
    canceled_ = true;
    if (cancel_callback_) {
        cancel_callback_->Run();
    }
    XRPC_LOG_INFO("Request canceled");
}

bool XrpcController::IsCanceled() const {
    return canceled_;
}

void XrpcController::NotifyOnCancel(google::protobuf::Closure* callback) {
    cancel_callback_ = callback;
    if (canceled_ && callback) {
        callback->Run();
    }
}

} // namespace xrpc