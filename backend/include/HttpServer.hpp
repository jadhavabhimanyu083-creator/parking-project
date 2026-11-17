#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <memory>
#include "ParkingManager.hpp"

class HttpServer {
private:
    int port;
    std::string publicDir;
    SOCKET listenSocket;
    bool isRunning;
    std::shared_ptr<ParkingManager> manager;

public:
    HttpServer(int portNum, const std::string& staticDir, std::shared_ptr<ParkingManager> mgr);
    ~HttpServer();

    bool start();
    void stop();

    static DWORD WINAPI clientWorker(LPVOID param);

private:
    void handleClient(SOCKET clientSocket);
    void sendResponse(SOCKET clientSocket, int statusCode, const std::string& statusText,
                      const std::string& contentType, const std::string& body);
    void sendFileResponse(SOCKET clientSocket, const std::string& filePath, const std::string& contentType);
    std::string getMimeType(const std::string& path);
};

struct ClientContext {
    HttpServer* server;
    SOCKET socket;
};

#endif // HTTP_SERVER_HPP
