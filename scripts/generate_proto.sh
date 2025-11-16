#!/bin/bash
# Quick fix script to generate protobuf files and rebuild
# Use this if you already ran wsl_setup.sh but got protobuf errors

set -e

echo "Quick Fix: Generating Protobuf Files"
echo "====================================="

# Get project directory
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

# Check for protoc
if ! command -v protoc &> /dev/null; then
    echo "Error: protoc not found. Installing..."
    sudo apt-get update
    sudo apt-get install -y protobuf-compiler
fi

# Check for grpc_cpp_plugin
if ! command -v grpc_cpp_plugin &> /dev/null; then
    echo "Error: grpc_cpp_plugin not found. gRPC may not be installed correctly."
    echo "Run: ./scripts/install_grpc_only.sh"
    exit 1
fi

# Create directory for generated files
echo "Creating directories..."
mkdir -p src/cpp/common
mkdir -p src/python/common

# Generate C++ protobuf files
echo "Generating C++ protobuf files..."
protoc --proto_path=protos \
       --cpp_out=src/cpp/common \
       --grpc_out=src/cpp/common \
       --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
       protos/minitwo.proto

if [ -f "src/cpp/common/minitwo.pb.cc" ]; then
    echo "✓ minitwo.pb.cc created"
    echo "✓ minitwo.pb.h created"
    echo "✓ minitwo.grpc.pb.cc created"
    echo "✓ minitwo.grpc.pb.h created"
else
    echo "✗ Failed to generate C++ protobuf files"
    exit 1
fi

# Generate Python protobuf files
echo "Generating Python protobuf files..."
python3 -m grpc_tools.protoc --proto_path=protos \
        --python_out=src/python/common \
        --grpc_python_out=src/python/common \
        protos/minitwo.proto

echo ""
echo "Protobuf files generated successfully!"
echo ""
echo "Now rebuild the project:"
echo "  cd build"
echo "  rm -rf *"
echo "  cmake -DCMAKE_PREFIX_PATH=/usr/local .."
echo "  make -j\$(nproc)"
