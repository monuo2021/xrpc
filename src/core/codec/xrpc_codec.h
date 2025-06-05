#ifndef XRPC_CODEC_H
#define XRPC_CODEC_H

#include "xrpc.pb.h"
#include <google/protobuf/message.h>
#include <string>

namespace xrpc {
/***
协议
+----------+----------------+------------+
| Varint32 | header_str数据 | args_str数据 |
+----------+----------------+------------+
    ↑           ↑                 ↑
    |           |                 |
    长度头      实际头部数据      实际参数数据
***/

class XrpcCodec {
public:
    // 编码请求：header + args
    std::string Encode(const RpcHeader& header, const google::protobuf::Message& args);

    // 解码请求：返回 header 和 args
    bool Decode(const std::string& data, RpcHeader& header, std::string& args);

    // 编码响应：header + response
    std::string EncodeResponse(const RpcHeader& header, const google::protobuf::Message& response);

    // 解码响应：返回 header 和 response
    bool DecodeResponse(const std::string& data, RpcHeader& header, google::protobuf::Message& response);

private:
    // 压缩和解压缩
    std::string Compress(const std::string& data);
    std::string Decompress(const std::string& data);
};

} // namespace xrpc

#endif // XRPC_CODEC_H