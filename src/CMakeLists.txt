# 收集源文件
file(GLOB CORE_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/registry/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport/*.cc"
)

# 创建 XRPC 核心库
add_library(xrpc_core STATIC ${CORE_SRCS})
target_link_libraries(xrpc_core
    xrpc_proto
    ${Protobuf_LIBRARIES}
    ${MUDUO_BASE}
    ${MUDUO_NET}
    ${ZOOKEEPER_MT}
    ${ZLIB_LIBRARIES}
)
target_include_directories(xrpc_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/protos
)