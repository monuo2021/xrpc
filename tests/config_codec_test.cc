#include "core/common/xrpc_config.h"
#include "core/common/xrpc_logger.h"
#include "core/codec/xrpc_codec.h"
#include "xrpc.pb.h"
#include <google/protobuf/wrappers.pb.h>
#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>

TEST(XrpcConfigTest, LoadAndGet) {
    xrpc::XrpcConfig config;
    config.Load("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf");
    EXPECT_EQ(config.Get("zookeeper_ip"), "127.0.0.1");
    EXPECT_EQ(config.Get("zookeeper_port"), "2181");
    EXPECT_EQ(config.Get("zookeeper_timeout_ms"), "6000");
    EXPECT_EQ(config.Get("server_ip"), "0.0.0.0");
    EXPECT_EQ(config.Get("server_port"), "8080");
    EXPECT_EQ(config.Get("log_level"), "debug");
    EXPECT_EQ(config.Get("log_file"), "xrpc.log");
    EXPECT_EQ(config.Get("missing_key", "default"), "default");
}

TEST(XrpcCodecTest, EncodeAndDecodeNoCompression) {
    xrpc::XrpcCodec codec;
    xrpc::RpcHeader header;
    header.set_service_name("UserService");
    header.set_method_name("Login");
    header.set_request_id(12345);
    header.set_compressed(false);

    google::protobuf::StringValue args;
    args.set_value("test_args");

    std::string encoded = codec.Encode(header, args);

    xrpc::RpcHeader decoded_header;
    std::string decoded_args;
    ASSERT_TRUE(codec.Decode(encoded, decoded_header, decoded_args)) << "Decode failed";

    EXPECT_EQ(decoded_header.service_name(), "UserService");
    EXPECT_EQ(decoded_header.method_name(), "Login");
    EXPECT_EQ(decoded_header.request_id(), 12345);
    EXPECT_FALSE(decoded_header.compressed());

    google::protobuf::StringValue decoded_message;
    std::ostringstream oss;
    for (char c : decoded_args) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)c << " ";
    }
    ASSERT_TRUE(decoded_message.ParseFromString(decoded_args)) 
        << "Failed to parse decoded_args, size: " << decoded_args.size() << ", hex: " << oss.str();
    EXPECT_EQ(decoded_message.value(), "test_args");
}

TEST(XrpcCodecTest, EncodeAndDecodeWithCompressionSmallData) {
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
    ASSERT_TRUE(codec.Decode(encoded, decoded_header, decoded_args)) << "Decode failed";

    EXPECT_EQ(decoded_header.service_name(), "UserService");
    EXPECT_EQ(decoded_header.method_name(), "Login");
    EXPECT_EQ(decoded_header.request_id(), 12345);
    EXPECT_FALSE(decoded_header.compressed()); // 小数据应跳过压缩

    google::protobuf::StringValue decoded_message;
    std::ostringstream oss;
    for (char c : decoded_args) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)c << " ";
    }
    ASSERT_TRUE(decoded_message.ParseFromString(decoded_args)) 
        << "Failed to parse decoded_args, size: " << decoded_args.size() << ", hex: " << oss.str();
    EXPECT_EQ(decoded_message.value(), "test_args");
}

TEST(XrpcCodecTest, EncodeAndDecodeWithCompressionLargeData) {
    xrpc::XrpcCodec codec;
    xrpc::RpcHeader header;
    header.set_service_name("UserService");
    header.set_method_name("Login");
    header.set_request_id(12345);
    header.set_compressed(true);

    google::protobuf::StringValue args;
    args.set_value(std::string(1000, 'a')); // 1000 字节重复数据，易于压缩

    std::string encoded = codec.Encode(header, args);

    xrpc::RpcHeader decoded_header;
    std::string decoded_args;
    ASSERT_TRUE(codec.Decode(encoded, decoded_header, decoded_args)) << "Decode failed";

    EXPECT_EQ(decoded_header.service_name(), "UserService");
    EXPECT_EQ(decoded_header.method_name(), "Login");
    EXPECT_EQ(decoded_header.request_id(), 12345);
    EXPECT_TRUE(decoded_header.compressed()); // 大数据应启用压缩

    google::protobuf::StringValue decoded_message;
    std::ostringstream oss;
    for (char c : decoded_args) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)c << " ";
    }
    ASSERT_TRUE(decoded_message.ParseFromString(decoded_args)) 
        << "Failed to parse decoded_args, size: " << decoded_args.size() << ", hex: " << oss.str();
    EXPECT_EQ(decoded_message.value(), std::string(1000, 'a'));
}

TEST(XrpcCodecTest, EncodeAndDecodeResponseNoCompression) {
    xrpc::XrpcCodec codec;
    xrpc::RpcHeader header;
    header.set_service_name("UserService");
    header.set_method_name("Login");
    header.set_request_id(12345);
    header.set_status(0);
    header.set_compressed(false);

    google::protobuf::StringValue response;
    response.set_value("test_response");

    std::string encoded = codec.EncodeResponse(header, response);

    xrpc::RpcHeader decoded_header;
    google::protobuf::StringValue decoded_response;
    ASSERT_TRUE(codec.DecodeResponse(encoded, decoded_header, decoded_response)) << "Decode response failed";

    EXPECT_EQ(decoded_header.service_name(), "UserService");
    EXPECT_EQ(decoded_header.method_name(), "Login");
    EXPECT_EQ(decoded_header.request_id(), 12345);
    EXPECT_EQ(decoded_header.status(), 0);
    EXPECT_FALSE(decoded_header.compressed());
    EXPECT_EQ(decoded_response.value(), "test_response");
}

TEST(XrpcCodecTest, EncodeAndDecodeResponseWithError) {
    xrpc::XrpcCodec codec;
    xrpc::RpcHeader header;
    header.set_service_name("UserService");
    header.set_method_name("Login");
    header.set_request_id(12345);
    header.set_status(1);
    header.mutable_error()->set_code(5);
    header.mutable_error()->set_message("Invalid credentials");
    header.set_compressed(false);

    google::protobuf::StringValue response;
    response.set_value(""); // 错误场景下 response 可为空

    std::string encoded = codec.EncodeResponse(header, response);

    xrpc::RpcHeader decoded_header;
    google::protobuf::StringValue decoded_response;
    ASSERT_TRUE(codec.DecodeResponse(encoded, decoded_header, decoded_response)) << "Decode response failed";

    EXPECT_EQ(decoded_header.service_name(), "UserService");
    EXPECT_EQ(decoded_header.method_name(), "Login");
    EXPECT_EQ(decoded_header.request_id(), 12345);
    EXPECT_EQ(decoded_header.status(), 1);
    EXPECT_EQ(decoded_header.error().code(), 5);
    EXPECT_EQ(decoded_header.error().message(), "Invalid credentials");
    EXPECT_FALSE(decoded_header.compressed());
    EXPECT_EQ(decoded_response.value(), "");
}

TEST(XrpcCodecTest, EncodeAndDecodeResponseWithCompressionLargeData) {
    xrpc::XrpcCodec codec;
    xrpc::RpcHeader header;
    header.set_service_name("UserService");
    header.set_method_name("Login");
    header.set_request_id(12345);
    header.set_status(0);
    header.set_compressed(true);

    google::protobuf::StringValue response;
    response.set_value(std::string(1000, 'a')); // 大数据，启用压缩

    std::string encoded = codec.EncodeResponse(header, response);

    xrpc::RpcHeader decoded_header;
    google::protobuf::StringValue decoded_response;
    ASSERT_TRUE(codec.DecodeResponse(encoded, decoded_header, decoded_response)) << "Decode response failed";

    EXPECT_EQ(decoded_header.service_name(), "UserService");
    EXPECT_EQ(decoded_header.method_name(), "Login");
    EXPECT_EQ(decoded_header.request_id(), 12345);
    EXPECT_EQ(decoded_header.status(), 0);
    EXPECT_TRUE(decoded_header.compressed()); // 大数据应压缩
    EXPECT_EQ(decoded_response.value(), std::string(1000, 'a'));
}