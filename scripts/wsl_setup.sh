#!/bin/bash
# WSL Setup Script for Mini-2 Project
# Installs all dependencies and builds the project on Windows WSL

set -e  # Exit on error

echo "=========================================="
echo "Mini-2 Project - WSL Setup Script"
echo "=========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running on WSL
if ! grep -q microsoft /proc/version; then
    echo -e "${YELLOW}Warning: This doesn't appear to be WSL. Continue anyway? (y/n)${NC}"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo -e "${GREEN}Step 1: Updating package lists...${NC}"
sudo apt-get update

echo ""
echo -e "${GREEN}Step 2: Installing build essentials...${NC}"
sudo apt-get install -y \
    build-essential \
    autoconf \
    libtool \
    pkg-config \
    cmake \
    git \
    wget \
    unzip

echo ""
echo -e "${GREEN}Step 3: Installing gRPC dependencies...${NC}"
sudo apt-get install -y \
    libssl-dev \
    zlib1g-dev \
    libprotobuf-dev \
    protobuf-compiler

echo ""
echo -e "${GREEN}Step 4: Checking for existing gRPC installation...${NC}"
if [ -d "/usr/local/lib/cmake/grpc" ] || [ -f "/usr/local/lib/libgrpc++.so" ]; then
    echo -e "${YELLOW}gRPC appears to be already installed. Skip reinstall? (y/n)${NC}"
    read -r skip_grpc
else
    skip_grpc="n"
fi

if [[ ! "$skip_grpc" =~ ^[Yy]$ ]]; then
    echo ""
    echo -e "${GREEN}Step 5: Installing gRPC from source (this takes 10-15 minutes)...${NC}"
    
    # Create temp directory
    TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"
    
    echo "Cloning gRPC repository..."
    git clone --recurse-submodules -b v1.54.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc
    cd grpc
    
    echo "Building gRPC..."
    mkdir -p cmake/build
    cd cmake/build
    
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          -DCMAKE_BUILD_TYPE=Release \
          ../..
    
    echo "Compiling gRPC (this will take several minutes)..."
    make -j$(nproc)
    
    echo "Installing gRPC..."
    sudo make install
    sudo ldconfig
    
    # Cleanup
    cd ~
    rm -rf "$TEMP_DIR"
    
    echo -e "${GREEN}gRPC installed successfully!${NC}"
else
    echo -e "${YELLOW}Skipping gRPC installation.${NC}"
fi

echo ""
echo -e "${GREEN}Step 6: Installing Python dependencies...${NC}"
sudo apt-get install -y \
    python3 \
    python3-pip

pip3 install grpcio grpcio-tools

echo ""
echo -e "${GREEN}Step 7: Verifying installations...${NC}"
echo -n "CMake version: "
cmake --version | head -n 1

echo -n "g++ version: "
g++ --version | head -n 1

echo -n "Python3 version: "
python3 --version

echo -n "protoc version: "
protoc --version

if [ -f "/usr/local/lib/libgrpc++.so" ]; then
    echo -e "${GREEN}✓ gRPC library found${NC}"
else
    echo -e "${RED}✗ gRPC library NOT found${NC}"
fi

if [ -d "/usr/local/lib/cmake/grpc" ]; then
    echo -e "${GREEN}✓ gRPC CMake config found${NC}"
else
    echo -e "${RED}✗ gRPC CMake config NOT found${NC}"
fi

echo ""
echo -e "${GREEN}Step 8: Setting up project directory...${NC}"

# Get the project directory (assuming script is in mini-2/scripts/)
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "Project directory: $PROJECT_DIR"

cd "$PROJECT_DIR"

# Generate protobuf files
echo ""
echo -e "${GREEN}Step 9: Generating protobuf files...${NC}"
cd "$PROJECT_DIR"

# Create directory for generated files if it doesn't exist
mkdir -p src/cpp/common

# Generate C++ protobuf files
echo "Generating C++ protobuf files..."
protoc --proto_path=protos \
       --cpp_out=src/cpp/common \
       --grpc_out=src/cpp/common \
       --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
       protos/minitwo.proto

if [ -f "src/cpp/common/minitwo.pb.cc" ]; then
    echo -e "${GREEN}✓ C++ protobuf files generated successfully${NC}"
else
    echo -e "${RED}✗ Failed to generate C++ protobuf files${NC}"
    exit 1
fi

# Generate Python protobuf files (optional)
if [ -d "src/python" ]; then
    echo "Generating Python protobuf files..."
    mkdir -p src/python/common
    python3 -m grpc_tools.protoc --proto_path=protos \
            --python_out=src/python/common \
            --grpc_python_out=src/python/common \
            protos/minitwo.proto
    echo -e "${GREEN}✓ Python protobuf files generated${NC}"
fi

# Generate test data
echo ""
echo -e "${GREEN}Step 10: Generating test data...${NC}"
if [ -f "test_data/data.py" ]; then
    cd test_data
    python3 data.py
    cd "$PROJECT_DIR"
    echo -e "${GREEN}Test data generated successfully!${NC}"
else
    echo -e "${YELLOW}data.py not found, skipping test data generation${NC}"
fi

# Build the project
echo ""
echo -e "${GREEN}Step 11: Building the project...${NC}"
mkdir -p build
cd build

# Clean previous build if exists
if [ -f "CMakeCache.txt" ]; then
    echo "Cleaning previous build..."
    rm -rf *
fi

echo "Running CMake..."
cmake -DCMAKE_PREFIX_PATH=/usr/local ..

echo "Compiling project..."
make -j$(nproc)

echo ""
echo -e "${GREEN}=========================================="
echo "Setup Complete! ✓"
echo "==========================================${NC}"
echo ""
echo "Binaries created:"
echo "  - build/src/cpp/mini2_server"
echo "  - build/src/cpp/mini2_client"
echo "  - build/src/cpp/inspect_shm"
echo ""
echo "Next steps:"
echo "1. Update config/network_setup.json with your actual IP addresses"
echo "2. On Computer 1: ./scripts/start_computer1.sh"
echo "3. On Computer 2: ./scripts/start_computer2.sh"
echo "4. Test: ./build/src/cpp/mini2_client --server <IP>:50050 --query \"test\""
echo ""
echo -e "${GREEN}Happy testing!${NC}"
