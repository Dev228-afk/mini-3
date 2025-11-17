#!/bin/bash

echo "=========================================="
echo "WINDOWS PORT FORWARDING SETUP"
echo "=========================================="
echo ""
echo "⚠️  CRITICAL: This must be run on WINDOWS PowerShell as Administrator"
echo ""
echo "Run these commands on Computer 1 (Windows PowerShell as Admin):"
echo ""
echo "----------------------------------------"
echo "COMPUTER 1 (192.168.137.189) COMMANDS:"
echo "----------------------------------------"
echo ""
cat << 'EOF'
# Get WSL IP dynamically
$wslIP = (wsl hostname -I).Trim()
Write-Host "WSL IP detected: $wslIP"
Write-Host ""

# Remove all existing port forwards
Write-Host "Removing old port forwarding rules..."
netsh interface portproxy reset
Write-Host ""

# Add port forwarding for servers A (50050), B (50051), D (50053)
Write-Host "Adding port forwarding rules..."
netsh interface portproxy add v4tov4 listenport=50050 listenaddress=0.0.0.0 connectport=50050 connectaddress=$wslIP
netsh interface portproxy add v4tov4 listenport=50051 listenaddress=0.0.0.0 connectport=50051 connectaddress=$wslIP
netsh interface portproxy add v4tov4 listenport=50053 listenaddress=0.0.0.0 connectport=50053 connectaddress=$wslIP

Write-Host ""
Write-Host "Current port forwarding rules:"
netsh interface portproxy show all

Write-Host ""
Write-Host "Adding Windows Firewall rules..."
New-NetFirewallRule -DisplayName "WSL gRPC Server 50050" -Direction Inbound -LocalPort 50050 -Protocol TCP -Action Allow -ErrorAction SilentlyContinue
New-NetFirewallRule -DisplayName "WSL gRPC Server 50051" -Direction Inbound -LocalPort 50051 -Protocol TCP -Action Allow -ErrorAction SilentlyContinue
New-NetFirewallRule -DisplayName "WSL gRPC Server 50053" -Direction Inbound -LocalPort 50053 -Protocol TCP -Action Allow -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "Verifying firewall rules..."
Get-NetFirewallRule -DisplayName "WSL gRPC Server*" | Format-Table DisplayName,Enabled,Direction,Action

Write-Host ""
Write-Host "✓ Port forwarding setup complete for Computer 1!"
Write-Host ""
Write-Host "Test connectivity (in WSL):"
Write-Host "  # From WSL, test if Windows IP reaches local server:"
Write-Host "  telnet 192.168.137.189 50050"
Write-Host ""
EOF

echo ""
echo "----------------------------------------"
echo "COMPUTER 2 (192.168.137.1) COMMANDS:"
echo "----------------------------------------"
echo ""
cat << 'EOF'
# Get WSL IP dynamically
$wslIP = (wsl hostname -I).Trim()
Write-Host "WSL IP detected: $wslIP"
Write-Host ""

# Remove all existing port forwards
Write-Host "Removing old port forwarding rules..."
netsh interface portproxy reset
Write-Host ""

# Add port forwarding for servers C (50052), E (50054), F (50055)
Write-Host "Adding port forwarding rules..."
netsh interface portproxy add v4tov4 listenport=50052 listenaddress=0.0.0.0 connectport=50052 connectaddress=$wslIP
netsh interface portproxy add v4tov4 listenport=50054 listenaddress=0.0.0.0 connectport=50054 connectaddress=$wslIP
netsh interface portproxy add v4tov4 listenport=50055 listenaddress=0.0.0.0 connectport=50055 connectaddress=$wslIP

Write-Host ""
Write-Host "Current port forwarding rules:"
netsh interface portproxy show all

Write-Host ""
Write-Host "Adding Windows Firewall rules..."
New-NetFirewallRule -DisplayName "WSL gRPC Server 50052" -Direction Inbound -LocalPort 50052 -Protocol TCP -Action Allow -ErrorAction SilentlyContinue
New-NetFirewallRule -DisplayName "WSL gRPC Server 50054" -Direction Inbound -LocalPort 50054 -Protocol TCP -Action Allow -ErrorAction SilentlyContinue
New-NetFirewallRule -DisplayName "WSL gRPC Server 50055" -Direction Inbound -LocalPort 50055 -Protocol TCP -Action Allow -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "Verifying firewall rules..."
Get-NetFirewallRule -DisplayName "WSL gRPC Server*" | Format-Table DisplayName,Enabled,Direction,Action

Write-Host ""
Write-Host "✓ Port forwarding setup complete for Computer 2!"
Write-Host ""
Write-Host "Test connectivity (in WSL):"
Write-Host "  # From WSL, test if Windows IP reaches local server:"
Write-Host "  telnet 192.168.137.1 50052"
Write-Host ""
EOF

echo ""
echo "=========================================="
echo "VERIFICATION STEPS (After Setup)"
echo "=========================================="
echo ""
echo "1. On Computer 1 WSL, test local access via Windows IP:"
echo "   telnet 192.168.137.189 50050"
echo ""
echo "2. On Computer 1 WSL, test Computer 2:"
echo "   telnet 192.168.137.1 50052"
echo ""
echo "3. On Computer 2 WSL, test local access via Windows IP:"
echo "   telnet 192.168.137.1 50052"
echo ""
echo "4. On Computer 2 WSL, test Computer 1:"
echo "   telnet 192.168.137.189 50050"
echo ""
echo "5. Run client test from Computer 1:"
echo "   ./build/src/cpp/mini2_client --server 192.168.137.189:50050 --query 'test'"
echo ""
echo "=========================================="
