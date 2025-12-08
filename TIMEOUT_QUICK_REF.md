# Configurable Timeouts - Quick Reference

## Environment Variables

| Variable | Purpose | Default | Recommended Range |
|----------|---------|---------|-------------------|
| `MINI3_LEADER_TIMEOUT_MS` | Global leader wait for teams | 12000ms | 10000-60000ms |
| `MINI3_TEAMLEADER_TIMEOUT_MS` | Team leader wait for workers | 10000ms | 8000-50000ms |

## Common Scenarios

### Fault Tolerance Experiments (Default)
```bash
# No env vars needed - uses defaults
./scripts/start_servers.sh
```
**Timeouts**: Leader=12s, TeamLeader=10s  
**Best for**: Crash/slow worker scenarios, fast failure detection

### Multi-Client Success Demo
```bash
export MINI3_LEADER_TIMEOUT_MS=30000
export MINI3_TEAMLEADER_TIMEOUT_MS=25000
./scripts/start_servers.sh
./scripts/run_multi_clients.sh --dataset test_data/data_1m.csv --clients 4
```
**Timeouts**: Leader=30s, TeamLeader=25s  
**Best for**: 4+ concurrent clients, large datasets

### Heavy Load Testing
```bash
export MINI3_LEADER_TIMEOUT_MS=60000
export MINI3_TEAMLEADER_TIMEOUT_MS=50000
./scripts/start_servers.sh
```
**Timeouts**: Leader=60s, TeamLeader=50s  
**Best for**: 10M+ rows, many concurrent clients

## Verification

Check configured timeouts in logs:
```bash
grep "timeout" /tmp/mini3_*/server.log
```

Expected output:
```
[A] [Leader] Using leader timeout = 30000 ms
[B] [TeamLeader] Using worker wait timeout = 25000 ms
[E] [TeamLeader] Using worker wait timeout = 25000 ms
```

## Guidelines

✓ **Always set Leader timeout > TeamLeader timeout** (2-5 second gap recommended)  
✓ **Start with defaults** for most experiments  
✓ **Increase for multi-client** scenarios (30s/25s recommended)  
✓ **Check logs** to verify values are applied  
✓ **Restart servers** after changing env vars  

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Values not taking effect | Ensure env vars set before starting servers |
| Clients timing out | Increase both timeout values |
| Partial results | Expected with short timeouts + failures |
| Invalid value | Use positive integers only (milliseconds) |

## Implementation Notes

- **Thread-safe**: Initialized at program startup
- **Backward compatible**: Defaults match previous hardcoded values
- **Logged at startup**: Values visible in server logs
- **No code changes**: Configure via env vars only
