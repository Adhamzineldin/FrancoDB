#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>
#include <random>
#include <iomanip>
#include <sstream>

#include "network/franco_client.h"
#include "common/franco_net_config.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace francodb;
namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// HELPER FUNCTIONS
// -----------------------------------------------------------------------------

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

std::string GetExecutableDir() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().string();
#else
    return fs::current_path().string();
#endif
}

std::string GenerateKey() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    return ss.str();
}

// -----------------------------------------------------------------------------
// SETUP WIZARD LOGIC
// -----------------------------------------------------------------------------
void RunSetupWizard(const std::string& configPath) {
    std::cout << "\n" << std::string(50, '=') << std::endl;
    std::cout << "      FRANCO DB CONFIGURATION WIZARD" << std::endl;
    std::cout << std::string(50, '=') << std::endl;

    std::string input;
    
    // 1. PORT
    int port = 2501;
    std::cout << "Server Port [2501]: ";
    std::getline(std::cin, input);
    if (!input.empty()) try { port = std::stoi(input); } catch(...) {}

    // 2. USERNAME
    std::string user = "maayn";
    std::cout << "Root Username [maayn]: ";
    std::getline(std::cin, input);
    if (!input.empty()) user = input;

    // 3. PASSWORD
    std::string pass = "root";
    std::cout << "Root Password [root]: ";
    std::getline(std::cin, input);
    if (!input.empty()) pass = input;

    // 4. DATA DIR
    std::string dataDir = "./data";
    std::cout << "Data Directory [./data]: ";
    std::getline(std::cin, input);
    if (!input.empty()) dataDir = input;

    // 5. ENCRYPTION
    bool use_enc = false;
    std::string key = "";
    
    std::cout << "\n[Encryption Setup]" << std::endl;
    std::cout << "1. Disable Encryption (Default)" << std::endl;
    std::cout << "2. Enable (Auto-Generate Key)" << std::endl;
    std::cout << "3. Enable (Input My Own Key)" << std::endl;
    std::cout << "Choice [1]: ";
    std::getline(std::cin, input);
    
    if (input == "2") {
        use_enc = true;
        key = GenerateKey();
        std::cout << " -> Generated Key: " << key << std::endl;
        std::cout << " -> [IMPORTANT] Save this key! If you lose it, data is lost." << std::endl;
    } else if (input == "3") {
        use_enc = true;
        std::cout << "Enter Encryption Key (32+ chars recommended): ";
        std::getline(std::cin, key);
    }

    // 6. SAVE
    std::ofstream file(configPath);
    if (file.is_open()) {
        file << "# FrancoDB Configuration\n";
        file << "port = " << port << "\n";
        file << "root_username = \"" << user << "\"\n";
        file << "root_password = \"" << pass << "\"\n";
        file << "data_directory = \"" << dataDir << "\"\n";
        file << "encryption_enabled = " << (use_enc ? "true" : "false") << "\n";
        if (use_enc) file << "encryption_key = \"" << key << "\"\n";
        
        std::cout << "\n[SUCCESS] Configuration saved to: " << configPath << std::endl;
        std::cout << "Please restart the server to apply changes." << std::endl;
    } else {
        std::cerr << "[ERROR] Could not write config file!" << std::endl;
    }
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    FrancoClient db_client;
    std::string username = net::DEFAULT_ADMIN_USERNAME;
    std::string current_db = "default";
    bool connected = false;

    if (argc > 1) {
        std::string cmd1 = argv[1]; 
        std::string cmd2 = (argc > 2) ? argv[2] : "";
        std::transform(cmd1.begin(), cmd1.end(), cmd1.begin(), ::tolower);
        std::transform(cmd2.begin(), cmd2.end(), cmd2.begin(), ::tolower);

        // --- CONFIG RESET COMMAND ---
        if (cmd1 == "config" && cmd2 == "reset") {
            fs::path config_path = fs::path(GetExecutableDir()) / "francodb.conf";
            RunSetupWizard(config_path.string());
            return 0;
        }

        // --- SERVICE COMMANDS ---
        if (cmd1 == "server" || cmd2 == "server") {
            std::string action = (cmd1 == "server") ? cmd2 : cmd1;
            if (!IsAdmin()) { std::cerr << "Run as Admin required." << std::endl; return 1; }
            
            if (action == "start") return system("net start FrancoDBService");
            if (action == "stop") return system("net stop FrancoDBService");
            if (action == "restart") { system("net stop FrancoDBService"); return system("net start FrancoDBService"); }
        }

        // --- LOGIN COMMANDS ---
        if (cmd1 == "login" || cmd1.find("maayn://") == 0) {
             std::string url = (cmd1 == "login") ? cmd2 : cmd1;
             if (db_client.ConnectFromString(url)) {
                 connected = true;
                 // Parse prompt info for immediate feedback
                 size_t u_start = url.find("://") + 3;
                 size_t u_end = url.find(':', u_start);
                 if (u_end != std::string::npos) username = url.substr(u_start, u_end - u_start);
                 if (url.find_last_of('/') > url.find('@')) current_db = url.substr(url.find_last_of('/') + 1);
             } else {
                 return 1;
             }
        }
    }

    if (!connected) {
        std::cout << "==========================================" << std::endl;
        std::cout << "            FrancoDB Shell v2.1           " << std::endl;
        std::cout << "==========================================" << std::endl;
        
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
    while (connected) {
        std::cout << username << "@" << current_db << "> ";
        std::cout.flush(); // Ensure prompt prints before waiting for input
        
        if (!std::getline(std::cin, input)) break;
        if (input == "exit" || input == "quit") break;
        if (input.empty()) continue;

        // --- [FIX] RESTORED PROMPT UPDATE LOGIC ---
        // Check if command is "USE <db>" to update the prompt immediately
        std::string upper_input = input;
        std::transform(upper_input.begin(), upper_input.end(), upper_input.begin(), ::toupper);
        
        if (upper_input.rfind("USE ", 0) == 0) {
            std::string new_db = input.substr(4);
            // Remove trailing semicolon if present
            if (!new_db.empty() && new_db.back() == ';') {
                new_db.pop_back();
            }
            // Remove extra whitespace
            size_t first = new_db.find_first_not_of(' ');
            if (std::string::npos != first) {
                 size_t last = new_db.find_last_not_of(' ');
                 current_db = new_db.substr(first, (last - first + 1));
            } else {
                 current_db = new_db;
            }
        }
        // ------------------------------------------

        std::cout << db_client.Query(input) << std::endl;
    }

    db_client.Disconnect();
    return 0;
}