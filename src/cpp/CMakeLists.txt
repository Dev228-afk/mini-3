
add_library(mini2_proto
    common/minitwo.pb.cc
    common/minitwo.grpc.pb.cc
)
target_include_directories(mini2_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(mini2_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)

add_library(mini2_common
    common/config.cpp
    common/config.h
)
target_include_directories(mini2_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(mini2_common PUBLIC mini2_proto)

add_library(mini2_processor
    server/RequestProcessor.cpp
    server/RequestProcessor.h
    server/SessionManager.cpp
    server/SessionManager.h
    server/DataProcessor.cpp
    server/DataProcessor.h
)
target_include_directories(mini2_processor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/server)
target_link_libraries(mini2_processor PUBLIC mini2_common mini2_proto gRPC::grpc++ protobuf::libprotobuf)

add_executable(mini2_server server/ServerMain.cpp server/Handlers.cpp)
target_link_libraries(mini2_server PRIVATE mini2_common mini2_proto mini2_processor gRPC::grpc++ protobuf::libprotobuf)

add_executable(mini2_client client/ClientMain.cpp)
target_link_libraries(mini2_client PRIVATE mini2_common mini2_proto gRPC::grpc++ protobuf::libprotobuf)

add_executable(cpp_unit_tests ../../tests/cpp_unit_tests.cpp)
target_link_libraries(cpp_unit_tests PRIVATE mini2_common mini2_proto gRPC::grpc++ protobuf::libprotobuf)
