#include <iostream>
#include <string>
#include "network/franco_client.h"

using namespace francodb;

void PrintWelcome() {
    std::cout << "FrancoDB Shell v1.0" << std::endl;
    std::cout << "Connected to backend logic via TCP." << std::endl;
    std::cout << "Type 'exit' to quit." << std::endl;
}

int main() {
    FrancoClient db_client;

    if (!db_client.Connect()) {
        std::cerr << "[FATAL] Could not connect to FrancoDB server." << std::endl;
        return 1;
    }

    PrintWelcome();

    std::string input;
    while (true) {
        std::cout << "FrancoDB> ";
        if (!std::getline(std::cin, input) || input == "exit") break;
        if (input.empty()) continue;

        // Use the Driver API
        std::string response = db_client.Query(input);
        
        std::cout << response << std::endl;
    }

    db_client.Disconnect();
    return 0;
}