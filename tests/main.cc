#include "core/common/xrpc_logger.h"
#include <gtest/gtest.h>
#include <iostream>

int main(int argc, char** argv) {
    try {
        xrpc::InitLoggerFromConfig("../configs/xrpc.conf");
    } catch (const std::runtime_error& ex) {
        std::cerr << "Failed to initialize logger: " << ex.what() << std::endl;
        return 1;
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}