#!/bin/bash

echo "=========================================="
echo "COMPREHENSIVE CONNECTIVITY TEST"
echo "=========================================="
echo ""

# Detect which computer we're on
MY_IP=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
if [ -z "$MY_IP" ]; then
    MY_IP=$(hostname -I | awk '{print $1}')
fi

echo "1. MY WSL IP ADDRESS"
echo "--------------------"
echo "WSL IP: $MY_IP"
echo ""

# Determine computer and remote IP
if [[ "$MY_IP" == "192.168.137.110" ]]; then
    COMPUTER="1"
    REMOTE_IP="192.168.137.1"
    MY_SERVERS="A B D"
    MY_PORTS="50050 50051 50053"
    REMOTE_PORTS="50052 50054 50055"
elif [[ "$MY_IP" == "192.168.137.1" ]] || [[ "$MY_IP" == "169.254.233.33" ]] || [[ "$MY_IP" == "172.22.208.1" ]]; then
    COMPUTER="2"
    REMOTE_IP="192.168.137.110"
    MY_SERVERS="C E F"
    MY_PORTS="50052 50054 50055"
    REMOTE_PORTS="50050 50051 50053"
else
    echo "❌ ERROR: Unknown IP $MY_IP"
    echo "Expected: 192.168.137.110 or 169.254.233.33"
    exit 1
fi

echo "This is Computer $COMPUTER"
echo "Remote Computer IP: $REMOTE_IP"
echo ""

echo "2. INTERNET CONNECTIVITY"
echo "------------------------"
ping -c 2 8.8.8.8 > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ Internet: WORKING"
else
    echo "❌ Internet: FAILED"
fi
echo ""

echo "3. WSL GATEWAY CONNECTIVITY"
echo "---------------------------"
GATEWAY=$(ip route | grep default | awk '{print $3}')
echo "Gateway: $GATEWAY"
ping -c 2 $GATEWAY > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ Can reach gateway: WORKING"
else
    echo "❌ Cannot reach gateway: FAILED"
fi
echo ""

echo "4. CHECK IF MY SERVERS ARE RUNNING"
echo "-----------------------------------"
for port in $MY_PORTS; do
    if ss -tlnp 2>/dev/null | grep -q ":$port "; then
        echo "✓ Port $port: Server RUNNING"
    else
        echo "❌ Port $port: No server listening"
    fi
done
echo ""

echo "5. TEST LOCAL PORTS (LOOPBACK)"
echo "-------------------------------"
for port in $MY_PORTS; do
    timeout 2 bash -c "echo > /dev/tcp/127.0.0.1/$port" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✓ Port $port: Locally accessible"
    else
        echo "❌ Port $port: Not accessible locally"
    fi
done
echo ""

echo "6. PING REMOTE COMPUTER"
echo "-----------------------"
echo "Pinging $REMOTE_IP..."
ping -c 3 $REMOTE_IP 2>&1 | grep -E "(bytes from|Destination Host Unreachable|100% packet loss)"
PING_RESULT=$?
if [ $PING_RESULT -eq 0 ]; then
    echo "✓ Ping: SUCCESS"
else
    echo "❌ Ping: FAILED"
    echo ""
    echo "DIAGNOSIS: Cannot ping remote computer"
    echo "This is a NETWORK LAYER problem, not application layer."
    echo ""
    echo "Possible causes:"
    echo "1. Computers not on same network"
    echo "2. Windows firewall blocking ICMP (ping)"
    echo "3. Network cable issue"
    echo "4. Different subnets without routing"
    echo ""
    echo "Next steps:"
    echo "- Verify both computers on same WiFi"
    echo "- Check Windows Firewall (should be OFF)"
    echo "- Try: netsh advfirewall set allprofiles state off"
fi
echo ""

echo "7. TEST REMOTE PORTS (TCP CONNECTION)"
echo "--------------------------------------"
for port in $REMOTE_PORTS; do
    echo -n "Testing $REMOTE_IP:$port ... "
    timeout 3 bash -c "echo > /dev/tcp/$REMOTE_IP/$port" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✓ CONNECTED"
    else
        echo "❌ FAILED (timeout or refused)"
    fi
done
echo ""

echo "8. CHECK WINDOWS IP (from PowerShell needed)"
echo "---------------------------------------------"
echo "Run this in Windows PowerShell:"
echo "  ipconfig | findstr IPv4"
echo "  Test-NetConnection -ComputerName $REMOTE_IP -Port ${REMOTE_PORTS%% *}"
echo ""

echo "9. ROUTING TABLE"
echo "----------------"
ip route
echo ""

echo "10. NETWORK INTERFACES"
echo "----------------------"
ip addr show | grep -E "^[0-9]+:|inet "
echo ""

echo "=========================================="
echo "SUMMARY"
echo "=========================================="
echo "Computer: $COMPUTER"
echo "WSL IP: $MY_IP"
echo "Remote IP: $REMOTE_IP"
echo ""

# Check if servers are running
SERVER_COUNT=$(ss -tlnp 2>/dev/null | grep -E ":(5005[0-5])" | wc -l)
echo "Servers running locally: $SERVER_COUNT"

# Check ping
if [ $PING_RESULT -eq 0 ]; then
    echo "Remote ping: ✓ SUCCESS"
    echo ""
    echo "✓ Network layer works!"
    echo "If telnet still fails, check:"
    echo "  1. Servers are running on remote computer"
    echo "  2. Port forwarding configured (NAT mode)"
    echo "  3. Windows firewall rules"
else
    echo "Remote ping: ❌ FAILED"
    echo ""
    echo "❌ NETWORK LAYER PROBLEM!"
    echo ""
    echo "CRITICAL: Fix network connectivity first!"
    echo "Without ping working, no application will connect."
    echo ""
    echo "Action items:"
    echo "  1. Verify same WiFi/network on both computers"
    echo "  2. Check Windows Firewall (disable completely)"
    echo "  3. Check WSL networking mode (should be NAT)"
    echo "  4. Verify Ethernet cable connection"
    echo "  5. Check subnet masks match or routing exists"
fi
echo ""
echo "=========================================="
