#!/bin/bash

echo "=========================================="
echo "MINI-2 SERVER STARTUP SCRIPT"
echo "=========================================="
echo ""

# Detect which computer
MY_IP=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
if [ -z "$MY_IP" ]; then
    MY_IP=$(hostname -I | awk '{print $1}')
fi

# Determine computer
if [[ "$MY_IP" =~ 192\.168\.137\. ]] && [[ "$MY_IP" != "192.168.137.1" ]]; then
    COMPUTER="1"
    SERVERS=(A B D)
elif [[ "$MY_IP" =~ (192\.168\.137\.1|169\.254\.|172\.22\.) ]]; then
    COMPUTER="2"
    SERVERS=(C E F)
else
    echo "❌ Cannot determine computer from IP: $MY_IP"
    echo "Please run from Computer 1 or Computer 2"
    exit 1
fi

echo "Detected: Computer $COMPUTER"
echo "My IP: $MY_IP"
echo "Will start servers: ${SERVERS[@]}"
echo ""

# Navigate to project root
cd ~/mini-2 || cd /home/*/dev/mini-2 || cd /mnt/c/Users/*/mini-2 || {
    echo "❌ Cannot find mini-2 directory"
    exit 1
}

echo "Working directory: $(pwd)"
echo ""

# Create logs directory if missing
echo "Step 1: Creating logs directory..."
mkdir -p logs
chmod 755 logs
echo "✓ logs/ directory ready"
echo ""

# Check if config file exists and is valid
echo "Step 2: Checking configuration..."
if [ ! -f config/network_setup.json ]; then
    echo "❌ ERROR: config/network_setup.json not found!"
    echo ""
    echo "Please run: git pull"
    exit 1
fi

# Validate JSON
if ! python3 -m json.tool config/network_setup.json > /dev/null 2>&1; then
    echo "❌ ERROR: config/network_setup.json is invalid JSON!"
    echo ""
    echo "Content preview:"
    head -20 config/network_setup.json
    echo ""
    echo "Please run: git pull"
    exit 1
fi

echo "✓ Configuration file valid"
echo ""

# Check if binaries exist
echo "Step 3: Checking server binaries..."
if [ ! -f build/src/cpp/mini2_server ]; then
    echo "❌ ERROR: mini2_server not found!"
    echo ""
    echo "Please build first:"
    echo "  cd build"
    echo "  cmake .."
    echo "  make -j\$(nproc)"
    exit 1
fi

echo "✓ Server binary exists"
echo ""

# Kill old servers
echo "Step 4: Stopping old servers..."
pkill -9 mini2_server 2>/dev/null
sleep 2
echo "✓ Old servers stopped"
echo ""

# Start servers
echo "Step 5: Starting servers..."
for server in "${SERVERS[@]}"; do
    echo "  Starting server $server..."
    ./build/src/cpp/mini2_server $server > logs/server_${server}.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    # Check if started successfully
    if ps -p $SERVER_PID > /dev/null 2>&1; then
        echo "    ✓ Server $server started (PID: $SERVER_PID)"
        
        # Check log for success
        if grep -q "Listening on" logs/server_${server}.log 2>/dev/null; then
            PORT=$(grep "Listening on" logs/server_${server}.log | grep -oE '[0-9]+' | tail -1)
            echo "      Listening on port: $PORT"
        fi
    else
        echo "    ❌ Server $server FAILED to start!"
        echo ""
        echo "Log output:"
        cat logs/server_${server}.log
        echo ""
    fi
done

echo ""
echo "Step 6: Verifying running servers..."
RUNNING=$(ps aux | grep mini2_server | grep -v grep | wc -l)
echo "Running servers: $RUNNING / ${#SERVERS[@]}"
echo ""

if [ $RUNNING -eq ${#SERVERS[@]} ]; then
    echo "✓ All servers started successfully!"
else
    echo "⚠ WARNING: Not all servers started!"
    echo ""
    echo "Check logs:"
    for server in "${SERVERS[@]}"; do
        if [ -f logs/server_${server}.log ]; then
            echo ""
            echo "=== Server $server log ==="
            tail -20 logs/server_${server}.log
        fi
    done
    exit 1
fi

echo ""
echo "Step 7: Server status..."
ps aux | grep mini2_server | grep -v grep | awk '{print "  Server " $11 " (PID " $2 ") - Memory: " $6 " KB"}'
echo ""

echo "=========================================="
echo "SERVERS READY!"
echo "=========================================="
echo ""
echo "Computer $COMPUTER servers running: ${SERVERS[@]}"
echo ""
echo "Next steps:"
if [ "$COMPUTER" == "1" ]; then
    echo "  1. Wait for Computer 2 to start servers C, E, F"
    echo "  2. Test connectivity:"
    echo "     telnet 192.168.137.1 50052"
    echo "  3. Run client test:"
    echo "     ./build/src/cpp/mini2_client --server 192.168.137.189:50050 --query 'test'"
else
    echo "  1. Wait for Computer 1 to start servers A, B, D"
    echo "  2. Test connectivity:"
    echo "     telnet 192.168.137.189 50050"
    echo "  3. Computer 1 should run client tests"
fi
echo ""
echo "View logs:"
echo "  tail -f logs/server_*.log"
echo ""
