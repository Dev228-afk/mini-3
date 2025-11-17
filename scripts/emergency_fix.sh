#!/bin/bash
# EMERGENCY FIX SCRIPT - Run on BOTH computers
# Fixes WSL networking and connectivity issues

echo "=== EMERGENCY FIX FOR MINI-2 ==="
echo ""

# IP addresses
CMP1_IP="192.168.137.189"
CMP2_IP="192.168.137.1"

echo "Expected IPs:"
echo "  Computer 1: $CMP1_IP"
echo "  Computer 2: $CMP2_IP"
echo ""

echo "Step 1: Navigate to project directory..."
cd ~/mini-2 2>/dev/null || cd /mnt/c/Users/*/mini-2 2>/dev/null || cd /home/*/dev/mini-2 2>/dev/null || {
    echo "ERROR: Cannot find mini-2 directory"
    exit 1
}
echo "Working in: $(pwd)"
echo ""

echo "Step 2: Fix config file with correct IPs..."
cat > config/network_setup.json << EOF
{
  "nodes": [
    {
      "id": "A",
      "role": "LEADER",
      "host": "$CMP1_IP",
      "port": 50050,
      "team": "GREEN"
    },
    {
      "id": "B",
      "role": "TEAM_LEADER",
      "host": "$CMP1_IP",
      "port": 50051,
      "team": "GREEN"
    },
    {
      "id": "C",
      "role": "WORKER",
      "host": "$CMP2_IP",
      "port": 50052,
      "team": "GREEN"
    },
    {
      "id": "D",
      "role": "TEAM_LEADER",
      "host": "$CMP1_IP",
      "port": 50053,
      "team": "PINK"
    },
    {
      "id": "E",
      "role": "TEAM_LEADER",
      "host": "$CMP2_IP",
      "port": 50054,
      "team": "PINK"
    },
    {
      "id": "F",
      "role": "WORKER",
      "host": "$CMP2_IP",
      "port": 50055,
      "team": "PINK"
    }
  ],
  "overlay": [
    ["A", "B"],
    ["B", "C"],
    ["B", "D"],
    ["A", "E"],
    ["E", "F"],
    ["E", "D"]
  ],
  "topology_mode": "hierarchical",
  "health_check": {
    "enabled": true,
    "interval_seconds": 10,
    "timeout_seconds": 5
  },
  "client_gateway": "A",
  "shared_memory": {
    "segments": [
      {
        "name": "shm_host1",
        "members": ["A", "B", "D"]
      },
      {
        "name": "shm_host2",
        "members": ["C", "E", "F"]
      }
    ],
    "fields": ["status", "queue_size", "last_ts_ms"]
  }
}
EOF

echo "✓ Config updated"
echo ""

echo "Step 3: Validate config JSON..."
if command -v python3 &> /dev/null; then
    python3 -c "import json; json.load(open('config/network_setup.json'))" && echo "✓ Config is valid JSON" || echo "✗ Config has JSON errors!"
fi
echo ""

echo "Step 4: Kill old servers..."
pkill -9 mini2_server 2>/dev/null || true
sleep 2
echo "✓ Old servers killed"
echo ""

echo "Step 5: Clean shared memory..."
rm -f /dev/shm/shm_host* 2>/dev/null || true
echo "✓ Shared memory cleaned"
echo ""

echo "Step 6: Rebuild with fix..."
cd build
make -j$(nproc) 2>&1 | tail -20
if [ $? -eq 0 ]; then
    echo "✓ Build complete"
else
    echo "✗ Build failed - check errors above"
    exit 1
fi
cd ..
echo ""

echo "Step 7: Create logs directory..."
mkdir -p logs
echo "✓ Logs directory ready"
echo ""

echo "Step 8: Detect which computer we're on..."
MY_IP=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
if [ -z "$MY_IP" ]; then
    MY_IP=$(hostname -I | awk '{print $1}')
fi
echo "My WSL IP: $MY_IP"

if [[ "$MY_IP" =~ ^172\. ]]; then
    echo "✓ WSL IP detected"
else
    echo "⚠ Warning: Unusual IP format"
fi
echo ""

echo "=== READY TO START SERVERS ==="
echo ""
echo "Copy and paste these commands:"
echo ""
echo "# Computer 1 ($CMP1_IP) - Run these:"
echo "./build/src/cpp/mini2_server A > logs/server_A.log 2>&1 &"
echo "sleep 2"
echo "./build/src/cpp/mini2_server B > logs/server_B.log 2>&1 &"
echo "sleep 2"
echo "./build/src/cpp/mini2_server D > logs/server_D.log 2>&1 &"
echo "sleep 3"
echo "ps aux | grep mini2_server | grep -v grep"
echo "echo 'If you see 3 processes above, servers started OK!'"
echo "tail -10 logs/server_A.log"
echo ""
echo "# Computer 2 ($CMP2_IP) - Run these:"
echo "./build/src/cpp/mini2_server C > logs/server_C.log 2>&1 &"
echo "sleep 2"
echo "./build/src/cpp/mini2_server E > logs/server_E.log 2>&1 &"
echo "sleep 2"
echo "./build/src/cpp/mini2_server F > logs/server_F.log 2>&1 &"
echo "sleep 3"
echo "ps aux | grep mini2_server | grep -v grep"
echo "echo 'If you see 3 processes above, servers started OK!'"
echo "tail -10 logs/server_C.log"
echo ""
echo "After BOTH computers have servers running, wait 20 seconds then test:"
echo "./build/src/cpp/mini2_client --server $CMP1_IP:50050 --query 'hello test'"
echo ""
echo "If servers crash, check logs:"
echo "tail -50 logs/server_A.log"
