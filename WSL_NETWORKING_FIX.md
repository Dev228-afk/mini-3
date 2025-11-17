# WSL Networking Fix - Mirrored Mode Issue

## Problem Identified
WSL cannot reach its Windows gateway (172.26.240.1) even though it has an IP (172.26.247.68). This breaks all network connectivity.

```
PING 172.26.240.1 (172.26.240.1) 56(84) bytes of data.
From 172.26.247.68 icmp_seq-1 Destination Host Unreachable
X Cannot reach Windows
```

**Root Cause**: WSL mirrored networking mode has broken connectivity. Your servers are binding to `[::ffff:172.26.247.68]` which won't be accessible from outside.

---

## SOLUTION: Switch to NAT Mode (Recommended)

### Step 1: Create/Edit .wslconfig (ON WINDOWS, BOTH COMPUTERS)

Open PowerShell as Administrator and run:

```powershell
# Create or edit .wslconfig
notepad "$env:USERPROFILE\.wslconfig"
```

**Replace ALL contents with this:**

```ini
[wsl2]
networkingMode=NAT
localhostForwarding=true
firewall=false

[experimental]
autoMemoryReclaim=gradual
sparseVhd=true
```

Save and close notepad.

### Step 2: Restart WSL (BOTH COMPUTERS)

```powershell
# Shutdown WSL completely
wsl --shutdown

# Wait 5 seconds
Start-Sleep -Seconds 5

# Start WSL again
wsl
```

### Step 3: Verify WSL Networking (IN WSL, BOTH COMPUTERS)

```bash
# Check new IP (should be 172.x.x.x in NAT range)
ip addr show eth0

# Test Windows gateway
ping -c 3 $(ip route | grep default | awk '{print $3}')

# Test internet
ping -c 3 8.8.8.8

# All pings should work now!
```

---

## Configure Port Forwarding (NAT Mode)

Since NAT mode puts WSL behind Windows, you need port forwarding so the other computer can reach your servers.

### Computer 1 (Servers A, B, D on ports 50050, 50051, 50053)

Run in **Windows PowerShell as Administrator**:

```powershell
# Get WSL IP
$wslIP = (wsl hostname -I).Trim()
Write-Host "WSL IP: $wslIP"

# Forward ports 50050, 50051, 50053
netsh interface portproxy add v4tov4 listenport=50050 listenaddress=0.0.0.0 connectport=50050 connectaddress=$wslIP
netsh interface portproxy add v4tov4 listenport=50051 listenaddress=0.0.0.0 connectport=50051 connectaddress=$wslIP
netsh interface portproxy add v4tov4 listenport=50053 listenaddress=0.0.0.0 connectport=50053 connectaddress=$wslIP

# Verify
netsh interface portproxy show all
```

### Computer 2 (Servers C, E, F on ports 50052, 50054, 50055)

Run in **Windows PowerShell as Administrator**:

```powershell
# Get WSL IP
$wslIP = (wsl hostname -I).Trim()
Write-Host "WSL IP: $wslIP"

# Forward ports 50052, 50054, 50055
netsh interface portproxy add v4tov4 listenport=50052 listenaddress=0.0.0.0 connectport=50052 connectaddress=$wslIP
netsh interface portproxy add v4tov4 listenport=50054 listenaddress=0.0.0.0 connectport=50054 connectaddress=$wslIP
netsh interface portproxy add v4tov4 listenport=50055 listenaddress=0.0.0.0 connectport=50055 connectaddress=$wslIP

# Verify
netsh interface portproxy show all
```

---

## Update Server Configuration

Your servers must bind to **0.0.0.0** (all interfaces) not specific IPs.

### Check Current Binding (IN WSL)

```bash
cd ~/mini-2/build/src/cpp

# Start one server
./mini2_server A &

# Check binding (should show 0.0.0.0, not specific IP)
ss -tlnp | grep mini2_server
```

If showing specific IP like `172.26.247.68:50050`, the code needs fixing.

---

## Verify Windows-to-Windows Connectivity

Before testing servers, verify Windows IPs can communicate.

### On Computer 1 (Windows PowerShell)

```powershell
# Get your Windows IP
ipconfig | findstr IPv4

# Ping Computer 2's Windows IP
ping 172.22.223.237

# Test if Computer 2's port 50052 is reachable
Test-NetConnection -ComputerName 172.22.223.237 -Port 50052
```

### On Computer 2 (Windows PowerShell)

```powershell
# Get your Windows IP
ipconfig | findstr IPv4

# Ping Computer 1's Windows IP
ping 172.26.247.68

# Test if Computer 1's port 50050 is reachable
Test-NetConnection -ComputerName 172.26.247.68 -Port 50050
```

**Both pings must succeed!** If not:
- Verify both on same WiFi network
- Check Ethernet cable connection
- Disable Windows Defender Firewall (already done)

---

## Start Servers with Correct Configuration

### Update config/network_setup.json

**Use Windows IPs** (not WSL IPs) since port forwarding routes to WSL:

```json
{
  "servers": [
    {
      "id": "A",
      "address": "172.26.247.68:50050",
      "host": 1
    },
    {
      "id": "B", 
      "address": "172.26.247.68:50051",
      "host": 1
    },
    {
      "id": "C",
      "address": "172.22.223.237:50052",
      "host": 2
    },
    {
      "id": "D",
      "address": "172.26.247.68:50053",
      "host": 1
    },
    {
      "id": "E",
      "address": "172.22.223.237:50054",
      "host": 2
    },
    {
      "id": "F",
      "address": "172.22.223.237:50055",
      "host": 2
    }
  ],
  "gateway": "A"
}
```

### Rebuild and Start Servers (IN WSL, BOTH COMPUTERS)

```bash
cd ~/mini-2

# Stop any running servers
pkill -9 mini2_server

# Rebuild with updated config
cd build
cmake ..
make -j$(nproc)

# Start servers on Computer 1
cd src/cpp
./mini2_server A > /tmp/server_A.log 2>&1 &
./mini2_server B > /tmp/server_B.log 2>&1 &
./mini2_server D > /tmp/server_D.log 2>&1 &

# Start servers on Computer 2
cd src/cpp
./mini2_server C > /tmp/server_C.log 2>&1 &
./mini2_server E > /tmp/server_E.log 2>&1 &
./mini2_server F > /tmp/server_F.log 2>&1 &

# Verify all running
ps aux | grep mini2_server
```

---

## Test Connectivity

### From Computer 1 WSL

```bash
# Test local server
telnet 172.26.247.68 50050

# Test remote server (Computer 2)
telnet 172.22.223.237 50052
```

### From Computer 2 WSL

```bash
# Test local server
telnet 172.22.223.237 50052

# Test remote server (Computer 1)
telnet 172.26.247.68 50050
```

All telnet tests should **connect immediately** if everything is configured correctly.

---

## Run Full Test Suite

Once connectivity works:

```bash
cd ~/mini-2

# Quick test all phases
./scripts/run_all_tests.sh

# Comprehensive benchmarks
./scripts/comprehensive_test.sh
```

---

## Troubleshooting

### If WSL still can't reach gateway after NAT switch:

```bash
# Check if WSL is actually in NAT mode
cat /proc/sys/net/ipv4/ip_forward  # Should be 1

# Restart network stack
sudo ip link set eth0 down
sudo ip link set eth0 up

# Flush DNS
sudo resolvconf -u
```

### If port forwarding doesn't work:

```powershell
# Remove all port forwards
netsh interface portproxy reset

# Re-add them one by one
netsh interface portproxy add v4tov4 listenport=50050 listenaddress=0.0.0.0 connectport=50050 connectaddress=<WSL_IP>
```

### If servers still bind to specific IP:

Check the server code to ensure it's binding to `0.0.0.0:PORT` not `specific_ip:PORT`.

---

## Quick Commands Summary

**ON BOTH COMPUTERS:**

1. Create `.wslconfig` with NAT mode (Windows PowerShell as Admin)
2. Run `wsl --shutdown` and restart WSL
3. Verify WSL networking: `ping 8.8.8.8`
4. Setup port forwarding (PowerShell as Admin)
5. Update `config/network_setup.json` with Windows IPs
6. Rebuild and start servers in WSL
7. Test with telnet between computers
8. Run test suite

After these steps, your distributed system should work across the two computers!
