#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include "network/franco_client.h"
#include "common/franco_net_config.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace francodb;

// -----------------------------------------------------------------------------
// HELPER FUNCTIONS
// -----------------------------------------------------------------------------

// Check Admin Privileges (Windows Only)
bool IsAdmin() {
#ifdef _WIN32
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
            fRet = Elevation.TokenIsElevated;
        }
    }
    if (hToken) CloseHandle(hToken);
    return fRet;
#else
    return geteuid() == 0;
#endif
}

// Print Usage / Help Menu
void PrintUsage() {
    std::cout << "Usage:" << std::endl;
    std::cout << "  francodb server start         Start the database service" << std::endl;
    std::cout << "  francodb server stop          Stop the database service" << std::endl;
    std::cout << "  francodb server restart       Restart the database service" << std::endl;
    std::cout << "  francodb login <url>          Connect using a URL" << std::endl;
    std::cout << "  francodb <url>                Connect using a URL (Short form)" << std::endl;
    std::cout << "  francodb                      Interactive manual login" << std::endl;
    std::cout << "\nConnection URL Format:" << std::endl;
    std::cout << "  maayn://user:pass@host:port/db" << std::endl;
}

// Print Welcome Banner
void PrintWelcome() {
    std::cout << "==========================================" << std::endl;
    std::cout << "            FrancoDB Shell v2.0           " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Type 'exit' to quit." << std::endl;
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    FrancoClient db_client;
    std::string username = net::DEFAULT_ADMIN_USERNAME;
    std::string current_db = "default";
    bool connected = false;

    // -------------------------------------------------------------------------
    // ARGUMENT PARSING
    // -------------------------------------------------------------------------
    if (argc > 1) {
        std::string arg1 = argv[1];
        std::string arg2 = (argc > 2) ? argv[2] : "";
        
        // Normalize for case-insensitive comparison
        std::string cmd1 = arg1; 
        std::string cmd2 = arg2;
        std::transform(cmd1.begin(), cmd1.end(), cmd1.begin(), ::tolower);
        std::transform(cmd2.begin(), cmd2.end(), cmd2.begin(), ::tolower);

        // 1. SERVICE COMMANDS (start server, server start, etc.)
        bool is_service_cmd = false;
        std::string action = "";

        if (cmd2 == "server" || cmd2 == "service") { action = cmd1; is_service_cmd = true; }
        else if (cmd1 == "server" || cmd1 == "service") { action = cmd2; is_service_cmd = true; }

        if (is_service_cmd) {
            if (!IsAdmin()) {
                std::cerr << "[ERROR] Access Denied. Run as Administrator." << std::endl;
                return 1;
            }

            if (action == "start") {
                std::cout << "[INFO] Starting Service..." << std::endl;
                #ifdef _WIN32
                return system("net start FrancoDBService");
                #else
                return system("systemctl start francodb");
                #endif
            } 
            else if (action == "stop") {
                std::cout << "[INFO] Stopping Service..." << std::endl;
                #ifdef _WIN32
                return system("net stop FrancoDBService");
                #else
                return system("systemctl stop francodb");
                #endif
            } 
            else if (action == "restart") {
                std::cout << "[INFO] Restarting Service..." << std::endl;
                #ifdef _WIN32
                system("net stop FrancoDBService");
                Sleep(2000);
                return system("net start FrancoDBService");
                #else
                return system("systemctl restart francodb");
                #endif
            }
            else {
                std::cerr << "[ERROR] Unknown service command: " << action << std::endl;
                PrintUsage();
                return 1;
            }
        }

        // 2. LOGIN COMMAND (francodb login maayn://...)
        else if (cmd1 == "login") {
            if (arg2.empty()) {
                std::cerr << "[ERROR] Missing connection URL." << std::endl;
                std::cout << "Usage: francodb login maayn://user:pass@host/db" << std::endl;
                return 1;
            }

            if (arg2.find("maayn://") != 0) {
                std::cerr << "[FATAL] Invalid Protocol. URL must start with 'maayn://'" << std::endl;
                return 1;
            }

            if (!db_client.ConnectFromString(arg2)) {
                std::cerr << "[FATAL] Connection Failed." << std::endl;
                return 1;
            }
            connected = true;
            // Parse info for prompt
            size_t u_start = arg2.find("://") + 3;
            size_t u_end = arg2.find(':', u_start);
            if (u_end != std::string::npos) username = arg2.substr(u_start, u_end - u_start);
            if (arg2.find_last_of('/') > arg2.find('@')) current_db = arg2.substr(arg2.find_last_of('/') + 1);
        }

        // 3. DIRECT URL (francodb maayn://...)
        else if (arg1.find("maayn://") == 0) {
            if (!db_client.ConnectFromString(arg1)) {
                std::cerr << "[FATAL] Connection Failed." << std::endl;
                return 1;
            }
            connected = true;
            // Parse info for prompt
            size_t u_start = arg1.find("://") + 3;
            size_t u_end = arg1.find(':', u_start);
            if (u_end != std::string::npos) username = arg1.substr(u_start, u_end - u_start);
            if (arg1.find_last_of('/') > arg1.find('@')) current_db = arg1.substr(arg1.find_last_of('/') + 1);
        }

        // 4. UNKNOWN COMMAND
        else {
            std::cerr << "[ERROR] Unknown command: " << arg1 << std::endl;
            PrintUsage();
            return 1;
        }
    }

    // -------------------------------------------------------------------------
    // INTERACTIVE MODE
    // -------------------------------------------------------------------------
    
    // Only print welcome if we are entering the shell
    PrintWelcome();

    if (!connected) {
        std::string password, host, port_str;
        int port = net::DEFAULT_PORT;
        
        std::cout << "\nUsername: ";
        if (!std::getline(std::cin, username)) return 0;
        std::cout << "Password: ";
        if (!std::getline(std::cin, password)) return 0;
        
        std::cout << "Host [localhost]: ";
        std::getline(std::cin, host);
        if (host.empty()) host = "127.0.0.1";
        
        std::cout << "Port [2501]: ";
        std::getline(std::cin, port_str);
        if (!port_str.empty()) try { port = std::stoi(port_str); } catch(...) {}
        
        if (!db_client.Connect(host, port, username, password)) {
            std::cerr << "[FATAL] Connection Failed." << std::endl;
            return 1;
        }
        connected = true;
    }

    // -------------------------------------------------------------------------
    // SHELL LOOP
    // -------------------------------------------------------------------------
    std::string input;
    while (true) {
        std::cout << username << "@" << current_db << "> ";
        std::cout.flush();
        if (!std::getline(std::cin, input)) break;
        if (input == "exit" || input == "quit") break;
        if (input.empty()) continue;

        // Client-side prompt update for USE command
        if (input.rfind("USE ", 0) == 0) {
            std::string db = input.substr(4);
            if (!db.empty() && db.back() == ';') db.pop_back();
            current_db = db;
        }

        std::string response = db_client.Query(input);
        std::cout << response << std::endl;
    }

    db_client.Disconnect();
    return 0;
}