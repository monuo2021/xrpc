# 客户端示例
add_executable(user_client client/user_client.cc)
target_link_libraries(user_client xrpc_core xrpc_proto)

# 服务端示例
add_executable(user_server server/user_server.cc)
target_link_libraries(user_server xrpc_core xrpc_proto)

# 安装可执行文件
install(TARGETS user_client user_server DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)