#include "core/common/xrpc_config.h"
#include "core/common/xrpc_logger.h"
#include "core/codec/xrpc_codec.h"
#include "xrpc.pb.h"
#include <google/protobuf/wrappers.pb.h>
#include <gtest/gtest.h>

TEST(XrpcConfigTest, LoadAndGet) {
    xrpc::XrpcConfig config;
    config.Load("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf"); // 使用绝对路径
    EXPECT_EQ(config.Get("zookeeper_ip"), "127.0.0.1");
    EXPECT_EQ(config.Get("log_level"), "info");
    EXPECT_EQ(config.Get("missing_key", "default"), "default");
}

TEST(XrpcCodecTest, EncodeAndDecode) {
    xrpc::XrpcCodec codec;
    xrpc::RpcHeader header;
    header.set_service_name("UserService");
    header.set_method_name("Login");
    header.set_request_id(12345);
    header.set_compressed(true);

    google::protobuf::StringValue args;
    args.set_value("test_args");

    std::string encoded = codec.Encode(header, args);

    xrpc::RpcHeader decoded_header;
    std::string decoded_args;
    ASSERT_TRUE(codec.Decode(encoded, decoded_header, decoded_args));

    EXPECT_EQ(decoded_header.service_name(), "UserService");
    EXPECT_EQ(decoded_header.method_name(), "Login");
    EXPECT_EQ(decoded_header.request_id(), 12345);
    EXPECT_TRUE(decoded_header.compressed());

    google::protobuf::StringValue decoded_message;
    ASSERT_TRUE(decoded_message.ParseFromString(decoded_args)) << "Failed to parse decoded_args, size: " << decoded_args.size();
    EXPECT_EQ(decoded_message.value(), "test_args");
}

int main(int argc, char** argv) {
    xrpc::InitLogger("test.log", xrpc::LogLevel::DEBUG); // 改为 DEBUG 级别
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}