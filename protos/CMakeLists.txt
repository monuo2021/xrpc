# 查找 Protoc 编译器
find_program(PROTOC protoc)

# 定义 Protobuf 文件
file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")
file(GLOB EXAMPLE_PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/example/*.proto")

# 生成 Protobuf 代码
foreach(PROTO_FILE ${PROTO_FILES} ${EXAMPLE_PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    get_filename_component(PROTO_PATH ${PROTO_FILE} PATH)
    set(PROTO_SRC "${CMAKE_BINARY_DIR}/protos/${PROTO_NAME}.pb.cc")
    set(PROTO_HDR "${CMAKE_BINARY_DIR}/protos/${PROTO_NAME}.pb.h")
    add_custom_command(
        OUTPUT ${PROTO_SRC} ${PROTO_HDR}
        COMMAND ${PROTOC}
        ARGS --cpp_out=${CMAKE_BINARY_DIR}/protos --proto_path=${PROTO_PATH} ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating Protobuf files for ${PROTO_NAME}"
    )
    list(APPEND PROTO_SRCS ${PROTO_SRC})
    list(APPEND PROTO_HDRS ${PROTO_HDR})
endforeach()

# 创建 Protobuf 库
add_library(xrpc_proto ${PROTO_SRCS})
target_link_libraries(xrpc_proto ${Protobuf_LIBRARIES})