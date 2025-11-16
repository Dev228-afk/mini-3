#!/bin/bash
# EMERGENCY FIX SCRIPT - Run on BOTH computers
# Fixes WSL networking and connectivity issues

echo "=== EMERGENCY FIX FOR MINI-2 ==="
echo ""

# Computer IPs
CMP1_IP="172.26.247.68"
CMP2_IP="172.22.223.237"

echo "Step 1: Fix config file..."
cd ~/mini-2 || cd /mnt/c/Users/*/mini-2 || cd /home/*/dev/mini-2

cat > config/network_setup.json << 'EOF'
{
  "nodes": [
    {"id": "A", "host": "172.26.247.68", "port": 50050, "role": "gateway"},
    {"id": "B", "host": "172.26.247.68", "port": 50051, "role": "team_leader", "team": "GREEN"},
    {"id": "C", "host": "172.22.223.237", "port": 50052, "role": "worker", "team": "GREEN"},
    {"id": "D", "host": "172.26.247.68", "port": 50053, "role": "team_leader", "team": "PINK"},
    {"id": "E", "host": "172.22.223.237", "port": 50054, "role": "team_leader", "team": "PINK"},
    {"id": "F", "host": "172.22.223.237", "port": 50055, "role": "worker", "team": "PINK"}
  ],
  "shared_memory": {
    "segments": [
      {"name": "shm_host1", "members": ["A", "B", "D"]},
      {"name": "shm_host2", "members": ["C", "E", "F"]}
    ]
  }
}
EOF

echo "✓ Config updated"

echo ""
echo "Step 2: Kill old servers..."
pkill -9 mini2_server 2>/dev/null || true
sleep 2
echo "✓ Old servers killed"

echo ""
echo "Step 3: Clean shared memory..."
rm -f /dev/shm/shm_host* 2>/dev/null || true
echo "✓ Shared memory cleaned"

echo ""
echo "Step 4: Rebuild with fix..."
cd build
make -j$(nproc)
echo "✓ Build complete"

echo ""
echo "Step 5: Create logs directory..."
cd ..
mkdir -p logs
echo "✓ Logs directory ready"

echo ""
echo "=== READY TO START SERVERS ==="
echo ""
echo "Determine which computer you are on:"
echo ""
echo "Computer 1 (172.26.247.68) - Run these commands:"
echo "  ./build/src/cpp/mini2_server A > logs/server_A.log 2>&1 &"
echo "  ./build/src/cpp/mini2_server B > logs/server_B.log 2>&1 &"
echo "  ./build/src/cpp/mini2_server D > logs/server_D.log 2>&1 &"
echo "  echo 'Servers A, B, D started'"
echo ""
echo "Computer 2 (172.22.223.237) - Run these commands:"
echo "  ./build/src/cpp/mini2_server C > logs/server_C.log 2>&1 &"
echo "  ./build/src/cpp/mini2_server E > logs/server_E.log 2>&1 &"
echo "  ./build/src/cpp/mini2_server F > logs/server_F.log 2>&1 &"
echo "  echo 'Servers C, E, F started'"
echo ""
echo "Then wait 20 seconds and test:"
echo "  ./build/src/cpp/mini2_client --server 172.26.247.68:50050 --query \"test\" --need-green"
