// // examples/json_client.cpp
// #include "network/franco_client.h"
// #include <iostream>
// #include <nlohmann/json.hpp> // Using JSON library
//
// int main() {
//     francodb::FrancoClient client(francodb::ProtocolType::JSON);
//     
//     if (!client.Connect("localhost", francodb::net::DEFAULT_PORT)) {
//         std::cerr << "Failed to connect" << std::endl;
//         return 1;
//     }
//     
//     // Create JSON request
//     nlohmann::json request;
//     request["query"] = "SELECT * FROM users";
//     request["format"] = "json";
//     
//     std::string response = client.Query(request.dump());
//     
//     auto json_response = nlohmann::json::parse(response);
//     std::cout << "Got " << json_response["row_count"] << " rows" << std::endl;
//     
//     return 0;
// }