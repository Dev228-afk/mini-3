
#include "config.h"
#include <fstream>
#include <iostream>
#include "nlohmann/json.hpp"
using json = nlohmann::json;

NetworkConfig LoadConfig(const std::string& path){
    NetworkConfig out;
    std::ifstream f(path);
    if (!f.is_open() || f.peek() == std::ifstream::traits_type::eof()) {
        std::cerr << "Error: Cannot read config file: " << path << std::endl;
        return out; // Return empty config
    }
    json j; f >> j;

    for (auto &n : j["nodes"]) {
        NodeInfo ni;
        ni.id = n["id"]; ni.role = n["role"]; ni.host = n["host"]; ni.port = n["port"]; ni.team = n["team"];
        out.nodes[ni.id] = ni;
    }
    for (auto &e : j["overlay"]) {
        out.overlay.edges.emplace_back(e[0], e[1]);
    }
    out.client_gateway = j["client_gateway"];
    for (auto &s : j["shared_memory"]["segments"]) {
        SharedSegment seg;
        seg.name = s["name"];
        for (auto &m : s["members"]) seg.members.push_back(m);
        out.segments.push_back(seg);
    }
    return out;
}
