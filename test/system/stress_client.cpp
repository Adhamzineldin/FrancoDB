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

const int NUM_THREADS = 8;        // 8 Threads
const int OPS_PER_THREAD = 500;   // 500 Ops each = 4000 Total

const char CMD_TEXT = 'Q';

static std::atomic<int> success_count{0};
std::atomic<int> fail_count{0};
std::mutex print_mutex;

void Log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(print_mutex);
    std::cout << msg << std::endl;
}

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
        return true;
    }

    // [FIXED] Protocol: Read Length Header -> Loop Read Body
    std::string Send(const std::string& query) {
        
        // 1. Prepare Request
        char type = CMD_TEXT;
        uint32_t len = htonl(query.length());
        std::vector<char> buffer;
        buffer.push_back(type);
        const char* len_ptr = reinterpret_cast<const char*>(&len);
        buffer.insert(buffer.end(), len_ptr, len_ptr + 4);
        buffer.insert(buffer.end(), query.begin(), query.end());

        // 2. Send Request
        if (send(sock, buffer.data(), (int)buffer.size(), 0) == SOCKET_ERROR) {
            return "NETWORK_ERROR_SEND";
        }

        // 3. Receive Response (SYNC FIXED)
        
        // A. Read Length Header (4 Bytes)
        uint32_t net_len = 0;
        int received = 0;
        char* net_len_ptr = (char*)&net_len;
        
        while (received < 4) {
            int bytes = recv(sock, net_len_ptr + received, 4 - received, 0);
            if (bytes <= 0) return "NETWORK_ERROR_RECV_HEADER";
            received += bytes;
        }
        
        uint32_t resp_len = ntohl(net_len);

        // B. Read Body (Loop until resp_len is fully read)
        std::vector<char> resp_buf(resp_len);
        received = 0;
        
        while (received < resp_len) {
            int bytes = recv(sock, resp_buf.data() + received, resp_len - received, 0);
            if (bytes <= 0) return "NETWORK_ERROR_RECV_BODY";
            received += bytes;
        }

        return std::string(resp_buf.begin(), resp_buf.end());
    }

    void Close() {
        if (sock != INVALID_SOCKET) closesocket(sock);
    }
};

static void Worker(int id) {
    FrancoClient client;
    if (!client.Connect()) {
        Log("[Thread " + std::to_string(id) + "] Failed to connect");
        return;
    }

    // 1. Authenticate
    std::string login_resp = client.Send("LOGIN " + USER + " " + PASS + ";");
    if (login_resp.find("ERROR") != std::string::npos) {
        Log("[Thread " + std::to_string(id) + "] Login Failed: " + login_resp);
        client.Close();
        return;
    }
    
    // 2. Select Database
    client.Send("2ESTA5DEM stress_db;");

    // 3. Random Workload
    static thread_local std::mt19937 rng(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::uniform_int_distribution<int> dist(1, 100);

    for (int i = 0; i < OPS_PER_THREAD; ++i) {
        int r = dist(rng);
        std::string query;
        
        // Mix: 40% Insert, 30% Select, 30% Update
        if (r < 40) {
            int val = dist(rng);
            query = "EMLA GOWA stress_table ELKEYAM (" + std::to_string(val) + ", 'StressTest');";
        } else if (r < 70) {
            query = "2E5TAR * MEN stress_table;"; // Returns massive data -> Tests Protocol
        } else {
            query = "3ADEL stress_table 5ALY val = 'Updated' LAMA id > 50;";
        }

        std::string resp = client.Send(query);
        
        
        if (i % 50 == 0) {
            std::cout << "." << std::flush; 
        }
        
        if (resp.find("ERROR") != std::string::npos || resp.find("NETWORK_ERROR") != std::string::npos) {
            fail_count++;
            // Optional: Log errors to see what's wrong
            // Log("Query Failed: " + resp); 
        } else {
            success_count++;
        }
    }
    client.Close();
}

void TestStressClient() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    Log("=== FRANCODB NETWORK STRESS TEST (PROTOCOL V1) ===");
    Log("Target: " + SERVER_IP + ":" + std::to_string(SERVER_PORT));
    
    // 1. Setup Phase
    {
        Log("-> Setting up Database 'stress_db'...");
        FrancoClient admin;
        if (!admin.Connect()) {
            Log("[ERROR] Could not connect to server.");
            WSACleanup();
            return;
        }
        
        admin.Send("LOGIN " + USER + " " + PASS + ";");
        admin.Send("2E3MEL DATABASE stress_db;");
        admin.Send("2ESTA5DEM stress_db;");
        admin.Send("2EMSA7 GADWAL stress_table;"); 
        std::string create_resp = admin.Send("2E3MEL GADWAL stress_table (id RAKAM, val GOMLA);");
        Log("-> Create Table Response: " + create_resp);
        admin.Close();
    }

    // 2. Attack Phase
    Log("-> Launching " + std::to_string(NUM_THREADS) + " threads (" + std::to_string(OPS_PER_THREAD) + " ops each)...");
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(Worker, i);
    }

    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    Log("\n=== RESULTS ===");
    Log("Time Taken: " + std::to_string(diff.count()) + " seconds");
    Log("Total Requests: " + std::to_string(success_count + fail_count));
    Log("Successful: " + std::to_string(success_count));
    Log("Failed: " + std::to_string(fail_count));
    
    if (diff.count() > 0) {
        double qps = (success_count + fail_count) / diff.count();
        Log("Throughput: " + std::to_string(qps) + " Queries/Sec");
    }

    WSACleanup();
}