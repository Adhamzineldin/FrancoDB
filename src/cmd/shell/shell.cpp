#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>
#include <random>
#include <iomanip>
#include <sstream>

#include "network/chronos_client.h"
#include "common/chronos_net_config.h"
#include "parser/lexer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace chronosdb;
namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// DYNAMIC SYNTAX HELPER
// -----------------------------------------------------------------------------
void DisplayDynamicSyntax() {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "                      CHRONOS DB SYNTAX REFERENCE" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    // Get keywords from lexer
    const auto& keywords = Lexer::GetKeywords();
    
    // Categorize keywords by token type
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> categories;
    
    for (const auto& [franco, type] : keywords) {
        std::string category;
        std::string english = Lexer::GetTokenTypeName(type);
        
        // Categorize based on token type
        switch(type) {
            case TokenType::SELECT:
            case TokenType::FROM:
            case TokenType::WHERE:
            case TokenType::CREATE:
            case TokenType::DELETE_CMD:
            case TokenType::UPDATE_CMD:
            case TokenType::UPDATE_SET:
            case TokenType::INSERT:
            case TokenType::INTO:
            case TokenType::VALUES:
                category = "BASIC COMMANDS";
                break;
                
            case TokenType::DATABASE:
            case TokenType::DATABASES:
            case TokenType::TABLE:
            case TokenType::USE:
            case TokenType::SHOW:
                category = "DATABASE OPERATIONS";
                break;
                
            case TokenType::USER:
            case TokenType::ROLE:
            case TokenType::PASS:
            case TokenType::WHOAMI:
            case TokenType::STATUS:
            case TokenType::LOGIN:
                category = "USER MANAGEMENT";
                break;
                
            case TokenType::ROLE_SUPERADMIN:
            case TokenType::ROLE_ADMIN:
            case TokenType::ROLE_NORMAL:
            case TokenType::ROLE_READONLY:
            case TokenType::ROLE_DENIED:
                category = "USER ROLES";
                break;
                
            case TokenType::INT_TYPE:
            case TokenType::STRING_TYPE:
            case TokenType::BOOL_TYPE:
            case TokenType::DATE_TYPE:
            case TokenType::DECIMAL_TYPE:
                category = "DATA TYPES";
                break;
                
            case TokenType::TRUE_LIT:
            case TokenType::FALSE_LIT:
                category = "BOOLEAN VALUES";
                break;
                
            case TokenType::AND:
            case TokenType::OR:
            case TokenType::IN_OP:
            case TokenType::ON:
                category = "LOGICAL OPERATORS";
                break;
                
            case TokenType::INDEX:
            case TokenType::PRIMARY_KEY:
                category = "INDEX & CONSTRAINTS";
                break;
                
            case TokenType::BEGIN_TXN:
            case TokenType::COMMIT:
            case TokenType::ROLLBACK:
                category = "TRANSACTIONS";
                break;
            
            // NEW CATEGORIES
            case TokenType::GROUP:
            case TokenType::BY:
            case TokenType::HAVING:
            case TokenType::COUNT:
            case TokenType::SUM:
            case TokenType::AVG:
            case TokenType::MIN_AGG:
            case TokenType::MAX_AGG:
                category = "GROUP BY & AGGREGATES";
                break;
            
            case TokenType::ORDER:
            case TokenType::ASC:
            case TokenType::DESC:
                category = "ORDER BY";
                break;
            
            case TokenType::LIMIT:
            case TokenType::OFFSET:
                category = "LIMIT & OFFSET";
                break;
            
            case TokenType::DISTINCT:
            case TokenType::ALL:
                category = "DISTINCT & ALL";
                break;
            
            case TokenType::JOIN:
            case TokenType::INNER:
            case TokenType::LEFT:
            case TokenType::RIGHT:
            case TokenType::OUTER:
            case TokenType::CROSS:
                category = "JOINS";
                break;
            
            case TokenType::FOREIGN:
            case TokenType::KEY:
            case TokenType::REFERENCES:
            case TokenType::CASCADE:
            case TokenType::RESTRICT:
            case TokenType::SET:
            case TokenType::NO:
            case TokenType::ACTION:
                category = "FOREIGN KEYS";
                break;
            
            case TokenType::NULL_LIT:
            case TokenType::NOT:
            case TokenType::DEFAULT_KW:
            case TokenType::UNIQUE:
            case TokenType::CHECK:
            case TokenType::AUTO_INCREMENT:
                category = "COLUMN CONSTRAINTS";
                break;
                
            default:
                category = "OTHER";
                break;
        }
        
        categories[category].push_back({franco, english});
    }
    
    // Display categorized keywords
    std::vector<std::string> order = {
        "BASIC COMMANDS",
        "DATABASE OPERATIONS",
        "USER MANAGEMENT",
        "USER ROLES",
        "DATA TYPES",
        "BOOLEAN VALUES",
        "LOGICAL OPERATORS",
        "INDEX & CONSTRAINTS",
        "TRANSACTIONS",
        "GROUP BY & AGGREGATES",
        "ORDER BY",
        "LIMIT & OFFSET",
        "DISTINCT & ALL",
        "JOINS",
        "FOREIGN KEYS",
        "COLUMN CONSTRAINTS"
    };
    
    for (const auto& cat : order) {
        if (categories.find(cat) != categories.end()) {
            std::cout << "\n[" << cat << "]" << std::endl;
            
            // Sort by Franco keyword for consistent display
            auto& items = categories[cat];
            std::sort(items.begin(), items.end());
            
            for (const auto& [franco, english] : items) {
                std::cout << "  " << std::left << std::setw(18) << franco 
                         << std::setw(18) << english 
                         << "- Franco keyword" << std::endl;
            }
        }
    }
    
    // Additional operators
    std::cout << "\n[COMPARISON OPERATORS]" << std::endl;
    std::cout << "  =                  =                - Equals" << std::endl;
    std::cout << "  >                  >                - Greater than" << std::endl;
    std::cout << "  <                  <                - Less than" << std::endl;
    std::cout << "  >=                 >=               - Greater than or equal" << std::endl;
    std::cout << "  <=                 <=               - Less than or equal" << std::endl;
    
    std::cout << "\n[SHELL COMMANDS]" << std::endl;
    std::cout << "  syntax / help      -                Display this syntax guide" << std::endl;
    std::cout << "  clear / cls        -                Clear screen" << std::endl;
    std::cout << "  run <file>         -                Execute FSQL file (.fsql)" << std::endl;
    std::cout << "  exec <file>        -                Execute FSQL file (alias)" << std::endl;
    std::cout << "  source <file>      -                Execute FSQL file (alias)" << std::endl;
    std::cout << "  exit / quit        -                Exit shell" << std::endl;
    
    std::cout << "\n[EXAMPLE QUERIES]" << std::endl;
    std::cout << "  -- Basic Commands:" << std::endl;
    std::cout << "  2E3MEL GADWAL users (id RAKAM ASASI, name GOMLA);" << std::endl;
    std::cout << "  EMLA GOWA users ELKEYAM (1, 'Ahmed');" << std::endl;
    std::cout << "  2E5TAR * MEN users LAMA id = 1;" << std::endl;
    std::cout << "  3ADEL GOWA users 5ALY name = 'Ali' LAMA id = 1;" << std::endl;
    std::cout << "  2EMSA7 MEN users LAMA id = 1;" << std::endl;
    
    std::cout << "\n  -- Indexes:" << std::endl;
    std::cout << "  2E3MEL FEHRIS idx_name 3ALA users (name);" << std::endl;
    
    std::cout << "\n  -- GROUP BY & Aggregates:" << std::endl;
    std::cout << "  2E5TAR department, 3ADD(*) MEN users MAGMO3A B department;" << std::endl;
    std::cout << "  2E5TAR department, MAG3MO3(salary) MEN employees GROUP BY department;" << std::endl;
    std::cout << "  2E5TAR city, MOTO3ASET(age) MEN users MAGMO3A B city ETHA MOTO3ASET(age) > 25;" << std::endl;
    
    std::cout << "\n  -- ORDER BY:" << std::endl;
    std::cout << "  2E5TAR * MEN users RATEB B name TASE3DI;" << std::endl;
    std::cout << "  2E5TAR * MEN products ORDER BY price DESC;" << std::endl;
    std::cout << "  2E5TAR * MEN users ORDER BY age ASC, name DESC;" << std::endl;
    
    std::cout << "\n  -- LIMIT & OFFSET:" << std::endl;
    std::cout << "  2E5TAR * MEN users 7ADD 10;" << std::endl;
    std::cout << "  2E5TAR * MEN users LIMIT 10 OFFSET 20;" << std::endl;
    std::cout << "  2E5TAR * MEN products 7ADD 5 EBDA2MEN 10;" << std::endl;
    
    std::cout << "\n  -- DISTINCT:" << std::endl;
    std::cout << "  2E5TAR MOTA3MEZ city MEN users;" << std::endl;
    std::cout << "  2E5TAR DISTINCT department MEN employees;" << std::endl;
    
    std::cout << "\n  -- JOINS:" << std::endl;
    std::cout << "  2E5TAR * MEN users ENTEDAH orders 3ALA users.id = orders.user_id;" << std::endl;
    std::cout << "  2E5TAR * MEN users DA5ELY JOIN orders ON users.id = orders.user_id;" << std::endl;
    std::cout << "  2E5TAR * MEN users SHMAL ENTEDAH orders 3ALA users.id = orders.user_id;" << std::endl;
    
    std::cout << "\n  -- FOREIGN KEYS:" << std::endl;
    std::cout << "  2E3MEL GADWAL orders (" << std::endl;
    std::cout << "    id RAKAM ASASI," << std::endl;
    std::cout << "    user_id RAKAM," << std::endl;
    std::cout << "    FOREIGN KEY (user_id) YOSHEER users(id) TATABE3" << std::endl;
    std::cout << "  );" << std::endl;
    
    std::cout << "\n  -- CONSTRAINTS:" << std::endl;
    std::cout << "  2E3MEL GADWAL products (" << std::endl;
    std::cout << "    id RAKAM ASASI TAZAYED," << std::endl;
    std::cout << "    name GOMLA MESH FADY WAHED," << std::endl;
    std::cout << "    price KASR EFRADY 0.0," << std::endl;
    std::cout << "    stock RAKAM FA7S (stock >= 0)" << std::endl;
    std::cout << "  );" << std::endl;
    
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TIP: Franco keywords are case-insensitive. Use what you prefer!" << std::endl;
    std::cout << "     Total keywords available: " << keywords.size() << std::endl;
    std::cout << std::string(80, '=') << std::endl << std::endl;
}

// -----------------------------------------------------------------------------
// FSQL FILE EXECUTION (ChronosSQL Script Files)
// -----------------------------------------------------------------------------
bool ExecuteFSQLFile(ChronosClient& client, const std::string& filepath, const std::string& current_db) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Cannot open file: " << filepath << std::endl;
        return false;
    }

    std::cout << "\n+============================================================+" << std::endl;
    std::cout << "|  Executing FSQL File: " << std::left << std::setw(32) << filepath << " |" << std::endl;
    std::cout << "|  Database Context: " << std::left << std::setw(36) << current_db << " |" << std::endl;
    std::cout << "+============================================================+\n" << std::endl;

    std::string line, statement;
    int line_number = 0;
    int statement_start_line = 0;
    int executed = 0;
    int successful = 0;
    int failed = 0;
    bool in_block_comment = false;

    while (std::getline(file, line)) {
        line_number++;
        
        // Remove leading/trailing whitespace
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue; // Empty line
        
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, (last - first + 1));
        
        // Handle block comments /* ... */
        if (in_block_comment) {
            size_t end_comment = line.find("*/");
            if (end_comment != std::string::npos) {
                in_block_comment = false;
                line = line.substr(end_comment + 2);
                if (line.empty()) continue;
            } else {
                continue; // Still in block comment
            }
        }
        
        // Check for block comment start
        size_t start_comment = line.find("/*");
        if (start_comment != std::string::npos) {
            size_t end_comment = line.find("*/", start_comment + 2);
            if (end_comment != std::string::npos) {
                // Block comment on single line - remove it
                line = line.substr(0, start_comment) + line.substr(end_comment + 2);
            } else {
                // Multi-line block comment
                line = line.substr(0, start_comment);
                in_block_comment = true;
            }
            if (line.empty()) continue;
        }
        
        // Skip single-line comments (lines starting with -- or #)
        if (line.rfind("--", 0) == 0 || line.rfind("#", 0) == 0) {
            std::cout << "\033[90m" << line << "\033[0m" << std::endl; // Gray color for comments
            continue;
        }
        
        // Remove inline comments
        size_t comment_pos = line.find("--");
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
            // Trim again
            last = line.find_last_not_of(" \t\r\n");
            if (last != std::string::npos) {
                line = line.substr(0, last + 1);
            }
        }
        
        if (line.empty()) continue;
        
        // Track where statement started
        if (statement.empty()) {
            statement_start_line = line_number;
        }
        
        // Accumulate statement until we hit a semicolon
        statement += line + " ";
        
        if (line.back() == ';') {
            // Execute the statement
            executed++;
            
            // Show statement with line info
            if (statement_start_line == line_number) {
                std::cout << "\n\033[36m[Line " << line_number << "]\033[0m " << statement << std::endl;
            } else {
                std::cout << "\n\033[36m[Lines " << statement_start_line << "-" << line_number << "]\033[0m " << statement << std::endl;
            }
            
            std::string result = client.Query(statement);
            
            // Check if successful
            std::string upper_result = result;
            std::transform(upper_result.begin(), upper_result.end(), upper_result.begin(), ::toupper);
            
            if (upper_result.find("ERROR") != std::string::npos || 
                upper_result.find("FAILED") != std::string::npos) {
                std::cout << "\033[31m[FAILED]\033[0m " << result << std::endl;
                
                // Try to provide more detailed error info
                if (upper_result.find("EXPECTED") != std::string::npos) {
                    std::cout << "\033[33m  ^ Syntax error in statement starting at line " 
                              << statement_start_line << "\033[0m" << std::endl;
                }
                
                failed++;
            } else {
                std::cout << "\033[32m[SUCCESS]\033[0m " << result << std::endl;
                successful++;
            }
            
            statement.clear();
        }
    }
    
    file.close();
    
    // Check for incomplete statement
    if (!statement.empty()) {
        std::string trimmed = statement;
        size_t pos = trimmed.find_last_not_of(" \t\r\n");
        if (pos != std::string::npos) {
            trimmed = trimmed.substr(0, pos + 1);
        }
        if (!trimmed.empty()) {
            std::cout << "\n\033[33m[WARNING] Incomplete statement at end of file (missing semicolon?):\033[0m" << std::endl;
            std::cout << "  Starting at line " << statement_start_line << ": " << trimmed << std::endl;
            failed++;
        }
    }
    
    std::cout << "\n+============================================================+" << std::endl;
    std::cout << "|  EXECUTION SUMMARY" << std::string(37, ' ') << "   |" << std::endl;
    std::cout << "+------------------------------------------------------------+" << std::endl;
    std::cout << "|  Total Statements: " << std::left << std::setw(38) << executed << " |" << std::endl;
    std::cout << "|  Successful:       " << std::left << std::setw(38) << successful << " |" << std::endl;
    std::cout << "|  Failed:           " << std::left << std::setw(38) << failed << " |" << std::endl;
    std::cout << "+============================================================+\n" << std::endl;
    
    return failed == 0;
}

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
    std::cout << "      CHRONOS DB CONFIGURATION WIZARD" << std::endl;
    std::cout << std::string(50, '=') << std::endl;

    std::string input;
    
    // 1. PORT
    int port = 2501;
    std::cout << "Server Port [2501]: ";
    std::getline(std::cin, input);
    if (!input.empty()) try { port = std::stoi(input); } catch(...) {}

    // 2. USERNAME
    std::string user = "chronos";
    std::cout << "Root Username [chronos]: ";
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
        file << "# ChronosDB Configuration\n";
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
    ChronosClient db_client;
    std::string username = net::DEFAULT_ADMIN_USERNAME;
    std::string current_db = "chronosdb";
    bool connected = false;

    if (argc > 1) {
        std::string cmd1 = argv[1]; 
        std::string cmd2 = (argc > 2) ? argv[2] : "";
        std::transform(cmd1.begin(), cmd1.end(), cmd1.begin(), ::tolower);
        std::transform(cmd2.begin(), cmd2.end(), cmd2.begin(), ::tolower);

        // --- CONFIG RESET COMMAND ---
        if (cmd1 == "config" && cmd2 == "reset") {
            fs::path config_path = fs::path(GetExecutableDir()) / "chronosdb.conf";
            RunSetupWizard(config_path.string());
            return 0;
        }

        // --- SERVICE COMMANDS ---
        if (cmd1 == "server" || cmd2 == "server") {
            std::string action = (cmd1 == "server") ? cmd2 : cmd1;
            if (!IsAdmin()) { std::cerr << "Run as Admin required." << std::endl; return 1; }
            
            if (action == "start") return system("net start ChronosDBService");
            if (action == "stop") return system("net stop ChronosDBService");
            if (action == "restart") { system("net stop ChronosDBService"); return system("net start ChronosDBService"); }
        }

        // --- LOGIN COMMANDS ---
        if (cmd1 == "login" || cmd1.find("chronos://") == 0) {
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
        
        // --- FSQL FILE EXECUTION FROM COMMAND LINE ---
        // Usage: chronosdb_shell file.fsql
        //        chronosdb_shell run file.fsql
        //        chronosdb_shell exec file.fsql
        //        chronosdb_shell -f file.fsql
        std::string fsql_file = "";
        if (cmd1 == "run" || cmd1 == "exec" || cmd1 == "-f") {
            fsql_file = cmd2;
        } else if (cmd1.size() > 5 && cmd1.substr(cmd1.size() - 5) == ".fsql") {
            fsql_file = cmd1;
        } else if (cmd2.size() > 5 && cmd2.substr(cmd2.size() - 5) == ".fsql") {
            fsql_file = cmd2;
        }
        
        if (!fsql_file.empty()) {
            // Need to connect first if not already connected
            if (!connected) {
                std::cout << "==========================================" << std::endl;
                std::cout << "     ChronosDB FSQL Script Executor        " << std::endl;
                std::cout << "==========================================" << std::endl;
                
                std::string password, host, port_str;
                int port = net::DEFAULT_PORT;
                
                std::cout << "\nConnect to execute: " << fsql_file << std::endl;
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
            
            // Execute the FSQL file
            bool success = ExecuteFSQLFile(db_client, fsql_file, current_db);
            db_client.Disconnect();
            return success ? 0 : 1;
        }
    }

    if (!connected) {
        std::cout << "==========================================" << std::endl;
        std::cout << "            ChronosDB Shell v2.1           " << std::endl;
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
    // SHELL LOOP - Supports multi-line commands
    // -------------------------------------------------------------------------
    std::string input;
    std::string accumulated_input;  // For multi-line command support
    bool in_multiline = false;

    while (connected) {
        // Show different prompt for continuation lines
        if (in_multiline) {
            std::cout << "       -> ";
        } else {
            std::cout << username << "@" << current_db << "> ";
        }
        std::cout.flush();
        
        if (!std::getline(std::cin, input)) break;

        // Handle exit only if not in middle of a statement
        if (!in_multiline && (input == "exit" || input == "quit")) break;

        // Empty input on first line = skip, empty input on continuation = keep going
        if (input.empty() && !in_multiline) continue;

        // --- CLEAR COMMAND (only on first line) ---
        if (!in_multiline && (input == "clear" || input == "cls")) {
            #ifdef _WIN32
                system("cls");
            #else
                system("clear");
            #endif
            continue;
        }

        // --- SYNTAX HELP COMMAND (only on first line) ---
        if (!in_multiline && (input == "syntax" || input == "help" || input == "SYNTAX" || input == "HELP")) {
            DisplayDynamicSyntax();
            continue;
        }

        // --- EXECUTE FSQL FILE (ChronosSQL Script) - only on first line ---
        if (!in_multiline &&
            (input.rfind("run ", 0) == 0 || input.rfind("RUN ", 0) == 0 ||
             input.rfind("exec ", 0) == 0 || input.rfind("EXEC ", 0) == 0 ||
             input.rfind("source ", 0) == 0 || input.rfind("SOURCE ", 0) == 0)) {

            std::string filepath = input.substr(input.find(' ') + 1);
            
            // Remove trailing semicolon if present
            if (!filepath.empty() && filepath.back() == ';') {
                filepath.pop_back();
            }
            
            // Trim whitespace
            size_t first = filepath.find_first_not_of(" \t\n\r");
            size_t last = filepath.find_last_not_of(" \t\n\r");
            if (first != std::string::npos) {
                filepath = filepath.substr(first, (last - first + 1));
            }
            
            // Add .fsql extension if not present
            if (filepath.find('.') == std::string::npos) {
                filepath += ".fsql";
            }
            
            ExecuteFSQLFile(db_client, filepath, current_db);
            continue;
        }

        // Accumulate input for multi-line support
        if (!accumulated_input.empty()) {
            accumulated_input += " ";
        }
        accumulated_input += input;

        // Check if statement is complete (ends with semicolon)
        // We need to handle potential semicolons inside strings
        std::string trimmed = accumulated_input;
        size_t last_non_space = trimmed.find_last_not_of(" \t\n\r");
        if (last_non_space != std::string::npos) {
            trimmed = trimmed.substr(0, last_non_space + 1);
        }

        if (trimmed.empty()) {
            accumulated_input.clear();
            in_multiline = false;
            continue;
        }

        // Check if we have a complete statement (ends with semicolon)
        // Simple check - for more robust handling, we'd need to track if we're inside quotes
        if (trimmed.back() != ';') {
            in_multiline = true;
            continue;  // Wait for more input
        }

        // Statement is complete - use accumulated input
        input = accumulated_input;
        accumulated_input.clear();
        in_multiline = false;

        // --- [FIXED] PROMPT UPDATE LOGIC - Only update on success ---
        std::string upper_input = input;
        std::transform(upper_input.begin(), upper_input.end(), upper_input.begin(), ::toupper);
        
        // Detect which keyword was used (USE or 2ESTA5DEM)
        size_t prefix_len = 0;
        if (upper_input.rfind("USE ", 0) == 0) {
            prefix_len = 4;
        } else if (upper_input.rfind("2ESTA5DEM ", 0) == 0) {
            prefix_len = 10;
        }

        std::string potential_new_db = "";
        // If a DB change command was detected, extract the database name
        if (prefix_len > 0) {
            std::string new_db = input.substr(prefix_len);
            
            // Remove trailing semicolon
            if (!new_db.empty() && new_db.back() == ';') {
                new_db.pop_back();
            }
            
            // Trim whitespace
            size_t first = new_db.find_first_not_of(" \t\n\r");
            if (std::string::npos != first) {
                size_t last = new_db.find_last_not_of(" \t\n\r");
                potential_new_db = new_db.substr(first, (last - first + 1));
            } else if (!new_db.empty()) {
                potential_new_db = new_db;
            }
        }

        // Execute the query
        std::string result = db_client.Query(input);
        std::cout << result << std::endl;

        // Check if connection was lost
        if (!db_client.IsConnected()) {
            std::cerr << "\n[DISCONNECTED] Server closed connection." << std::endl;
            break;
        }

        // Only update current_db if the USE command was successful
        if (!potential_new_db.empty()) {
            // Check if result indicates success (doesn't contain ERROR)
            std::string upper_result = result;
            std::transform(upper_result.begin(), upper_result.end(), upper_result.begin(), ::toupper);
            if (upper_result.find("ERROR") == std::string::npos && 
                upper_result.find("FAILED") == std::string::npos) {
                current_db = potential_new_db;
            }
        }
        // ------------------------------------------
    }

    db_client.Disconnect();
    return 0;
}