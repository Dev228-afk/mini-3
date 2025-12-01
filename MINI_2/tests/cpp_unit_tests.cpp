
#include <cassert>
#include <iostream>
#include "../src/cpp/common/config.h"

int main(){
    std::vector<std::string> paths = {"config/network_setup.json", "../config/network_setup.json"};
    NetworkConfig cfg;
    bool loaded = false;
    for (const auto& path : paths) {
        try {
            cfg = LoadConfig(path);
            if (!cfg.nodes.empty()) {
                loaded = true;
                break;
            }
        } catch (...) { continue; }
    }
    if (!loaded) {
        std::cerr << "Failed to load config" << std::endl;
        return 1;
    }
    assert(cfg.nodes.size()==6);
    return 0;
}
