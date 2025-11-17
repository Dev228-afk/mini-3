#!/bin/bash
# FINAL FIX - Guaranteed to work
# Run on BOTH computers

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== MINI-2 FINAL FIX ===${NC}"
echo ""

# Find project
for dir in ~/mini-2 /home/*/dev/mini-2 /mnt/c/Users/*/mini-2; do
    if [ -d "$dir" ]; then
        cd "$dir"
        break
    fi
done

if [ ! -f "config/network_setup.json" ]; then
    echo -e "${RED}ERROR: Cannot find project directory${NC}"
    exit 1
fi

echo "Working in: $(pwd)"
echo ""

# IPs
# Computer IPs
CMP1="192.168.137.110"
CMP2="192.168.137.1"

echo -e "${YELLOW}Step 1: KILL ALL SERVERS${NC}"
pkill -9 mini2_server 2>/dev/null && echo "Killed old servers" || echo "No servers running"
sleep 3

echo ""
echo -e "${YELLOW}Step 2: CLEAN EVERYTHING${NC}"
rm -f /dev/shm/shm_* 2>/dev/null && echo "Cleaned shared memory" || true

echo ""
echo -e "${YELLOW}Step 3: REWRITE CONFIG${NC}"
cat > config/network_setup.json <<EOF
{
  "nodes": [
    {"id": "A", "role": "LEADER", "host": "$CMP1", "port": 50050, "team": "GREEN"},
    {"id": "B", "role": "TEAM_LEADER", "host": "$CMP1", "port": 50051, "team": "GREEN"},
    {"id": "C", "role": "WORKER", "host": "$CMP2", "port": 50052, "team": "GREEN"},
    {"id": "D", "role": "TEAM_LEADER", "host": "$CMP1", "port": 50053, "team": "PINK"},
    {"id": "E", "role": "TEAM_LEADER", "host": "$CMP2", "port": 50054, "team": "PINK"},
    {"id": "F", "role": "WORKER", "host": "$CMP2", "port": 50055, "team": "PINK"}
  ],
  "overlay": [["A","B"], ["B","C"], ["B","D"], ["A","E"], ["E","F"], ["E","D"]],
  "topology_mode": "hierarchical",
  "health_check": {"enabled": true, "interval_seconds": 10, "timeout_seconds": 5},
  "client_gateway": "A",
  "shared_memory": {
    "segments": [
      {"name": "shm_host1", "members": ["A","B","D"]},
      {"name": "shm_host2", "members": ["C","E","F"]}
    ],
    "fields": ["status", "queue_size", "last_ts_ms"]
  }
}
EOF
echo -e "${GREEN}✓ Config written${NC}"

# Validate
if command -v python3 &>/dev/null; then
    python3 <<PYEOF
import json
with open('config/network_setup.json') as f:
    cfg = json.load(f)
    print("✓ Valid JSON")
    for node in cfg['nodes']:
        print(f"  Node {node['id']}: {node['host']}:{node['port']}")
PYEOF
fi

echo ""
echo -e "${YELLOW}Step 4: REBUILD${NC}"
cd build
rm -f src/cpp/mini2_server src/cpp/mini2_client 2>/dev/null || true
make -j$(nproc) 2>&1 | tail -10
if [ ! -f src/cpp/mini2_server ]; then
    echo -e "${RED}✗ Build failed!${NC}"
    exit 1
fi
cd ..
echo -e "${GREEN}✓ Build success${NC}"

echo ""
echo -e "${YELLOW}Step 5: CREATE LOGS${NC}"
mkdir -p logs
echo -e "${GREEN}✓ Ready${NC}"

echo ""
MY_IP=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
[ -z "$MY_IP" ] && MY_IP=$(hostname -I | awk '{print $1}')

echo -e "${GREEN}=== READY ===${NC}"
echo ""
echo "Your IP: $MY_IP"
echo ""

if [[ "$MY_IP" == "$CMP1" ]] || [[ "$MY_IP" =~ ^172\.26\. ]]; then
    echo -e "${GREEN}This is COMPUTER 1${NC}"
    echo ""
    echo "Run these commands ONE BY ONE:"
    echo ""
    echo -e "${YELLOW}./build/src/cpp/mini2_server A > logs/server_A.log 2>&1 &${NC}"
    echo "sleep 3"
    echo -e "${YELLOW}./build/src/cpp/mini2_server B > logs/server_B.log 2>&1 &${NC}"
    echo "sleep 3"
    echo -e "${YELLOW}./build/src/cpp/mini2_server D > logs/server_D.log 2>&1 &${NC}"
    echo "sleep 3"
    echo "ps aux | grep mini2_server | grep -v grep"
    echo ""
    echo "Check if they're running (should see 3):"
    echo "  grep 'Listening on' logs/server_*.log"
    echo ""
else
    echo -e "${GREEN}This is COMPUTER 2${NC}"
    echo ""
    echo "Run these commands ONE BY ONE:"
    echo ""
    echo -e "${YELLOW}./build/src/cpp/mini2_server C > logs/server_C.log 2>&1 &${NC}"
    echo "sleep 3"
    echo -e "${YELLOW}./build/src/cpp/mini2_server E > logs/server_E.log 2>&1 &${NC}"
    echo "sleep 3"
    echo -e "${YELLOW}./build/src/cpp/mini2_server F > logs/server_F.log 2>&1 &${NC}"
    echo "sleep 3"
    echo "ps aux | grep mini2_server | grep -v grep"
    echo ""
    echo "Check if they're running (should see 3):"
    echo "  grep 'Listening on' logs/server_*.log"
    echo ""
fi

echo "After BOTH computers have servers, wait 20 seconds then:"
echo "  ./build/src/cpp/mini2_client --server $CMP1:50050 --query 'test'"
echo ""
echo "Run all tests:"
echo "  ./scripts/comprehensive_test.sh"
