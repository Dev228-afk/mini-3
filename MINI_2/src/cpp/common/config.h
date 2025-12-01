
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct NodeInfo {
    std::string id;
    std::string role;
    std::string host;
    int port{};
    std::string team;
};

struct Overlay {
    std::vector<std::pair<std::string,std::string>> edges;
};

struct SharedSegment {
    std::string name;
    std::vector<std::string> members;
};

struct NetworkConfig {
    std::unordered_map<std::string, NodeInfo> nodes;
    Overlay overlay;
    std::string client_gateway;
    std::vector<SharedSegment> segments;
};

NetworkConfig LoadConfig(const std::string& path);
