#include <iostream>
#include <string>
#include <algorithm>
#include "network/franco_client.h"
#include "common/franco_net_config.h"

using namespace francodb;

void PrintWelcome() {
    std::cout << "==========================================" << std::endl;
    std::cout << "            FrancoDB Shell v2.0           " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Connection string format:" << std::endl;
    std::cout << "  maayn://user:pass@host:port/dbname" << std::endl;
    std::cout << "  maayn://user:pass@host/dbname     (default port 2501)" << std::endl;
    std::cout << "  maayn://user:pass@host            (no database)" << std::endl;
    std::cout << "Or enter credentials manually." << std::endl;
    std::cout << "Commands: exit | USE <db>; | CREATE DATABASE <db>; | SELECT/INSERT/..." << std::endl;
}

int main(int argc, char* argv[]) {
    FrancoClient db_client;
    std::string username = net::DEFAULT_ADMIN_USERNAME;
    std::string current_db = "default";
    bool connected = false;

    PrintWelcome();

    // Check if connection string provided as argument
    if (argc > 1) {
        std::string conn_str = argv[1];
        if (conn_str.find("maayn://") == 0) {
            if (db_client.ConnectFromString(conn_str)) {
                connected = true;
                // Extract username from connection string for display
                size_t user_start = conn_str.find("://") + 3;
                size_t user_end = conn_str.find('@', user_start);
                if (user_end != std::string::npos) {
                    username = conn_str.substr(user_start, user_end - user_start);
                    size_t colon = username.find(':');
                    if (colon != std::string::npos) {
                        username = username.substr(0, colon);
                    }
                }
                // Extract database from connection string (only if there is a path part after host)
                size_t at_pos = conn_str.find('@');
                size_t db_start = conn_str.find_last_of('/');
                if (at_pos != std::string::npos &&
                    db_start != std::string::npos &&
                    db_start > at_pos &&
                    db_start + 1 < conn_str.length()) {
                    current_db = conn_str.substr(db_start + 1);
                }
            } else {
                std::cerr << "[FATAL] Invalid connection string or connection failed." << std::endl;
                return 1;
            }
        }
    }

    // Manual connection if not connected via argument
    if (!connected) {
        std::string password;
        std::cout << "\nConnection string (or press Enter for manual): ";
        std::string conn_input;
        std::getline(std::cin, conn_input);
        
        // Trim whitespace
        while (!conn_input.empty() && (conn_input.front() == ' ' || conn_input.front() == '\t')) {
            conn_input.erase(0, 1);
        }
        while (!conn_input.empty() && (conn_input.back() == ' ' || conn_input.back() == '\t' || conn_input.back() == '\r')) {
            conn_input.pop_back();
        }
        
        if (!conn_input.empty() && conn_input.find("maayn://") == 0) {
            if (!db_client.ConnectFromString(conn_input)) {
                std::cerr << "[FATAL] Invalid connection string or connection failed." << std::endl;
                std::cerr << "Make sure:" << std::endl;
                std::cerr << "  1. The server is running (francodb_server)" << std::endl;
                std::cerr << "  2. The connection string format is correct" << std::endl;
                std::cerr << "  3. The server is listening on port 2501" << std::endl;
                return 1;
            }
            // Extract username and database from connection string
            size_t user_start = conn_input.find("://") + 3;
            size_t user_end = conn_input.find('@', user_start);
            if (user_end != std::string::npos) {
                username = conn_input.substr(user_start, user_end - user_start);
                size_t colon = username.find(':');
                if (colon != std::string::npos) {
                    username = username.substr(0, colon);
                }
            }
            size_t at_pos2 = conn_input.find('@');
            size_t db_start = conn_input.find_last_of('/');
            if (at_pos2 != std::string::npos &&
                db_start != std::string::npos &&
                db_start > at_pos2 &&
                db_start + 1 < conn_input.length()) {
                current_db = conn_input.substr(db_start + 1);
            }
        } else {
            // Manual credentials
            std::cout << "Username: ";
            std::getline(std::cin, username);
            std::cout << "Password: ";
            std::getline(std::cin, password);
            
            if (!db_client.Connect(net::DEFAULT_SERVER_IP, net::DEFAULT_PORT, username, password)) {
                std::cerr << "[FATAL] Could not connect/authenticate to FrancoDB server." << std::endl;
                return 1;
            }
        }
    }

    std::string input;
    while (true) {
        std::cout << username << "@" << current_db << "> ";
        if (!std::getline(std::cin, input) || input == "exit") break;
        if (input.empty()) continue;

        // Track USE <db>;
        if (input.rfind("USE ", 0) == 0 || input.rfind("2ESTA5DEM ", 0) == 0) {
            // naive parse: USE <db>;
            std::string db = input.substr(input.find(' ') + 1);
            if (!db.empty() && db.back() == ';') db.pop_back();
            // Trim whitespace
            while (!db.empty() && (db.front() == ' ' || db.front() == '\t')) db.erase(0, 1);
            while (!db.empty() && (db.back() == ' ' || db.back() == '\t')) db.pop_back();
            current_db = db;
        }

        // Use the Driver API
        std::string response = db_client.Query(input);
        
        // Update current_db if USE command was successful
        if (response.rfind("Using database: ", 0) == 0) {
            current_db = response.substr(16); // "Using database: ".length()
            current_db.erase(std::remove(current_db.begin(), current_db.end(), '\n'), current_db.end());
        }
        
        std::cout << response << std::endl;
    }

    db_client.Disconnect();
    return 0;
}