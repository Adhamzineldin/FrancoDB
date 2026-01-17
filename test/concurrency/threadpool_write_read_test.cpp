#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>
#include <cassert>
#include <map>
#include <shared_mutex>
#include <random>
#include "common/thread_pool.h"

using namespace francodb;

// --- SHARED BANK SIMULATION ---
struct Bank {
    std::map<int, int> accounts;
    std::shared_mutex rw_lock; 

    Bank(int num_accounts, int initial_balance) {
        for(int i=0; i<num_accounts; ++i) {
            accounts[i] = initial_balance;
        }
    }

    // WRITER TASK
    void Transfer(int from, int to, int amount) {
        std::unique_lock<std::shared_mutex> lock(rw_lock); 
        
        // Safety check: Ensure accounts exist so we don't accidentally create new ones
        if (accounts.find(from) != accounts.end() && accounts.find(to) != accounts.end()) {
            if (accounts[from] >= amount) {
                accounts[from] -= amount;
                accounts[to] += amount;
            }
        }
    }

    // READER TASK
    long long GetTotalBalance() {
        std::shared_lock<std::shared_mutex> lock(rw_lock); 
        long long total = 0;
        for(auto const& [id, bal] : accounts) {
            total += bal;
        }
        return total;
    }
};

void TestBasicExecution() {
    std::cout << "[1/4] Testing Basic Execution..." << std::endl;
    ThreadPool pool(4);
    auto future = pool.Enqueue([](int a, int b) { return a + b; }, 10, 20);
    int result = future.get();
    if (result == 30) std::cout << "  -> SUCCESS.\n";
    else std::cerr << "  -> FAILED: Expected 30, got " << result << "\n";
}

void TestMassiveConcurrency() {
    int task_count = 10000;
    std::cout << "[2/4] Testing Task Throughput (" << task_count << " tasks)..." << std::endl;
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> results;

    for (int i = 0; i < task_count; ++i) {
        results.emplace_back(pool.Enqueue([&counter] { counter++; }));
    }
    for (auto && res : results) res.get();

    if (counter == task_count) std::cout << "  -> SUCCESS.\n";
    else std::cerr << "  -> FAILED: Race condition detected.\n";
}

// --- SAFE RANDOM NUMBER GENERATOR ---
int GetRandom(int min, int max) {
    // Thread-local engine ensures no race conditions on the RNG state
    static thread_local std::mt19937 generator(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

void TestReadWriteMix() {
    std::cout << "[3/4] Testing Read/Write Consistency (The Bank Problem)..." << std::endl;
    
    int num_accounts = 100;
    int initial_bal = 1000;
    Bank bank(num_accounts, initial_bal);
    long long expected_total = (long long)num_accounts * initial_bal;

    ThreadPool pool(8); 
    std::vector<std::future<void>> futures;

    int num_transactions = 10000; // Increased load
    std::atomic<int> read_errors{0};

    for(int i = 0; i < num_transactions; ++i) {
        
        bool is_writer = (GetRandom(0, 100) < 80); // 80% Writers

        if (is_writer) {
            futures.emplace_back(pool.Enqueue([&bank, num_accounts]() {
                // Use Thread-Safe Random
                int from = GetRandom(0, num_accounts - 1);
                int to = GetRandom(0, num_accounts - 1);
                int amount = GetRandom(1, 50);
                if(from != to) bank.Transfer(from, to, amount);
            }));
        } else {
            futures.emplace_back(pool.Enqueue([&bank, expected_total, &read_errors]() {
                long long current_total = bank.GetTotalBalance();
                if (current_total != expected_total) {
                    read_errors++; 
                    // Only print first few errors to avoid console spam
                    if (read_errors < 5) {
                        std::cerr << "  [ERROR] Data Corruption! Expected " << expected_total 
                                  << " but saw " << current_total << std::endl;
                    }
                }
            }));
        }
    }

    for(auto& f : futures) f.get();

    if (read_errors == 0 && bank.GetTotalBalance() == expected_total) {
        std::cout << "  -> SUCCESS: " << num_transactions << " Mixed I/O ops finished. Data is consistent.\n";
    } else {
        std::cerr << "  -> FAILED: " << read_errors << " inconsistent reads detected.\n";
        exit(1);
    }
}

void TestShutdown() {
    std::cout << "[4/4] Testing Clean Shutdown..." << std::endl;
    {
        ThreadPool pool(4);
        pool.Enqueue([]{ std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }
    std::cout << "  -> SUCCESS.\n";
}

int main() {
    std::cout << "=== FRANCODB THREAD POOL STRESS TEST (THREAD-SAFE) ===\n";
    
    TestBasicExecution();
    TestMassiveConcurrency();
    TestReadWriteMix();
    TestShutdown();
    
    std::cout << "\nALL SYSTEMS GREEN.\n";
    return 0;
}