#include <gtest/gtest.h>
#include "core/controller/xrpc_controller.h"

namespace xrpc {

class ControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        controller_ = std::make_unique<XrpcController>();
    }

    std::unique_ptr<XrpcController> controller_;
};

TEST_F(ControllerTest, InitialState) {
    EXPECT_FALSE(controller_->Failed());
    EXPECT_TRUE(controller_->ErrorText().empty());
    EXPECT_FALSE(controller_->IsCanceled());
}

TEST_F(ControllerTest, SetFailed) {
    controller_->SetFailed("Request timeout");
    EXPECT_TRUE(controller_->Failed());
    EXPECT_EQ(controller_->ErrorText(), "Request timeout");
}

TEST_F(ControllerTest, Reset) {
    controller_->SetFailed("Request timeout");
    controller_->Reset();
    EXPECT_FALSE(controller_->Failed());
    EXPECT_TRUE(controller_->ErrorText().empty());
}

TEST_F(ControllerTest, Cancel) {
    controller_->StartCancel();
    EXPECT_TRUE(controller_->IsCanceled());
}

} // namespace xrpc