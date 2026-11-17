#include <iostream>
#include <memory>
#include <string>
#include <windows.h>
#include "../include/ParkingManager.hpp"
#include "../include/HttpServer.hpp"

static HttpServer* g_server = nullptr;

BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        std::cout << "\n[ParkingFlow] Graceful shutdown initiated..." << std::endl;
        if (g_server) {
            g_server->stop();
        }
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string publicDir = "public";
    std::string dataDir = "data";

    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    if (argc > 2) {
        publicDir = argv[2];
    }

    std::cout << "=================================================================" << std::endl;
    std::cout << "    PARKINGFLOW - SMART PARKING SLOT MANAGEMENT SYSTEM           " << std::endl;
    std::cout << "    Second-Year Engineering DSA Project (C++17 Engine Core)      " << std::endl;
    std::cout << "=================================================================" << std::endl;
    std::cout << "[DSA Core] Initializing Data Structures & Algorithms..." << std::endl;
    std::cout << "  - 2D Matrix Grid: Parking Layout Representation" << std::endl;
    std::cout << "  - Graph BFS & DFS: Shortest Route Navigation & Reachability" << std::endl;
    std::cout << "  - Binary Search Tree (BST): Vehicle Record Storage & Search" << std::endl;
    std::cout << "  - Hash Table (unordered_map): O(1) Instant Plate Lookup" << std::endl;
    std::cout << "  - Dual Queues: FIFO & VIP Priority Waiting Manager" << std::endl;
    std::cout << "  - Stack: Layout Designer Multi-Level Undo Operations" << std::endl;
    std::cout << "  - Doubly Linked List: Dynamic Chronological Session History" << std::endl;
    std::cout << "  - Quicksort & Binary Search: Fast Sorting & Records Lookup" << std::endl;
    std::cout << "-----------------------------------------------------------------" << std::endl;

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    auto parkingManager = std::make_shared<ParkingManager>(dataDir);
    parkingManager->initialize();

    HttpServer server(port, publicDir, parkingManager);
    g_server = &server;

    std::cout << "[Storage] Persistent records loaded from ./" << dataDir << "/" << std::endl;
    std::cout << "[Web Interface] Serving application at http://localhost:" << port << std::endl;
    std::cout << "Press Ctrl+C to terminate server." << std::endl << std::endl;

    if (!server.start()) {
        std::cerr << "[Error] Failed to start HTTP server." << std::endl;
        return 1;
    }

    return 0;
}
