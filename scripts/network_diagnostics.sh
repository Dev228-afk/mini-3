#!/bin/bash
# Network Diagnostics - Run on BOTH computers

echo "=== NETWORK DIAGNOSTICS ==="
echo ""

echo "1. WSL IP Address:"
ip addr show eth0 2>/dev/null | grep "inet " || hostname -I
echo ""

echo "2. WSL Routing Table:"
ip route show
echo ""

echo "3. Windows IP (Gateway):"
ip route show | grep default | awk '{print "Windows Gateway: " $3}'
echo ""

echo "4. Can WSL reach Windows?"
WIN_IP=$(ip route show | grep default | awk '{print $3}')
ping -c 2 $WIN_IP && echo "✓ Can reach Windows" || echo "✗ Cannot reach Windows"
echo ""

echo "5. Can WSL reach Internet?"
ping -c 2 8.8.8.8 && echo "✓ Internet works" || echo "✗ No internet"
echo ""

echo "6. Servers listening on which interface?"
echo "Checking if mini2_server is bound to 0.0.0.0 or specific IP:"
netstat -tuln 2>/dev/null | grep -E ':(5005[0-5])' || ss -tuln | grep -E ':(5005[0-5])'
echo ""

echo "7. Test if other computer is reachable at IP layer:"
echo "From Computer 1, test Computer 2:"
echo "  ping -c 3 192.168.137.1"
echo ""
echo "From Computer 2, test Computer 1:"
echo "  ping -c 3 192.168.137.110"
echo ""

echo "8. CRITICAL: Are both computers on the same physical network?"
echo "Computer 1 IP: 192.168.137.110 (subnet: 192.168.137.x)"
echo "Computer 2 IP: 192.168.137.1 (subnet: 192.168.137.x)"
echo "✓ SAME SUBNET! Network layer should work."
echo ""
echo "⚠️ DIFFERENT SUBNETS! They may not be on the same network!"
echo ""
echo "Check:"
echo "  - Are they connected to the same WiFi/Ethernet?"
echo "  - Are they using the same router?"
echo "  - Can you ping between their WINDOWS IPs (not WSL)?"
echo ""

echo "9. Get Windows IP (from Windows PowerShell):"
echo "  ipconfig | findstr IPv4"
echo ""

echo "10. Test Windows-to-Windows connectivity:"
echo "  From Computer 1 PowerShell: Test-NetConnection -ComputerName <Computer2_Windows_IP> -Port 50050"
echo ""
