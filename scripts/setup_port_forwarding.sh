#!/bin/bash

echo "=========================================="
echo "WINDOWS PORT FORWARDING FIX"
echo "=========================================="
echo ""
echo "This script generates PowerShell commands to run on Windows."
echo "You MUST run these commands in Windows PowerShell as Administrator."
echo ""

# Detect computer
MY_IP=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
if [ -z "$MY_IP" ]; then
    MY_IP=$(hostname -I | awk '{print $1}')
fi

if [[ "$MY_IP" == "192.168.137.110" ]]; then
    COMPUTER="1"
    WIN_IP="192.168.137.110"
    PORTS="50050 50051 50053"
elif [[ "$MY_IP" == "169.254.233.33" ]]; then
    COMPUTER="2"
    WIN_IP="169.254.233.33"
    PORTS="50052 50054 50055"
else
    echo "Cannot detect computer. Please run manually."
    exit 1
fi

echo "Detected: Computer $COMPUTER"
echo "Windows IP: $WIN_IP"
echo "WSL IP: $MY_IP"
echo ""
echo "========================================"
echo "STEP 1: CLEAR OLD PORT FORWARDING"
echo "========================================"
echo ""
echo "Run this in Windows PowerShell (Admin):"
echo ""
cat << 'EOF'
# Remove all existing port forwards
netsh interface portproxy reset
EOF
echo ""

echo "========================================"
echo "STEP 2: ADD NEW PORT FORWARDING"
echo "========================================"
echo ""
echo "Run these commands in Windows PowerShell (Admin):"
echo ""

# Get WSL IP dynamically
cat << EOF
# Get WSL IP dynamically
\$wslIP = (wsl hostname -I).Trim()
Write-Host "WSL IP: \$wslIP"
Write-Host ""

# Add port forwarding rules
EOF

for port in $PORTS; do
    cat << EOF
netsh interface portproxy add v4tov4 listenport=$port listenaddress=0.0.0.0 connectport=$port connectaddress=\$wslIP
Write-Host "✓ Port $port forwarded"
EOF
done

echo ""
cat << 'EOF'
# Verify
Write-Host ""
Write-Host "Current port forwarding rules:"
netsh interface portproxy show all
EOF

echo ""
echo "========================================"
echo "STEP 3: CONFIGURE WINDOWS FIREWALL"
echo "========================================"
echo ""
echo "Run this in Windows PowerShell (Admin):"
echo ""

cat << EOF
# Allow inbound connections on our ports
EOF

for port in $PORTS; do
    cat << EOF
New-NetFirewallRule -DisplayName "WSL gRPC Server $port" -Direction Inbound -LocalPort $port -Protocol TCP -Action Allow
EOF
done

echo ""
cat << 'EOF'
# Verify firewall rules
Get-NetFirewallRule -DisplayName "WSL gRPC Server*" | Format-Table DisplayName,Enabled,Direction,Action
EOF

echo ""
echo "========================================"
echo "STEP 4: VERIFY IN WSL"
echo "========================================"
echo ""
echo "After running Windows commands, verify in WSL:"
echo ""
echo "# Check servers are listening on 0.0.0.0"
echo "ss -tlnp | grep mini2_server"
echo ""
echo "# Should show:"
for port in $PORTS; do
    echo "#   0.0.0.0:$port or :::$port"
done
echo ""

echo "========================================"
echo "ALTERNATIVE: IF SERVERS BIND TO SPECIFIC IP"
echo "========================================"
echo ""
echo "If servers are binding to specific IP (e.g., $MY_IP:$port)"
echo "instead of 0.0.0.0:$port, you need to modify server code."
echo ""
echo "Check with:"
echo "  ss -tlnp | grep mini2_server"
echo ""
echo "If showing $MY_IP:$port, the server needs to bind to 0.0.0.0"
echo ""

echo "========================================"
echo "DONE"
echo "========================================"
echo ""
echo "After completing all steps:"
echo "1. Restart servers on both computers"
echo "2. Run: ./scripts/test_connectivity.sh"
echo "3. Test with: telnet <remote_ip> <port>"
echo ""
