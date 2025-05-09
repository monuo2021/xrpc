#include "core/codec/xrpc_codec.h"
#include "core/common/xrpc_logger.h"
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <zlib.h>
#include <stdexcept>

namespace xrpc {

std::string XrpcCodec::Encode(const RpcHeader& header, const google::protobuf::Message& args) {
    std::string args_str;
    if (!args.SerializeToString(&args_str)) {
        XRPC_LOG_ERROR("Failed to serialize args");
        throw std::runtime_error("Failed to serialize args");
    }

    RpcHeader mutable_header = header;
    mutable_header.set_args_size(args_str.size());

    if (mutable_header.compressed() && args_str.size() > 100) {
        std::string compressed_args = Compress(args_str);
        if (compressed_args.size() < args_str.size()) {
            XRPC_LOG_DEBUG("Compressed args from {} to {} bytes", args_str.size(), compressed_args.size());
            mutable_header.set_args_size(compressed_args.size());
            args_str = compressed_args;
        } else {
            mutable_header.set_compressed(false);
            XRPC_LOG_DEBUG("Skipped compression: compressed size {} >= original size {}", 
                           compressed_args.size(), args_str.size());
        }
    } else if (mutable_header.compressed()) {
        mutable_header.set_compressed(false);
        XRPC_LOG_DEBUG("Skipped compression: data size {} too small", args_str.size());
    }

    std::string header_str;
    if (!mutable_header.SerializeToString(&header_str)) {
        XRPC_LOG_ERROR("Failed to serialize RpcHeader");
        throw std::runtime_error("Failed to serialize RpcHeader");
    }

    std::string result;
    google::protobuf::io::StringOutputStream output(&result);
    google::protobuf::io::CodedOutputStream coded_output(&output);
    coded_output.WriteVarint32(header_str.size());
    coded_output.WriteString(header_str);
    coded_output.WriteString(args_str);

    XRPC_LOG_DEBUG("Encoded data: header_size={}, header_bytes={}, args_bytes={}",
                   header_str.size(), header_str.size(), args_str.size());
    return result;
}

bool XrpcCodec::Decode(const std::string& data, RpcHeader& header, std::string& args) {
    if (data.empty()) {
        XRPC_LOG_ERROR("Empty data received");
        return false;
    }

    google::protobuf::io::ArrayInputStream input(data.data(), data.size());
    google::protobuf::io::CodedInputStream coded_input(&input);
    coded_input.SetTotalBytesLimit(64 * 1024 * 1024); // 64MB 限制

    uint32_t header_size = 0;
    if (!coded_input.ReadVarint32(&header_size)) {
        XRPC_LOG_ERROR("Failed to read header size: invalid varint");
        return false;
    }

    if (header_size == 0 || header_size > data.size()) {
        XRPC_LOG_ERROR("Invalid header size: {}", header_size);
        return false;
    }

    std::string header_str;
    if (!coded_input.ReadString(&header_str, header_size)) {
        XRPC_LOG_ERROR("Failed to read header: expected {} bytes", header_size);
        return false;
    }

    if (!header.ParseFromString(header_str)) {
        XRPC_LOG_ERROR("Failed to parse RpcHeader");
        return false;
    }

    uint32_t args_size = header.args_size();
    if (args_size > data.size() - coded_input.CurrentPosition()) {
        XRPC_LOG_ERROR("Invalid args size: {} exceeds remaining data", args_size);
        return false;
    }

    args.clear();
    if (!coded_input.ReadString(&args, args_size)) {
        XRPC_LOG_ERROR("Failed to read args: expected {} bytes", args_size);
        return false;
    }

    if (header.compressed()) {
        try {
            args = Decompress(args);
            XRPC_LOG_DEBUG("Decompressed args to {} bytes", args.size());
        } catch (const std::runtime_error& e) {
            XRPC_LOG_ERROR("Decompression failed: {}", e.what());
            return false;
        }
    }

    XRPC_LOG_DEBUG("Decoded data: header_size={}, args_size={}, compressed={}",
                   header_size, args.size(), header.compressed());
    return true;
}

std::string XrpcCodec::EncodeResponse(const google::protobuf::Message& response) {
    std::string response_str;
    if (!response.SerializeToString(&response_str)) {
        XRPC_LOG_ERROR("Failed to serialize response");
        throw std::runtime_error("Failed to serialize response");
    }
    return response_str;
}

bool XrpcCodec::DecodeResponse(const std::string& data, google::protobuf::Message& response) {
    if (!response.ParseFromString(data)) {
        XRPC_LOG_ERROR("Failed to parse response");
        return false;
    }
    return true;
}

std::string XrpcCodec::Compress(const std::string& data) {
    z_stream stream = {};
    if (deflateInit(&stream, Z_BEST_SPEED) != Z_OK) {
        XRPC_LOG_ERROR("Failed to initialize deflate");
        throw std::runtime_error("Failed to initialize deflate");
    }

    std::string result;
    stream.next_in = (Bytef*)data.data();
    stream.avail_in = data.size();

    char buffer[8192];
    do {
        stream.next_out = (Bytef*)buffer;
        stream.avail_out = sizeof(buffer);
        int ret = deflate(&stream, Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&stream);
            XRPC_LOG_ERROR("Failed to compress data");
            throw std::runtime_error("Failed to compress data");
        }
        result.append(buffer, sizeof(buffer) - stream.avail_out);
    } while (stream.avail_out == 0);

    deflateEnd(&stream);
    XRPC_LOG_DEBUG("Compressed data from {} to {} bytes", data.size(), result.size());
    return result;
}

std::string XrpcCodec::Decompress(const std::string& data) {
    z_stream stream = {};
    if (inflateInit(&stream) != Z_OK) {
        XRPC_LOG_ERROR("Failed to initialize inflate");
        throw std::runtime_error("Failed to initialize inflate");
    }

    std::string result;
    stream.next_in = (Bytef*)data.data();
    stream.avail_in = data.size();

    char buffer[8192];
    do {
        stream.next_out = (Bytef*)buffer;
        stream.avail_out = sizeof(buffer);
        int ret = inflate(&stream, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR) {
            inflateEnd(&stream);
            XRPC_LOG_ERROR("Failed to decompress data");
            throw std::runtime_error("Failed to decompress data");
        }
        result.append(buffer, sizeof(buffer) - stream.avail_out);
    } while (stream.avail_out == 0);

    inflateEnd(&stream);
    XRPC_LOG_DEBUG("Decompressed data from {} to {} bytes", data.size(), result.size());
    return result;
}

} // namespace xrpc