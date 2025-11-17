# QUICK START GUIDE - Cross-Computer Testing

## ✅ System Configuration

**Computer 1 (Windows IP: 192.168.137.189, WSL IP: 192.168.137.169)**
- Servers: A (50050), B (50051), D (50053)
- Role: Gateway + Green Team Leader + Pink Team Worker

**Computer 2 (Windows IP: 192.168.137.1, WSL IP: 169.254.233.33)**
- Servers: C (50052), E (50054), F (50055)  
- Role: Green Worker + Pink Team Leader + Pink Worker

**Network:** Direct Ethernet connection (192.168.137.x subnet)

---

## 🚀 Quick Start Commands

### On Both Computers:

```bash
# Navigate to project
cd ~/dev/mini-2  # Computer 1
# OR
cd ~/mini-2      # Computer 2

# Start all servers
./scripts/start_servers.sh

# Verify connectivity
./scripts/diagnose_and_fix.sh
```

Both should show: **✅ ALL SYSTEMS GO!**

---

## 🧪 Run Tests (Computer 1)

### Phase 1: Basic Connectivity
```bash
./build/src/cpp/mini2_client --server 192.168.137.169:50050 --mode ping
```

### Phase 2: Request Forwarding (No Dataset)
```bash
./build/src/cpp/mini2_client --server 192.168.137.169:50050 --mode request --query "test distributed system"
```

### Phase 2: With Dataset
```bash
./build/src/cpp/mini2_client --server 192.168.137.169:50050 --mode request --query "./test_data/small_test.csv"
```

### Phase 3: Chunked Response
```bash
./build/src/cpp/mini2_client --server 192.168.137.169:50050 --mode session --query "./test_data/medium_data.csv"
```

### Phase 4: Shared Memory (Check Coordination)
```bash
# On Computer 1
./build/src/cpp/inspect_shm shm_host1

# On Computer 2  
./build/src/cpp/inspect_shm shm_host2
```

---

## 📊 Comprehensive Testing

### Run All Tests Automatically
```bash
./scripts/comprehensive_test.sh
```

This tests:
- ✅ All 4 phases
- ✅ Cross-computer forwarding
- ✅ Both Green and Pink teams
- ✅ Autonomous behavior
- ✅ Shared memory coordination

---

## 🔍 Monitoring

### Watch Server Logs (Real-time)
```bash
# Computer 1
tail -f logs/server_A.log logs/server_B.log logs/server_D.log

# Computer 2
tail -f logs/server_C.log logs/server_E.log logs/server_F.log
```

### Check Shared Memory
```bash
# Computer 1: Shows A, B, D status
./build/src/cpp/inspect_shm shm_host1

# Computer 2: Shows C, E, F status
./build/src/cpp/inspect_shm shm_host2
```

---

## 🎯 What to Demonstrate

### 1. Cross-Computer Request Forwarding
- Client → A (Computer 1)
- A → B (Computer 1) AND E (Computer 2) ✅
- B → C (Computer 2) ✅
- E → D & F (Computer 2) ✅

### 2. Autonomous Health Checks
- Each server pings neighbors every 10s
- Logs show: "✓ neighbor - healthy" or "✗ neighbor - unreachable"

### 3. Shared Memory Coordination (Phase 4)
- Computer 1: A, B, D share shm_host1
- Computer 2: C, E, F share shm_host2
- Updates every 2 seconds with process status

### 4. Load Distribution
- Green Team: B → C
- Pink Team: E → D, F
- Work distributed across both computers

---

## 📝 Performance Metrics to Report

### Network Latency
```bash
# From Computer 1 to Computer 2
ping -c 100 192.168.137.1 | tail -1
```

### Request Processing Time
Check logs for:
- Request arrival time
- Forwarding time
- Worker processing time
- Response aggregation time

### Throughput
Send multiple concurrent requests:
```bash
for i in {1..10}; do
  ./build/src/cpp/mini2_client --server 192.168.137.169:50050 --mode request --query "test_$i" &
done
wait
```

---

## 🛠️ Troubleshooting

### If connectivity fails:
```bash
# 1. Check servers running
ps aux | grep mini2_server

# 2. Check ports listening  
netstat -tlnp | grep mini2_server

# 3. Test cross-computer connectivity
nc -zv 192.168.137.1 50052      # From Computer 1
nc -zv 192.168.137.169 50050    # From Computer 2

# 4. Re-run diagnostics
./scripts/diagnose_and_fix.sh
```

### If WSL IP changes:
1. Check new IP: `hostname -I`
2. Update port forwarding (see CROSS_COMPUTER_TROUBLESHOOTING.md)
3. Restart servers

---

## 📦 Files for Report

### Screenshots to Capture:
1. Diagnostic script output (both computers showing ✅)
2. Client request output (showing distributed processing)
3. Server logs (showing cross-computer forwarding)
4. Shared memory output (inspect_shm)
5. Health check logs (autonomous behavior)

### Log Files to Include:
- `logs/server_A.log` - Gateway logs
- `logs/server_E.log` - Remote team leader logs
- Client output showing cross-computer flow

### Configuration Files:
- `config/network_setup.json` - Network topology
- Port forwarding setup (PowerShell commands)
- Firewall configuration

---

## ✨ Key Achievement

Successfully deployed a **distributed hierarchical system** across:
- ✅ 2 physical Windows computers
- ✅ WSL networking with port forwarding
- ✅ 6 servers (3 on each computer)
- ✅ Cross-computer request forwarding
- ✅ Shared memory coordination (per host)
- ✅ Autonomous health monitoring

**Total deployment time from repository clone to working system: ~2 hours** 🎉
