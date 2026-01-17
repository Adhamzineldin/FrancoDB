#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// --- CONFIG ---
const std::string SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 2501;      
const std::string USER = "maayn";  
const std::string PASS = "root";   

const int NUM_THREADS = 8;
const int OPS_PER_THREAD = 100; // Lower count, but higher quality checks

const char CMD_TEXT = 'Q';

std::atomic<int> success_count{0};
std::atomic<int> data_errors{0};
std::mutex log_mutex;

void Log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << msg << std::endl;
}

void LogError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << "[DATA ERROR] " << msg << std::endl;
}

class FrancoClient {
    SOCKET sock;
public:
    FrancoClient() : sock(INVALID_SOCKET) {}

    bool Connect() {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) return false;
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP.c_str(), &addr.sin_addr);
        return connect(sock, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR;
    }

    std::string Send(const std::string& query) {
        char type = CMD_TEXT;
        uint32_t len = htonl(query.length());
        std::vector<char> buffer;
        buffer.push_back(type);
        const char* len_ptr = reinterpret_cast<const char*>(&len);
        buffer.insert(buffer.end(), len_ptr, len_ptr + 4);
        buffer.insert(buffer.end(), query.begin(), query.end());

        if (send(sock, buffer.data(), (int)buffer.size(), 0) == SOCKET_ERROR) return "NET_ERR";

        char recv_buf[8192];
        int bytes = recv(sock, recv_buf, 8192 - 1, 0);
        if (bytes > 0) {
            recv_buf[bytes] = '\0';
            return std::string(recv_buf);
        }
        return "NET_ERR";
    }

    void Close() { if (sock != INVALID_SOCKET) closesocket(sock); }
};

void Worker(int thread_id) {
    FrancoClient client;
    if (!client.Connect()) return;

    // Login & Setup
    if (client.Send("LOGIN " + USER + " " + PASS + ";").find("ERROR") != std::string::npos) return;
    client.Send("2ESTA5DEM verify_db;");

    for (int i = 0; i < OPS_PER_THREAD; ++i) {
        // Unique ID logic: Thread 1 handles IDs 1000-1999, Thread 2 handles 2000-2999, etc.
        // This avoids race conditions between threads, testing PURE storage correctness.
        int unique_id = (thread_id * 10000) + i;
        std::string val_v1 = "T" + std::to_string(thread_id) + "_VAL_" + std::to_string(i);
        std::string val_v2 = "UPDATED_" + std::to_string(i);

        // --- STEP 1: INSERT ---
        std::string q_ins = "EMLA GOWA verify_table ELKEYAM (" + std::to_string(unique_id) + ", '" + val_v1 + "');";
        std::string r_ins = client.Send(q_ins);
        if (r_ins.find("SUCCESS") == std::string::npos && r_ins.find("INSERT") == std::string::npos) {
            LogError("Insert Failed: " + r_ins);
            data_errors++; continue;
        }

        // --- STEP 2: VERIFY INSERT (READ YOUR OWN WRITE) ---
        // Assuming your SELECT * returns the data row string
        std::string q_sel = "2E5TAR * MEN verify_table LAMA id = " + std::to_string(unique_id) + ";";
        std::string r_sel = client.Send(q_sel);
        
        if (r_sel.find(val_v1) == std::string::npos) {
            LogError("Thread " + std::to_string(thread_id) + " Wrote '" + val_v1 + "' but Read: " + r_sel);
            data_errors++; continue;
        }

        // --- STEP 3: UPDATE ---
        std::string q_upd = "3ADEL verify_table 5ALY val = '" + val_v2 + "' LAMA id = " + std::to_string(unique_id) + ";";
        client.Send(q_upd);

        // --- STEP 4: VERIFY UPDATE ---
        std::string r_sel2 = client.Send(q_sel);
        if (r_sel2.find(val_v2) == std::string::npos) {
            LogError("Thread " + std::to_string(thread_id) + " Updated to '" + val_v2 + "' but Read: " + r_sel2);
            data_errors++; continue;
        }

        success_count++;
    }
    client.Close();
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    Log("=== FRANCODB DATA INTEGRITY TEST ===");

    // 1. Admin Setup
    {
        FrancoClient admin;
        admin.Connect();
        admin.Send("LOGIN " + USER + " " + PASS + ";");
        admin.Send("2E3MEL DATABASE verify_db;");
        admin.Send("2ESTA5DEM verify_db;");
        admin.Send("2EMSA7 GADWAL verify_table;");
        admin.Send("2E3MEL GADWAL verify_table (id RAKAM, val GOMLA);");
        // Create Index to ensure Reads are fast and test B+Tree correctness
        admin.Send("2E3MEL FEHRIS idx_id 3ALA verify_table (id);");
        admin.Close();
    }

    // 2. Run Threads
    Log("-> Launching " + std::to_string(NUM_THREADS) + " threads validating Read-After-Write consistency...");
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) threads.emplace_back(Worker, i + 1);
    for (auto& t : threads) t.join();

    Log("\n=== INTEGRITY REPORT ===");
    Log("Successful Cycles: " + std::to_string(success_count));
    Log("Data Corruptions:  " + std::to_string(data_errors));

    WSACleanup();
    return 0;
}