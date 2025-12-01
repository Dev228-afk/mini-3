#!/usr/bin/env bash
set -euo pipefail
PROTO=protos/minitwo.proto

# Generate C++
protoc -Iprotos --cpp_out=src/cpp/common --grpc_out=src/cpp/common --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) $PROTO

# Generate Python
python3 -m grpc_tools.protoc -Iprotos --python_out=src/python/common --grpc_python_out=src/python/common $PROTO

echo "Generated stubs for C++ and Python."
