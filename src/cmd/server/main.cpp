#include <iostream>
#include "network/franco_server.h"
#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "common/config.h" // Assuming DB_FILENAME and BUFFER_POOL_SIZE are here

using namespace francodb;

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "          FRANCO DB SERVER v1.0           " << std::endl;
    std::cout << "==========================================" << std::endl;

    try {
        // 1. Core Storage Setup
        auto *disk_manager = new DiskManager("francodb.db");
        auto *bpm = new BufferPoolManager(100, disk_manager);
        auto *catalog = new Catalog(bpm);

        // 2. Start the Network Service
        FrancoServer server(bpm, catalog);
        server.Start(); // This blocks until shutdown

        // 3. Cleanup (if loop breaks)
        delete catalog;
        delete bpm;
        delete disk_manager;

    } catch (const std::exception &e) {
        std::cerr << "[CRASH] Server failed to start: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}