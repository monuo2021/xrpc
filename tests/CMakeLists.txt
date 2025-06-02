# 查找 Google Test
find_package(GTest REQUIRED)

# 收集测试文件
file(GLOB TEST_SRCS "*.cc")

# 创建测试可执行文件
add_executable(xrpc_tests ${TEST_SRCS})
target_link_libraries(xrpc_tests
    xrpc_core
    xrpc_proto
    GTest::GTest
    GTest::Main
    Boost::system
)

# 添加测试
add_test(NAME XrpcTests COMMAND xrpc_tests)