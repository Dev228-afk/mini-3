#!/usr/bin/env python3

import grpc, argparse, time, json, sys, os
from concurrent.futures import ThreadPoolExecutor

# Add project root to path
project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
sys.path.insert(0, project_root)

from python.common import minitwo_pb2 as pb
from python.common import minitwo_pb2_grpc as rpc

class NodeControl(rpc.NodeControlServicer):
    """Handles health checks and control operations"""
    def __init__(self, node_id):
        self.node_id = node_id
        self.start_time = time.time()
        
    def Ping(self, request, context):
        return pb.HeartbeatAck(ok=True)
    
    def Status(self, request, context):
        uptime = int(time.time() - self.start_time)
        return pb.StatusResponse(
            node_id=self.node_id,
            state="IDLE",
            queue_size=0,
            uptime_seconds=uptime,
            requests_processed=0,
            memory_bytes=0
        )

class TeamIngress(rpc.TeamIngressServicer):
    """Handles requests from team leaders"""
    def __init__(self, node_id):
        self.node_id = node_id
        self.request_count = 0
        
    def HandleRequest(self, request, context):
        self.request_count += 1
        print(f"[{self.node_id}] Received HandleRequest: {request.request_id}")
        print(f"  Query: {request.query}")
        print(f"  Needs: green={request.need_green}, pink={request.need_pink}")
        return pb.HeartbeatAck(ok=True)
    
    def PushWorkerResult(self, request, context):
        print(f"[{self.node_id}] Received PushWorkerResult: {request.request_id}")
        return pb.HeartbeatAck(ok=True)

class ClientGateway(rpc.ClientGatewayServicer):
    """Handles client requests (only for Node A)"""
    def __init__(self, node_id):
        self.node_id = node_id
        self.sessions = {}
        
    def OpenSession(self, request, context):
        session_id = f"py-session-{int(time.time())}"
        self.sessions[session_id] = {"created": time.time()}
        print(f"[{self.node_id}] OpenSession: {session_id}")
        return pb.HeartbeatAck(ok=True)
    
    def GetNext(self, request, context):
        print(f"[{self.node_id}] GetNext: {request.request_id}")
        return pb.NextChunkResp(
            request_id=request.request_id, 
            has_more=False,
            chunk=b"Python server response"
        )
    
    def StartRequest(self, request, context):
        req_id = request.request_id or f"py-req-{int(time.time())}"
        print(f"[{self.node_id}] StartRequest: {req_id}")
        print(f"  Query: {request.query}")
        return pb.SessionOpen(request_id=req_id)
    
    def PollNext(self, request, context):
        print(f"[{self.node_id}] PollNext: {request.request_id}")
        return pb.PollResp(
            request_id=request.request_id, 
            ready=True, 
            has_more=False
        )
    
    def RequestOnce(self, request, context):
        print(f"[{self.node_id}] RequestOnce: {request.query}")
        return pb.AggregatedResult(
            request_id=f"py-{int(time.time())}", 
            total_rows=42,
            total_bytes=1024
        )
    
    def CloseSession(self, request, context):
        if request.request_id in self.sessions:
            del self.sessions[request.request_id]
        print(f"[{self.node_id}] CloseSession: {request.request_id}")
        return pb.HeartbeatAck(ok=True)

def main():
    ap = argparse.ArgumentParser(description="Python gRPC Mini2 Server")
    ap.add_argument("--bind", default="0.0.0.0:60000", help="Address to bind (default: 0.0.0.0:60000)")
    ap.add_argument("--isA", action="store_true", help="Act as Node A (gateway)")
    ap.add_argument("--node-id", default="PY", help="Node ID for logging")
    ap.add_argument("--config", help="Path to network_setup.json (optional)")
    args = ap.parse_args()

    # Load config if provided
    if args.config:
        try:
            with open(args.config) as f:
                config = json.load(f)
                print(f"[{args.node_id}] Loaded config: {args.config}")
        except Exception as e:
            print(f"[{args.node_id}] Could not load config: {e}")

    # Create server
    server = grpc.server(ThreadPoolExecutor(max_workers=8))
    
    # Add services
    rpc.add_NodeControlServicer_to_server(NodeControl(args.node_id), server)
    rpc.add_TeamIngressServicer_to_server(TeamIngress(args.node_id), server)
    
    if args.isA:
        rpc.add_ClientGatewayServicer_to_server(ClientGateway(args.node_id), server)
        print(f"[{args.node_id}] Running as GATEWAY (Node A)")
    else:
        print(f"[{args.node_id}] Running as WORKER")
    
    server.add_insecure_port(args.bind)
    server.start()
    print(f"[{args.node_id}] Python server listening at {args.bind}")
    print(f"[{args.node_id}] Press Ctrl+C to stop")
    
    try:
        server.wait_for_termination()
    except KeyboardInterrupt:
        print(f"\n[{args.node_id}] Shutting down...")
        server.stop(0)

if __name__ == "__main__":
    main()
