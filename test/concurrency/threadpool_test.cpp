#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>
#include <cassert>
#include "common/thread_pool.h"

using namespace francodb;

void TestBasicExecution() {
    std::cout << "[1/3] Testing Basic Execution..." << std::endl;
    ThreadPool pool(4);
    
    // Launch a task that returns a value
    auto future = pool.Enqueue([](int a, int b) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate work
        return a + b;
    }, 10, 20);

    int result = future.get(); // Waits for result
    if (result == 30) {
        std::cout << "  -> SUCCESS: Task returned correct value (30)." << std::endl;
    } else {
        std::cerr << "  -> FAILED: Expected 30, got " << result << std::endl;
        exit(1);
    }
}

void TestMassiveConcurrency() {
    int task_count = 100000;
    std::cout << "[2/3] Testing Stress Load (" << task_count << " tasks)..." << std::endl;
    
    ThreadPool pool(4); // Only 4 threads handling 10,000 tasks
    std::atomic<int> counter{0};
    std::vector<std::future<void>> results;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < task_count; ++i) {
        results.emplace_back(pool.Enqueue([&counter] {
            counter++;
            // Simulate tiny calculation to ensure context switching happens
            volatile int x = 0; 
            for(int j=0; j<100; j++) x++; 
        }));
    }

    // Wait for all to finish
    for (auto && res : results) {
        res.get();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    if (counter == task_count) {
        std::cout << "  -> SUCCESS: Processed " << counter << " tasks in " 
                  << diff.count() << "s." << std::endl;
    } else {
        std::cerr << "  -> FAILED: Race condition detected. Counter=" << counter << std::endl;
        exit(1);
    }
}

void TestShutdown() {
    std::cout << "[3/3] Testing Clean Shutdown..." << std::endl;
    {
        ThreadPool pool(4);
        for(int i=0; i<50; i++) {
            pool.Enqueue([]{
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
        }
    } // pool destructor called here
    std::cout << "  -> SUCCESS: Pool destroyed without hanging." << std::endl;
}

int main() {
    std::cout << "=== FRANCODB THREAD POOL DIAGNOSTIC ===" << std::endl;
    
    TestBasicExecution();
    TestMassiveConcurrency();
    TestShutdown();
    
    std::cout << "\nALL TESTS PASSED. SYSTEM IS STABLE." << std::endl;
    return 0;
}