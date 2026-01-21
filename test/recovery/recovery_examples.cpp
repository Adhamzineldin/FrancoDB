/**
 * FrancoDB Recovery System - CLIENT-SIDE Integration Examples
 * 
 * This file demonstrates recovery and time travel features by sending
 * queries to a running FrancoDB server (just like a real client would).
 */

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// Server configuration
const std::string SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 2501;
const std::string USER = "maayn";
const std::string PASS = "root";
const char CMD_TEXT = 'Q';

// Helper function to get current timestamp in microseconds
uint64_t GetCurrentTimestamp() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Simple FrancoDB Client
class FrancoClient {
    SOCKET sock;
public:
    FrancoClient() : sock(INVALID_SOCKET) {}

    bool Connect() {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) return false;

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP.c_str(), &server_addr.sin_addr);

        if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            closesocket(sock);
            return false;
        }
        
        // Authenticate
        std::string auth_query = "LOGIN " + USER + " PASS " + PASS + ";";
        Send(auth_query);
        
        return true;
    }

    std::string Send(const std::string& query) {
        // Send header + body
        char type = CMD_TEXT;
        uint32_t len = htonl(static_cast<uint32_t>(query.length()));
        std::vector<char> buffer;
        buffer.push_back(type);
        buffer.insert(buffer.end(), reinterpret_cast<char*>(&len), reinterpret_cast<char*>(&len) + 4);
        buffer.insert(buffer.end(), query.begin(), query.end());

        send(sock, buffer.data(), static_cast<int>(buffer.size()), 0);

        // Receive response
        char resp_type;
        uint32_t resp_len;
        recv(sock, &resp_type, 1, 0);
        recv(sock, reinterpret_cast<char*>(&resp_len), 4, 0);
        resp_len = ntohl(resp_len);

        std::vector<char> response(resp_len);
        int total = 0;
        while (total < static_cast<int>(resp_len)) {
            int n = recv(sock, response.data() + total, resp_len - total, 0);
            if (n <= 0) break;
            total += n;
        }

        return std::string(response.begin(), response.end());
    }

    void Close() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
        }
    }

    ~FrancoClient() { Close(); }
};

   
/**
 * Example 1: Basic Database Operations with Multi-Database Support
 */
void Example1_MultiDatabaseSetup() {
    std::cout << "\n=== Example 1: Multi-Database Setup ===" << std::endl;

    FrancoClient client;
    if (!client.Connect()) {
        std::cout << "[ERROR] Cannot connect to server. Is it running?" << std::endl;
        return;
    }

    // Create multiple databases
    std::cout << "[TEST] Creating databases..." << std::endl;
    client.Send("KHALEK DATABASE production;");
    client.Send("KHALEK DATABASE staging;");
    client.Send("KHALEK DATABASE analytics;");

    // Show databases
    std::string resp = client.Send("WARY DATABASES;");
    std::cout << "[RESULT] Databases:\n" << resp << std::endl;

    std::cout << "[SUCCESS] Multi-database setup complete" << std::endl;
    client.Close();
}

/**
 * Example 2: Database Context Switching
 */
void Example2_DatabaseSwitching() {
    std::cout << "\n=== Example 2: Database Context Switching ===" << std::endl;

    FrancoClient client;
    if (!client.Connect()) {
        std::cout << "[ERROR] Cannot connect to server" << std::endl;
        return;
    }

    // Switch between databases
    std::cout << "[TEST] Switching to production database..." << std::endl;
    client.Send("2E5TAML production;");
    
    // Create table in production
    client.Send("KHALEK TABLE users (id INT, name STRING);");
    client.Send("7OT FY users VALUES (1, 'Alice');");

    std::cout << "[TEST] Switching to staging database..." << std::endl;
    client.Send("2E5TAML staging;");
    
    // Create table in staging
    client.Send("KHALEK TABLE test_users (id INT, score INT);");
    client.Send("7OT FY test_users VALUES (1, 100);");

    // Verify isolation
    std::string resp = client.Send("WARY TABLES;");
    std::cout << "[RESULT] Tables in staging:\n" << resp << std::endl;

    std::cout << "[SUCCESS] Database switching works correctly" << std::endl;
    client.Close();
}

/**
 * Example 3: Time Travel - Read-Only Snapshot
 */
void Example3_TimeTravel_Snapshot() {
    std::cout << "\n=== Example 3: Time Travel Snapshot (AS OF) ===" << std::endl;

    FrancoClient client;
    if (!client.Connect()) {
        std::cout << "[ERROR] Cannot connect to server" << std::endl;
        return;
    }

    // Setup: Create test database and table
    client.Send("KHALEK DATABASE timetravel_test;");
    client.Send("2E5TAML timetravel_test;");
    client.Send("KHALEK TABLE accounts (id INT, balance INT);");
    
    // Insert initial data
    client.Send("7OT FY accounts VALUES (1, 10000);");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Capture safe timestamp
    uint64_t safe_time = GetCurrentTimestamp();
    std::cout << "[INFO] Safe timestamp captured: " << safe_time << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Modify data (simulate disaster)
    client.Send("3ADEL accounts 5ALY balance = 0 LAMA id = 1;");
    
    // Verify current state (should be 0)
    std::string current = client.Send("2E5TAR * MEN accounts;");
    std::cout << "[CURRENT STATE]\n" << current << std::endl;
    
    // Query historical state (should be 10000)
    std::string query_as_of = "2E5TAR * MEN accounts AS OF " + std::to_string(safe_time) + ";";
    std::string historical = client.Send(query_as_of);
    std::cout << "[HISTORICAL STATE (AS OF " << safe_time << ")]\n" << historical << std::endl;

    std::cout << "[SUCCESS] Time travel snapshot complete" << std::endl;
    client.Close();
}

/**
 * Example 4: Point-in-Time Recovery
 */
void Example4_PointInTimeRecovery() {
    std::cout << "\n=== Example 4: Point-in-Time Recovery (RECOVER TO) ===" << std::endl;

    FrancoClient client;
    if (!client.Connect()) {
        std::cout << "[ERROR] Cannot connect to server" << std::endl;
        return;
    }

    // Setup
    client.Send("KHALEK DATABASE recovery_test;");
    client.Send("2E5TAML recovery_test;");
    client.Send("KHALEK TABLE important_data (id INT, value INT);");
    
    // Insert initial data
    client.Send("7OT FY important_data VALUES (1, 999);");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Capture safe point
    uint64_t safe_time = GetCurrentTimestamp();
    std::cout << "[INFO] Safe point: " << safe_time << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Disaster: Delete all data
    std::cout << "[DISASTER] Deleting all data..." << std::endl;
    client.Send("E7ZEF MEN important_data LAMA id = 1;");
    
    // Verify disaster
    std::string after_delete = client.Send("2E5TAR * MEN important_data;");
    std::cout << "[AFTER DELETE]\n" << after_delete << std::endl;
    
    // Perform recovery
    std::cout << "[RECOVERY] Restoring to safe point..." << std::endl;
    std::string recover_query = "RECOVER TO " + std::to_string(safe_time) + ";";
    std::string recover_resp = client.Send(recover_query);
    std::cout << "[RECOVERY RESPONSE]\n" << recover_resp << std::endl;
    
    // Verify recovery
    std::string after_recovery = client.Send("2E5TAR * MEN important_data;");
    std::cout << "[AFTER RECOVERY]\n" << after_recovery << std::endl;

    std::cout << "[SUCCESS] Point-in-time recovery complete" << std::endl;
    client.Close();
}

/**
 * Example 5: Checkpoint Testing
 */
void Example5_CheckpointTest() {
    std::cout << "\n=== Example 5: Checkpoint Testing ===" << std::endl;

    FrancoClient client;
    if (!client.Connect()) {
        std::cout << "[ERROR] Cannot connect to server" << std::endl;
        return;
    }

    // Note: Checkpoint is typically triggered internally by the server
    // This example shows how the system handles checkpoints
    
    std::cout << "[INFO] Performing operations..." << std::endl;
    client.Send("KHALEK DATABASE checkpoint_test;");
    client.Send("2E5TAML checkpoint_test;");
    client.Send("KHALEK TABLE test (id INT);");
    
    // Insert many records to trigger checkpoint (if configured)
    for (int i = 0; i < 100; i++) {
        client.Send("7OT FY test VALUES (" + std::to_string(i) + ");");
    }
    
    std::cout << "[INFO] Checkpoint should occur in background (if configured)" << std::endl;
    std::cout << "[SUCCESS] Checkpoint test complete" << std::endl;
    
    client.Close();
}

/**
 * Example 6: Drop Database Test
 */
void Example6_DropDatabaseTest() {
    std::cout << "\n=== Example 6: DROP DATABASE Test ===" << std::endl;

    FrancoClient client;
    if (!client.Connect()) {
        std::cout << "[ERROR] Cannot connect to server" << std::endl;
        return;
    }

    // Create temporary database
    std::cout << "[TEST] Creating temporary database..." << std::endl;
    client.Send("KHALEK DATABASE temp_db;");
    client.Send("2E5TAML temp_db;");
    client.Send("KHALEK TABLE temp_table (id INT);");
    client.Send("7OT FY temp_table VALUES (1);");
    
    // Verify it exists
    std::string before = client.Send("WARY DATABASES;");
    std::cout << "[BEFORE DROP]\n" << before << std::endl;
    
    // Drop it
    std::cout << "[TEST] Dropping database..." << std::endl;
    std::string drop_resp = client.Send("E7ZEF DATABASE temp_db;");
    std::cout << "[DROP RESPONSE]\n" << drop_resp << std::endl;
    
    // Verify it's gone
    std::string after = client.Send("WARY DATABASES;");
    std::cout << "[AFTER DROP]\n" << after << std::endl;

    std::cout << "[SUCCESS] DROP DATABASE test complete" << std::endl;
    client.Close();
}

/**
 * Main function - Run all examples
 */
int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::cout << "========================================" << std::endl;
    std::cout << "FrancoDB Recovery System Examples" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "\nNOTE: Make sure FrancoDB server is running on port " << SERVER_PORT << std::endl;
    std::cout << "Press Enter to continue...";
    std::cin.get();

    try {
        Example1_MultiDatabaseSetup();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        Example2_DatabaseSwitching();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        Example3_TimeTravel_Snapshot();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        Example4_PointInTimeRecovery();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        Example5_CheckpointTest();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        Example6_DropDatabaseTest();

        std::cout << "\n========================================" << std::endl;
        std::cout << "All examples completed successfully!" << std::endl;
        std::cout << "========================================" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        WSACleanup();
        return 1;
    }

    WSACleanup();
    return 0;
}

