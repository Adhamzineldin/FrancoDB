#include <iostream>
#include <string>
#include <chrono>
#include <vector>

// FIX: Include Exception so we can catch errors
#include "common/exception.h"

#include "storage/disk/disk_manager.h"
#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "parser/parser.h"
#include "execution/execution_engine.h"

using namespace francodb;

void PrintWelcome() {
    std::cout << "==========================================" << std::endl;
    std::cout << "        WELCOME TO FRANCO DB (v1.0)       " << std::endl;
    std::cout << "   'The First Egyptian Database Engine'   " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Type 'exit' or '\\q' to quit." << std::endl;
    std::cout << std::endl;
}

void PrintPrompt() {
    std::cout << "FrancoDB> ";
}

int main() {
    // 1. Initialize the Engine
    // We persist data to "franco.db"
    std::string db_file = "franco.francodb";
    auto *disk_manager = new DiskManager(db_file);
    // Give the shell plenty of memory (100 pages)
    auto *bpm = new BufferPoolManager(100, disk_manager); 
    auto *catalog = new Catalog(bpm);
    ExecutionEngine engine(bpm, catalog);

    PrintWelcome();

    std::string input_sql;
    
    // 2. The Main Loop
    while (true) {
        PrintPrompt();
        
        if (!std::getline(std::cin, input_sql)) {
            break; 
        }

        if (input_sql.empty()) continue;
        if (input_sql == "exit" || input_sql == "\\q") {
            break;
        }

        try {
            auto start = std::chrono::high_resolution_clock::now();

            // A. Lex & Parse
            Lexer lexer(input_sql);
            Parser parser(std::move(lexer));
            auto stmt = parser.ParseQuery();

            // B. Execute
            engine.Execute(stmt.get());

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            
            std::cout << "(Time: " << elapsed.count() << "s)" << std::endl;
            std::cout << std::endl;

        } catch (const Exception &e) {
            std::cerr << "[ERROR] " << e.what() << std::endl << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "[SYSTEM ERROR] " << e.what() << std::endl << std::endl;
        }
    }

    std::cout << "Ma3a Salama! (Goodbye)" << std::endl;

    delete catalog;
    delete bpm;
    delete disk_manager;

    return 0;
}