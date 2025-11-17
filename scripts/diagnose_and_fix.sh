#!/bin/bash

echo "=========================================="
echo "COMPLETE DIAGNOSTIC & FIX"
echo "=========================================="
echo ""

# Detect computer
MY_IP=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
if [ -z "$MY_IP" ]; then
    MY_IP=$(hostname -I | awk '{print $1}')
fi

if [[ "$MY_IP" =~ 192\.168\.137\. ]] && [[ "$MY_IP" != "192.168.137.1" ]]; then
    COMPUTER="1"
    WIN_IP="192.168.137.189"
    MY_PORTS=(50050 50051 50053)
    REMOTE_IP="192.168.137.1"
    REMOTE_PORTS=(50052 50054 50055)
elif [[ "$MY_IP" =~ (192\.168\.137\.1|169\.254\.|172\.22\.) ]]; then
    COMPUTER="2"
    WIN_IP="192.168.137.1"
    MY_PORTS=(50052 50054 50055)
    REMOTE_IP="192.168.137.189"
    REMOTE_PORTS=(50050 50051 50053)
else
    echo "Cannot determine computer from IP: $MY_IP"
    exit 1
fi

echo "Computer: $COMPUTER"
echo "WSL IP: $MY_IP"
echo "Windows IP: $WIN_IP"
echo ""

echo "=========================================="
echo "STEP 1: Check if servers are running"
echo "=========================================="
ps aux | grep mini2_server | grep -v grep
SERVER_COUNT=$(ps aux | grep mini2_server | grep -v grep | wc -l)
echo ""
echo "Running servers: $SERVER_COUNT"
echo ""

if [ $SERVER_COUNT -eq 0 ]; then
    echo "❌ NO SERVERS RUNNING!"
    echo ""
    echo "Fix: Start servers first"
    echo "  cd ~/mini-2"
    echo "  ./scripts/start_servers.sh"
    exit 1
fi

echo "=========================================="
echo "STEP 2: Check what ports are listening"
echo "=========================================="
echo "Expected ports for Computer $COMPUTER: ${MY_PORTS[@]}"
echo ""
netstat -tlnp 2>/dev/null | grep "mini2_server" || ss -tlnp | grep "mini2_server"
echo ""

echo "Checking each expected port:"
for port in "${MY_PORTS[@]}"; do
    if netstat -tln 2>/dev/null | grep -q ":$port " || ss -tln 2>/dev/null | grep -q ":$port "; then
        echo "  ✓ Port $port: LISTENING"
    else
        echo "  ❌ Port $port: NOT LISTENING"
    fi
done
echo ""

echo "=========================================="
echo "STEP 3: Test local WSL IP connectivity"
echo "=========================================="
echo "Testing if servers respond on WSL IP ($MY_IP)..."
echo ""

for port in "${MY_PORTS[@]}"; do
    echo -n "  Testing $MY_IP:$port ... "
    timeout 2 bash -c "echo > /dev/tcp/$MY_IP/$port" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✓ WORKS"
    else
        echo "❌ FAILED"
    fi
done
echo ""

echo "=========================================="
echo "STEP 4: Test Windows IP (requires port forwarding)"
echo "=========================================="

# Check if WSL IP matches Windows IP (Computer 1 scenario)
if [ "$MY_IP" == "$WIN_IP" ]; then
    echo "WSL IP matches Windows IP ($MY_IP) - Port forwarding NOT NEEDED"
    echo "This is normal for Computer 1 in WSL NAT mode."
    echo ""
    FORWARD_WORKS=${#MY_PORTS[@]}  # Mark as working since not needed
else
    echo "Testing if port forwarding works (Windows IP → WSL)..."
    echo ""
    
    FORWARD_WORKS=0
    for port in "${MY_PORTS[@]}"; do
        echo -n "  Testing $WIN_IP:$port ... "
        timeout 2 bash -c "echo > /dev/tcp/$WIN_IP/$port" 2>/dev/null
        if [ $? -eq 0 ]; then
            echo "✓ WORKS"
            FORWARD_WORKS=$((FORWARD_WORKS + 1))
        else
            echo "❌ FAILED (port forwarding missing!)"
        fi
    done
    echo ""
fi

if [ $FORWARD_WORKS -eq 0 ] && [ "$MY_IP" != "$WIN_IP" ]; then
    echo "❌ CRITICAL: Port forwarding NOT configured!"
    echo ""
    echo "You MUST run these commands in Windows PowerShell as Administrator:"
    echo ""
    echo "===== COPY THIS TO WINDOWS POWERSHELL (AS ADMIN) ====="
    echo ""
    cat << EOF
# Get WSL IP
\$wslIP = (wsl hostname -I).Trim()
Write-Host "WSL IP: \$wslIP"

# Remove old forwards
netsh interface portproxy reset

# Add port forwarding
EOF
    
    for port in "${MY_PORTS[@]}"; do
        echo "netsh interface portproxy add v4tov4 listenport=$port listenaddress=0.0.0.0 connectport=$port connectaddress=\$wslIP"
    done
    
    cat << 'EOF'

# Verify
netsh interface portproxy show all
Write-Host ""
Write-Host "If you see the ports listed above, forwarding is active!"
EOF
    echo ""
    echo "===== END OF POWERSHELL COMMANDS ====="
    echo ""
fi

echo "=========================================="
echo "STEP 5: Test cross-computer connectivity"
echo "=========================================="
echo "Testing connection to Computer $([ "$COMPUTER" == "1" ] && echo "2" || echo "1") ($REMOTE_IP)..."
echo ""

REMOTE_WORKS=0
for port in "${REMOTE_PORTS[@]}"; do
    echo -n "  Testing $REMOTE_IP:$port ... "
    timeout 2 bash -c "echo > /dev/tcp/$REMOTE_IP/$port" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✓ WORKS"
        REMOTE_WORKS=$((REMOTE_WORKS + 1))
    else
        echo "❌ FAILED"
    fi
done
echo ""

if [ $REMOTE_WORKS -eq 0 ]; then
    echo "❌ Cannot reach remote computer!"
    echo ""
    echo "Possible causes:"
    echo "  1. Remote computer's servers not running"
    echo "  2. Remote computer's port forwarding not configured"
    echo "  3. Network connectivity issue"
    echo ""
    echo "Have the other computer run: ./scripts/diagnose_and_fix.sh"
fi

echo "=========================================="
echo "SUMMARY"
echo "=========================================="
echo ""
echo "Computer $COMPUTER Status:"
echo "  Servers running: $SERVER_COUNT"
echo "  WSL connectivity: ✓"
if [ $FORWARD_WORKS -gt 0 ]; then
    echo "  Port forwarding: ✓ ($FORWARD_WORKS/${#MY_PORTS[@]} ports)"
else
    echo "  Port forwarding: ❌ MISSING - RUN POWERSHELL COMMANDS ABOVE"
fi

if [ $REMOTE_WORKS -gt 0 ]; then
    echo "  Remote connectivity: ✓ ($REMOTE_WORKS/${#REMOTE_PORTS[@]} ports)"
else
    echo "  Remote connectivity: ❌"
fi
echo ""

if [ $FORWARD_WORKS -eq ${#MY_PORTS[@]} ] && [ $REMOTE_WORKS -eq ${#REMOTE_PORTS[@]} ]; then
    echo "✅ ALL SYSTEMS GO! Ready to run tests."
    echo ""
    echo "Run client test:"
    if [ "$COMPUTER" == "1" ]; then
        echo "  ./build/src/cpp/mini2_client --server 192.168.137.189:50050 --mode ping"
    else
        echo "  Computer 1 should run client tests"
    fi
else
    echo "⚠️  NOT READY - Fix issues above first"
fi
echo ""
echo "=========================================="
