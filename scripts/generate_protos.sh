#!/bin/bash
mkdir -p build/protos/example
protoc -I=protos --cpp_out=build/protos protos/xrpc.proto
protoc -I=protos --cpp_out=build/protos protos/example/user_service.proto