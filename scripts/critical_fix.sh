#!/bin/bash
# CRITICAL FIX - Servers are crashing due to config/firewall issues

echo "=== CRITICAL FIX SCRIPT ==="
echo ""

# Find project directory
if [ -d ~/mini-2 ]; then
    cd ~/mini-2
elif [ -d /home/*/dev/mini-2 ]; then
    cd /home/*/dev/mini-2
elif [ -d /mnt/c/Users/*/mini-2 ]; then
    cd /mnt/c/Users/*/mini-2
else
    echo "ERROR: Cannot find mini-2 directory"
    exit 1
fi

echo "Working directory: $(pwd)"
echo ""

# Step 1: Kill crashed servers
echo "Step 1: Killing crashed servers..."
pkill -9 mini2_server 2>/dev/null || true
sleep 2
echo "✓ Done"

# Step 2: Clean shared memory
echo "Step 2: Cleaning shared memory..."
rm -f /dev/shm/shm_* 2>/dev/null || true
echo "✓ Done"

# Step 3: Check which computer we're on
echo ""
echo "Step 3: Detecting computer..."
MY_IP=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
if [ -z "$MY_IP" ]; then
    MY_IP=$(hostname -I | awk '{print $1}')
fi
echo "My IP: $MY_IP"

# Determine which computer
if [[ "$MY_IP" == "172.26.247.68" ]]; then
    COMPUTER="1"
    MY_SERVERS="A B D"
    OTHER_IP="172.22.223.237"
elif [[ "$MY_IP" == "172.22.223.237" ]]; then
    COMPUTER="2"
    MY_SERVERS="C E F"
    OTHER_IP="172.26.247.68"
else
    echo "WARNING: IP doesn't match expected values"
    echo "Assuming Computer 1..."
    COMPUTER="1"
    MY_SERVERS="A B D"
    OTHER_IP="172.22.223.237"
fi

echo "This is Computer $COMPUTER"
echo "Will start servers: $MY_SERVERS"
echo ""

# Step 4: Check firewall (Linux)
echo "Step 4: Checking Linux firewall..."
if command -v ufw &> /dev/null; then
    sudo ufw allow 50050:50055/tcp 2>/dev/null || echo "UFW not active or permission denied"
fi
if command -v iptables &> /dev/null; then
    sudo iptables -A INPUT -p tcp --dport 50050:50055 -j ACCEPT 2>/dev/null || echo "iptables permission denied (may be ok)"
fi
echo "✓ Firewall rules attempted"

# Step 5: Test connectivity to other computer
echo ""
echo "Step 5: Testing connectivity to other computer ($OTHER_IP)..."
if ping -c 2 -W 2 $OTHER_IP > /dev/null 2>&1; then
    echo "✓ Ping successful to $OTHER_IP"
else
    echo "✗ Cannot ping $OTHER_IP"
    echo "  This may cause cross-computer tests to fail"
fi

# Step 6: Verify binaries exist
echo ""
echo "Step 6: Checking binaries..."
if [ ! -f build/src/cpp/mini2_server ]; then
    echo "✗ mini2_server not found - need to build first!"
    exit 1
fi
echo "✓ Binaries found"

# Step 7: Create logs directory
echo ""
echo "Step 7: Creating logs directory..."
mkdir -p logs
echo "✓ Done"

echo ""
echo "=== READY TO START SERVERS ==="
echo ""
echo "Start servers with this command:"
echo ""
if [ "$COMPUTER" == "1" ]; then
    cat << 'EOF'
# Computer 1 - Start A, B, D
nohup ./build/src/cpp/mini2_server A > logs/server_A.log 2>&1 &
sleep 1
nohup ./build/src/cpp/mini2_server B > logs/server_B.log 2>&1 &
sleep 1
nohup ./build/src/cpp/mini2_server D > logs/server_D.log 2>&1 &
sleep 2
echo "Servers started. Check status:"
ps aux | grep mini2_server | grep -v grep
echo ""
echo "Check logs if crashed:"
tail -20 logs/server_A.log
EOF
else
    cat << 'EOF'
# Computer 2 - Start C, E, F
nohup ./build/src/cpp/mini2_server C > logs/server_C.log 2>&1 &
sleep 1
nohup ./build/src/cpp/mini2_server E > logs/server_E.log 2>&1 &
sleep 1
nohup ./build/src/cpp/mini2_server F > logs/server_F.log 2>&1 &
sleep 2
echo "Servers started. Check status:"
ps aux | grep mini2_server | grep -v grep
echo ""
echo "Check logs if crashed:"
tail -20 logs/server_C.log
EOF
fi

echo ""
echo "After starting servers, wait 20 seconds then test from Computer 1:"
echo "  ./build/src/cpp/mini2_client --server 172.26.247.68:50050 --query 'test'"
