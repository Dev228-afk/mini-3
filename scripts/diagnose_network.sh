#!/usr/bin/env bash
# WSL network diagnostic and fix script

echo "=== WSL Network Diagnostics ==="
echo ""

# Check if running in WSL
if grep -qi microsoft /proc/version 2>/dev/null; then
    echo "✓ Running in WSL"
    
    # WSL version detection
    if grep -qi "WSL2" /proc/version 2>/dev/null; then
        WSL_VERSION="WSL2"
    else
        WSL_VERSION="WSL1"
    fi
    echo "  Version: $WSL_VERSION"
else
    echo "Not running in WSL, skipping WSL-specific checks"
    exit 0
fi

echo ""
echo "=== Network Configuration ==="

# Show IP addresses
echo "IP Addresses:"
ip addr show | grep "inet " | awk '{print "  " $2 " on " $NF}'

echo ""
echo "Default gateway:"
ip route | grep default | awk '{print "  " $3}'

echo ""
echo "=== Port Listening Check ==="

# Check if mini2_server processes are running
SERVER_PROCS=$(ps aux | grep "[m]ini2_server" | wc -l)
if [[ $SERVER_PROCS -gt 0 ]]; then
    echo "✓ Found $SERVER_PROCS mini2_server process(es)"
    ps aux | grep "[m]ini2_server" | awk '{print "  PID: " $2 " - " $11 " " $12 " " $13}'
else
    echo "✗ No mini2_server processes found"
    echo "  Start servers with: ./scripts/start_servers.sh"
fi

echo ""
echo "Listening ports (mini2):"
if command -v netstat >/dev/null 2>&1; then
    netstat -tuln 2>/dev/null | grep -E ":(5005[0-5])" | awk '{print "  " $4}' || echo "  No ports 50050-50055 listening"
elif command -v ss >/dev/null 2>&1; then
    ss -tuln 2>/dev/null | grep -E ":(5005[0-5])" | awk '{print "  " $5}' || echo "  No ports 50050-50055 listening"
else
    echo "  netstat/ss not available"
fi

echo ""
echo "=== Firewall Status ==="

# Check iptables (common in WSL)
if command -v iptables >/dev/null 2>&1; then
    RULES=$(sudo iptables -L -n 2>/dev/null | wc -l)
    echo "iptables rules: $RULES lines"
    
    # Check for blocking rules on our ports
    BLOCKED=$(sudo iptables -L -n 2>/dev/null | grep -E "5005[0-5]" | grep DROP | wc -l)
    if [[ $BLOCKED -gt 0 ]]; then
        echo "⚠ WARNING: Found firewall rules blocking ports 50050-50055"
    fi
else
    echo "iptables not available"
fi

echo ""
echo "=== Connectivity Test ==="

# Try to connect to localhost ports
for PORT in 50050 50051 50052 50053 50054 50055; do
    if command -v nc >/dev/null 2>&1; then
        if nc -z -w 1 127.0.0.1 $PORT 2>/dev/null; then
            echo "✓ Port $PORT is reachable on localhost"
        else
            echo "✗ Port $PORT is not reachable on localhost"
        fi
    else
        # Fallback using /dev/tcp
        if timeout 1 bash -c "echo >/dev/tcp/127.0.0.1/$PORT" 2>/dev/null; then
            echo "✓ Port $PORT is reachable on localhost"
        else
            echo "✗ Port $PORT is not reachable on localhost"
        fi
    fi
done

echo ""
echo "=== WSL-Specific Issues ==="

if [[ "$WSL_VERSION" == "WSL1" ]]; then
    echo "WSL1 Notes:"
    echo "  - SO_REUSEPORT not supported (causes harmless warnings)"
    echo "  - Network stack shares Windows host"
    echo "  - Use Windows firewall settings"
elif [[ "$WSL_VERSION" == "WSL2" ]]; then
    echo "WSL2 Notes:"
    echo "  - Has own virtual network adapter"
    echo "  - May need port forwarding from Windows"
    echo "  - Check Windows Firewall for port access"
fi

echo ""
echo "=== Recommendations ==="

if [[ $SERVER_PROCS -eq 0 ]]; then
    echo "1. Start the servers: ./scripts/start_servers.sh"
fi

echo "2. Suppress gRPC warnings: export GRPC_VERBOSITY=ERROR"
echo "3. Use the wrapper script: ./scripts/run_client.sh --server <addr> --mode session --query <file>"

if [[ "$WSL_VERSION" == "WSL2" ]]; then
    echo ""
    echo "For WSL2 cross-machine connectivity:"
    echo "  Windows side: netsh interface portproxy add v4tov4 listenport=50050 connectaddress=<WSL_IP> connectport=50050"
    echo "  Check WSL IP: ip addr show eth0 | grep inet"
fi

echo ""
echo "=== Diagnostic Complete ==="
