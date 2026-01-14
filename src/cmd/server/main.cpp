// main_server.cpp
#include <iostream>
#include "network/franco_server.h"
#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/config.h"
#include "common/franco_net_config.h"

using namespace francodb;

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "     FRANCO DB SERVER v2.0 (Multi-Protocol)" << std::endl;
    std::cout << "==========================================" << std::endl;

    try {
        // Core setup
        auto disk_manager = std::make_unique<DiskManager>("francodb.db");
        auto bpm = std::make_unique<BufferPoolManager>(
            BUFFER_POOL_SIZE, disk_manager.get());
        auto catalog = std::make_unique<Catalog>(bpm.get());
        
        // Server with multiple protocol support
        FrancoServer server(bpm.get(), catalog.get());
        
        // Configuration from file/env
        int port = net::DEFAULT_PORT;
        // Could read from config: config::GetServerPort()
        
        server.Start(port);
        
        // Server runs until shutdown
        // Cleanup happens automatically with smart pointers
        
    } catch (const std::exception &e) {
        std::cerr << "[CRASH] Server failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}