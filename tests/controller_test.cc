#include "core/controller/xrpc_controller.h"
#include <gtest/gtest.h>

namespace xrpc {

TEST(XrpcControllerTest, ErrorHandling) {
    XrpcController ctrl;
    EXPECT_FALSE(ctrl.Failed());
    EXPECT_TRUE(ctrl.ErrorText().empty());

    ctrl.SetFailed("Test error");
    EXPECT_TRUE(ctrl.Failed());
    EXPECT_EQ(ctrl.ErrorText(), "Test error");

    ctrl.Reset();
    EXPECT_FALSE(ctrl.Failed());
    EXPECT_TRUE(ctrl.ErrorText().empty());
}

} // namespace xrpc
