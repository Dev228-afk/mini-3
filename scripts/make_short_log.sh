#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <combined-slice.log> [output-file]" >&2
  exit 1
fi

INPUT="$1"

if [[ ! -f "$INPUT" ]]; then
  echo "Error: Input file not found: $INPUT" >&2
  exit 1
fi

if [[ $# -ge 2 ]]; then
  OUTPUT="$2"
else
  # Default: strip .log and append -short.log
  BASE="${INPUT%.log}"
  OUTPUT="${BASE}-short.log"
fi

awk '
# Always keep header and structure lines
/^#/ { print; next }
/^=====/ { print; next }

# Drop noisy lines
/WorkerLoop] No tasks available/ { next }
/RequestTask from/ { next }
/RequestTaskForWorker/ { next }
/NodeControl] Ping from/ { next }
/Updated stats for/ { next }
/socket_utils_common_posix.cc/ { next }
/SO_REUSEPORT/ { next }

# Keep high-signal lines
/📦 PROCESSING DATASET:/ { print; next }
/Testing Strategy B: GetNext/ { print; next }
/Strategy B \(GetNext\) Results:/ { print; next }
/Total chunks:/ { print; next }
/Total time:/ { print; next }
/Time to first chunk:/ { print; next }
/\[ClientGateway] start:/ { print; next }
/\[ClientGateway] GetNext:/ { print; next }
/\[ClientGateway] background processing/ { print; next }
/\[ClientGateway] background done/ { print; next }
/\[SessionManager]/ { print; next }
/\[Leader]/ { print; next }
/\[TeamLeader/ { print; next }
/\[TeamIngress] PushWorkerResult/ { print; next }
/\[RequestProcessor] Assigned task/ { print; next }
/\[Worker] Processing task test-strategyB-getnext/ { print; next }
/\[WorkerLoop] Finished task test-strategyB-getnext/ { print; next }
/\[RequestProcessor] Loading dataset:/ { print; next }
/\[DataProcessor] loading/ { print; next }
/\[DataProcessor] Progress:/ { print; next }
/\[DataProcessor] loaded/ { print; next }
/\[RequestProcessor] Dataset loaded successfully/ { print; next }
/\[DataProcessor] chunk start=/ { print; next }
/\[DataProcessor] processed=/ { print; next }
' "$INPUT" > "$OUTPUT"

echo "$OUTPUT"
