#!/bin/bash

echo "=========================================="
echo "WINDOWS IP DISCOVERY"
echo "=========================================="
echo ""
echo "Run these commands on EACH computer to find Windows IPs:"
echo ""
echo "=========================================="
echo "IN WINDOWS POWERSHELL (both computers):"
echo "=========================================="
echo ""
cat << 'EOF'
# Get all IPv4 addresses
ipconfig | findstr IPv4

# Or more detailed:
Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.InterfaceAlias -notlike "*Loopback*"} | Format-Table InterfaceAlias, IPAddress, PrefixLength
EOF

echo ""
echo "=========================================="
echo "WHAT TO LOOK FOR:"
echo "=========================================="
echo ""
echo "Computer 1 should have:"
echo "  - Ethernet/WiFi adapter: 192.168.137.x (shared to Computer 2)"
echo "  - WSL (vEthernet): Different IP for WSL"
echo ""
echo "Computer 2 should have:"
echo "  - Ethernet adapter: 192.168.137.x (received from Computer 1)"
echo "  - WSL IP: 169.254.233.33 (current)"
echo ""
echo "=========================================="
echo "CURRENT CONFIGURATION ANALYSIS:"
echo "=========================================="
echo ""
echo "Based on your output:"
echo "  - Computer 2 WSL IP: 169.254.233.33 (link-local - BAD)"
echo "  - Computer 2 can ping: 192.168.137.1 (Computer 1's gateway)"
echo "  - Computer 1 WSL IP: 192.168.137.110"
echo ""
echo "⚠️  PROBLEM IDENTIFIED:"
echo "  Computer 2 WSL has link-local IP (169.254.x.x) which means"
echo "  it didn't get a proper IP from DHCP or static config."
echo ""
echo "=========================================="
echo "SOLUTION: FIX COMPUTER 2's WSL IP"
echo "=========================================="
echo ""
echo "The issue is Computer 2 WSL is on 169.254.233.33 (link-local)"
echo "while Computer 1 is on 192.168.137.x subnet."
echo ""
echo "Computer 2 needs to be on the SAME subnet: 192.168.137.x"
echo ""
echo "=========================================="
echo "OPTION 1: Use Windows IPs (RECOMMENDED)"
echo "=========================================="
echo ""
echo "Instead of using WSL IPs in config, use Windows IPs:"
echo ""
echo "Step 1: Find Computer 2's Windows IP (PowerShell):"
echo "  ipconfig | findstr \"192.168.137\""
echo ""
echo "Step 2: Update config/network_setup.json"
echo "  Computer 1 servers (A,B,D): Use Computer 1 Windows IP"
echo "  Computer 2 servers (C,E,F): Use Computer 2 Windows IP"
echo ""
echo "Step 3: Setup port forwarding (PowerShell as Admin):"
echo "  See setup_port_forwarding.sh output"
echo ""
echo "=========================================="
echo "OPTION 2: Fix Computer 2 WSL Networking"
echo "=========================================="
echo ""
echo "On Computer 2, in PowerShell as Administrator:"
echo ""
cat << 'EOF'
# Shutdown WSL
wsl --shutdown

# Edit .wslconfig
notepad $env:USERPROFILE\.wslconfig

# Add/modify to force static IP in same subnet:
[wsl2]
networkingMode=bridged
vmSwitch=WSLBridge

# Or keep NAT but fix subnet
[wsl2]
networkingMode=NAT
EOF

echo ""
echo "Then restart WSL and check if it gets 192.168.137.x IP"
echo ""
echo "=========================================="
echo "QUICK FIX: Find Computer 2 Windows IP Now"
echo "=========================================="
echo ""
echo "On Computer 2, run in PowerShell:"
echo "  ipconfig"
echo ""
echo "Look for an adapter with IP starting with 192.168.137.x"
echo "That's the IP to use in the config for servers C, E, F"
echo ""
echo "If Computer 2 Windows also has 169.254.x.x, then:"
echo "  1. Check if Ethernet cable is properly connected"
echo "  2. Check if Internet Connection Sharing is enabled on Computer 1"
echo "  3. Or connect both computers to same WiFi router"
echo ""
