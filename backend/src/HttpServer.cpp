#include "../include/HttpServer.hpp"
#include "../include/Json.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <regex>

HttpServer::HttpServer(int portNum, const std::string& staticDir, std::shared_ptr<ParkingManager> mgr)
    : port(portNum), publicDir(staticDir), listenSocket(INVALID_SOCKET), isRunning(false), manager(mgr) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::stop() {
    isRunning = false;
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }
}

static std::string urlDecode(const std::string& src) {
    std::string ret;
    char ch;
    int i, ii;
    for (i = 0; i < (int)src.length(); i++) {
        if (src[i] == '%') {
            if (i + 2 < (int)src.length()) {
                std::istringstream hexStream(src.substr(i + 1, 2));
                if (hexStream >> std::hex >> ii) {
                    ch = static_cast<char>(ii);
                    ret += ch;
                    i += 2;
                } else {
                    ret += src[i];
                }
            } else {
                ret += src[i];
            }
        } else if (src[i] == '+') {
            ret += ' ';
        } else {
            ret += src[i];
        }
    }
    return ret;
}

std::string HttpServer::getMimeType(const std::string& path) {
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

void HttpServer::sendResponse(SOCKET clientSocket, int statusCode, const std::string& statusText,
                              const std::string& contentType, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
    response << "Access-Control-Allow-Headers: Content-Type\r\n";
    response << "Access-Control-Allow-Private-Network: true\r\n";
    response << "\r\n";
    response << body;

    std::string respStr = response.str();
    send(clientSocket, respStr.c_str(), static_cast<int>(respStr.length()), 0);
}

void HttpServer::sendFileResponse(SOCKET clientSocket, const std::string& filePath, const std::string& contentType) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::string notFoundBody = "{\"error\": \"File not found: " + filePath + "\"}";
        sendResponse(clientSocket, 404, "Not Found", "application/json", notFoundBody);
        return;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string fileContent = ss.str();
    file.close();

    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << fileContent.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "Cache-Control: no-cache\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "\r\n";

    std::string headerStr = response.str();
    send(clientSocket, headerStr.c_str(), static_cast<int>(headerStr.length()), 0);
    send(clientSocket, fileContent.data(), static_cast<int>(fileContent.length()), 0);
}

DWORD WINAPI HttpServer::clientWorker(LPVOID param) {
    ClientContext* ctx = reinterpret_cast<ClientContext*>(param);
    if (ctx && ctx->server) {
        ctx->server->handleClient(ctx->socket);
        closesocket(ctx->socket);
        delete ctx;
    }
    return 0;
}

bool HttpServer::start() {
    WSADATA wsaData;
    int wsaRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaRes != 0) {
        std::cerr << "[HttpServer] WSAStartup failed: " << wsaRes << std::endl;
        return false;
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "[HttpServer] Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(static_cast<u_short>(port));

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[HttpServer] Bind failed on port " << port << ": " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[HttpServer] Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    isRunning = true;
    std::cout << "==========================================================" << std::endl;
    std::cout << "  ParkingFlow C++17 Core DSA Engine running on port " << port << std::endl;
    std::cout << "  Serving static frontend from: " << publicDir << std::endl;
    std::cout << "  Open browser at: http://localhost:" << port << std::endl;
    std::cout << "==========================================================" << std::endl;

    while (isRunning) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (!isRunning) break;
            std::cerr << "[HttpServer] Accept error: " << WSAGetLastError() << std::endl;
            continue;
        }

        ClientContext* ctx = new ClientContext();
        ctx->server = this;
        ctx->socket = clientSocket;

        HANDLE hThread = CreateThread(NULL, 0, clientWorker, ctx, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        } else {
            delete ctx;
            closesocket(clientSocket);
        }
    }

    WSACleanup();
    return true;
}

void HttpServer::handleClient(SOCKET clientSocket) {
    DWORD timeoutMs = 4000;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeoutMs, sizeof(timeoutMs));

    char buffer[16384];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        closesocket(clientSocket);
        return;
    }

    std::string requestStr(buffer, bytesReceived);

    // Read full request body based on Content-Length
    size_t clPos = requestStr.find("Content-Length:");
    if (clPos == std::string::npos) clPos = requestStr.find("content-length:");
    if (clPos != std::string::npos) {
        size_t valStart = clPos + 15;
        while (valStart < requestStr.length() && (requestStr[valStart] == ' ' || requestStr[valStart] == '\t')) valStart++;
        size_t valEnd = valStart;
        while (valEnd < requestStr.length() && (requestStr[valEnd] >= '0' && requestStr[valEnd] <= '9')) valEnd++;
        if (valEnd > valStart) {
            char* pEnd = nullptr;
            size_t expectedBodyLen = static_cast<size_t>(std::strtoul(requestStr.substr(valStart, valEnd - valStart).c_str(), &pEnd, 10));
            while (true) {
                size_t bPos = requestStr.find("\r\n\r\n");
                if (bPos != std::string::npos) {
                    size_t curBodyLen = requestStr.length() - (bPos + 4);
                    if (curBodyLen >= expectedBodyLen) break;
                }
                char extraBuf[4096];
                int n = recv(clientSocket, extraBuf, sizeof(extraBuf), 0);
                if (n <= 0) break;
                requestStr.append(extraBuf, n);
            }
        }
    }

    std::istringstream reqStream(requestStr);

    std::string method, fullPath, protocol;
    reqStream >> method >> fullPath >> protocol;

    // Handle CORS preflight OPTIONS
    if (method == "OPTIONS") {
        sendResponse(clientSocket, 204, "No Content", "text/plain", "");
        return;
    }

    // Split path and query string
    std::string path = fullPath;
    std::string queryString = "";
    size_t qPos = fullPath.find('?');
    if (qPos != std::string::npos) {
        path = fullPath.substr(0, qPos);
        queryString = fullPath.substr(qPos + 1);
    }

    // Extract request body
    std::string body = "";
    size_t bodyPos = requestStr.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        body = requestStr.substr(bodyPos + 4);
    }

    auto getQueryParam = [&](const std::string& key) -> std::string {
        if (queryString.empty()) return "";
        std::string needle = key + "=";
        size_t p = queryString.find(needle);
        if (p == std::string::npos) return "";
        p += needle.length();
        size_t ampPos = queryString.find('&', p);
        if (ampPos == std::string::npos) return urlDecode(queryString.substr(p));
        return urlDecode(queryString.substr(p, ampPos - p));
    };

    // ========================================================================
    // REST API ENDPOINTS
    // ========================================================================

    // 1. GET /api/status - Current status of active plot & gates & EV summary
    if (method == "GET" && path == "/api/status") {
        manager->processWaitingQueue("");
        Plot activePlot;
        manager->getActivePlot(activePlot);

        int total = 0, occupied = 0, available = 0, reserved = 0, evSlots = 0, activeEVs = 0;
        for (const auto& row : activePlot.grid) {
            for (const auto& s : row) {
                bool isGate = false;
                for (const auto& g : activePlot.gates) {
                    if (g.row == s.row && g.col == s.col) { isGate = true; break; }
                }
                if (!isGate && (s.cellType == CellType::SLOT_CAR || s.cellType == CellType::SLOT_BIKE)) {
                    total++;
                    if (s.status == SlotStatus::OCCUPIED) occupied++;
                    else if (s.status == SlotStatus::AVAILABLE) available++;
                    else if (s.status == SlotStatus::RESERVED) reserved++;

                    if (s.hasCharger) {
                        evSlots++;
                        if (s.status == SlotStatus::OCCUPIED) activeEVs++;
                    }
                }
            }
        }

        std::vector<ParkingSession> activeList = manager->getRecords("time");
        std::vector<WaitingRequest> waitList = manager->getWaitingQueue(activePlot.id);

        std::ostringstream json;
        json << "{\n"
             << "  \"activePlot\": {\n"
             << "    \"id\": " << Json::stringValue(activePlot.id) << ",\n"
             << "    \"name\": " << Json::stringValue(activePlot.name) << ",\n"
             << "    \"location\": " << Json::stringValue(activePlot.location) << ",\n"
             << "    \"parkingType\": " << Json::stringValue(activePlot.parkingType) << ",\n"
             << "    \"rows\": " << activePlot.rows << ",\n"
             << "    \"cols\": " << activePlot.cols << ",\n"
             << "    \"ratePerHourCar\": " << Json::numberValue(activePlot.ratePerHourCar) << ",\n"
             << "    \"ratePerHourBike\": " << Json::numberValue(activePlot.ratePerHourBike) << ",\n"
             << "    \"cellScaleMeters\": " << Json::numberValue(activePlot.cellScaleMeters) << ",\n"
             << "    \"gates\": [\n";

        for (size_t g = 0; g < activePlot.gates.size(); ++g) {
            const auto& gate = activePlot.gates[g];
            json << "      {\"id\":" << Json::stringValue(gate.id)
                 << ",\"name\":" << Json::stringValue(gate.name)
                 << ",\"type\":" << Json::stringValue(gateTypeToString(gate.type))
                 << ",\"row\":" << gate.row
                 << ",\"col\":" << gate.col
                 << ",\"isActive\":" << Json::boolValue(gate.isActive) << "}";
            if (g + 1 < activePlot.gates.size()) json << ",";
            json << "\n";
        }

        json << "    ]\n"
             << "  },\n"
             << "  \"stats\": {\n"
             << "    \"totalSlots\": " << total << ",\n"
             << "    \"availableSlots\": " << available << ",\n"
             << "    \"occupiedSlots\": " << occupied << ",\n"
             << "    \"reservedSlots\": " << reserved << ",\n"
             << "    \"occupancyRate\": " << (total > 0 ? (occupied * 100 / total) : 0) << ",\n"
             << "    \"evSlotsCount\": " << evSlots << ",\n"
             << "    \"activeEVCount\": " << activeEVs << ",\n"
             << "    \"waitingCount\": " << waitList.size() << "\n"
             << "  },\n"
             << "  \"grid\": [\n";

        for (size_t r = 0; r < activePlot.grid.size(); ++r) {
            json << "    [";
            for (size_t c = 0; c < activePlot.grid[r].size(); ++c) {
                const auto& s = activePlot.grid[r][c];
                std::string vType = "";
                if (s.status == SlotStatus::OCCUPIED) {
                    vType = vehicleTypeToString(s.currentVehicleType);
                } else {
                    vType = (s.cellType == CellType::SLOT_BIKE ? "TWO_WHEELER" : "CAR");
                }
                json << "{\"id\":" << Json::stringValue(s.id)
                     << ",\"r\":" << s.row << ",\"c\":" << s.col
                     << ",\"type\":" << Json::stringValue(cellTypeToString(s.cellType))
                     << ",\"status\":" << Json::stringValue(slotStatusToString(s.status))
                     << ",\"plate\":" << Json::stringValue(s.currentPlate)
                     << ",\"vehicleType\":" << Json::stringValue(vType)
                     << ",\"dist\":" << s.bfsDistance
                     << ",\"distMeters\":" << Json::numberValue(s.distanceMeters)
                     << ",\"category\":" << Json::stringValue(slotCategoryToString(s.category))
                     << ",\"hasCharger\":" << Json::boolValue(s.hasCharger)
                     << ",\"chargerId\":" << Json::stringValue(s.chargerId)
                     << ",\"connector\":" << Json::stringValue(s.connectorType)
                     << ",\"powerKw\":" << Json::numberValue(s.chargingPowerKw)
                     << ",\"chargerStatus\":" << Json::stringValue(chargerStatusToString(s.chargerStatus))
                     << ",\"chargingRate\":" << Json::numberValue(s.chargingRatePerHour)
                     << "}";
                if (c + 1 < activePlot.grid[r].size()) json << ",";
            }
            json << "]";
            if (r + 1 < activePlot.grid.size()) json << ",";
            json << "\n";
        }
        json << "  ]\n}";

        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 2. GET /api/plots - List all plots
    if (method == "GET" && path == "/api/plots") {
        std::vector<Plot> allPlots = manager->getAllPlots();
        Plot activeP;
        manager->getActivePlot(activeP);

        std::ostringstream json;
        json << "{\"activePlotId\": " << Json::stringValue(activeP.id) << ", \"plots\": [";
        for (size_t i = 0; i < allPlots.size(); ++i) {
            const auto& p = allPlots[i];
            int total = 0, occupied = 0, avail = 0, evSlots = 0;
            for (const auto& row : p.grid) {
                for (const auto& s : row) {
                    bool isGate = false;
                    for (const auto& g : p.gates) {
                        if (g.row == s.row && g.col == s.col) { isGate = true; break; }
                    }
                    if (!isGate && (s.cellType == CellType::SLOT_CAR || s.cellType == CellType::SLOT_BIKE)) {
                        total++;
                        if (s.status == SlotStatus::OCCUPIED) occupied++;
                        else if (s.status == SlotStatus::AVAILABLE) avail++;
                        if (s.hasCharger) evSlots++;
                    }
                }
            }

            json << "{\"id\":" << Json::stringValue(p.id)
                 << ",\"name\":" << Json::stringValue(p.name)
                 << ",\"location\":" << Json::stringValue(p.location)
                 << ",\"parkingType\":" << Json::stringValue(p.parkingType)
                 << ",\"rows\":" << p.rows
                 << ",\"cols\":" << p.cols
                 << ",\"totalSlots\":" << total
                 << ",\"availableSlots\":" << avail
                 << ",\"occupiedSlots\":" << occupied
                 << ",\"evSlotsCount\":" << evSlots
                 << ",\"cellScaleMeters\":" << Json::numberValue(p.cellScaleMeters)
                 << ",\"ratePerHourCar\":" << Json::numberValue(p.ratePerHourCar)
                 << ",\"ratePerHourBike\":" << Json::numberValue(p.ratePerHourBike)
                 << ",\"gatesCount\":" << p.gates.size()
                 << "}";
            if (i + 1 < allPlots.size()) json << ",";
        }
        json << "]}";

        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 3. POST /api/plots - Create new plot from 3-Step Wizard
    if (method == "POST" && path == "/api/plots") {
        std::string name = Json::getStringField(body, "name", "New Parking Plot");
        std::string location = Json::getStringField(body, "location", "Campus");
        std::string parkingType = Json::getStringField(body, "parkingType", "mixed");
        int rows = Json::getIntField(body, "rows", 4);
        int cols = Json::getIntField(body, "cols", 6);
        double rateCar = Json::getDoubleField(body, "ratePerHourCar", 40.0);
        double rateBike = Json::getDoubleField(body, "ratePerHourBike", 20.0);

        Plot p;
        p.id = "plot_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        p.name = name;
        p.location = location;
        p.parkingType = parkingType;
        p.rows = (rows >= 1) ? rows : 4;
        p.cols = (cols >= 1) ? cols : 6;
        p.ratePerHourCar = rateCar > 0 ? rateCar : 40.0;
        p.ratePerHourBike = rateBike > 0 ? rateBike : 20.0;
        p.gracePeriodMinutes = 15;
        p.cellScaleMeters = 5.0;

        // Default Gate setup
        Gate gIn;
        gIn.id = "gate_in_1";
        gIn.name = "Main Entrance";
        gIn.type = GateType::ENTRANCE;
        gIn.row = 0;
        gIn.col = 0;
        gIn.isActive = true;
        p.gates.push_back(gIn);

        Gate gOut;
        gOut.id = "gate_out_1";
        gOut.name = "Main Exit";
        gOut.type = GateType::EXIT;
        gOut.row = p.rows - 1;
        gOut.col = p.cols - 1;
        gOut.isActive = true;
        p.gates.push_back(gOut);

        p.grid.resize(p.rows, std::vector<Slot>(p.cols));

        // Parse gridData if provided from layout wizard
        std::string gridData = Json::getStringField(body, "gridData");
        std::vector<std::vector<std::string>> parsedTypes;
        if (!gridData.empty()) {
            std::stringstream ss(gridData);
            std::string rowStr;
            while (std::getline(ss, rowStr, ';')) {
                std::vector<std::string> rowTypes;
                std::stringstream rss(rowStr);
                std::string cellTypeStr;
                while (std::getline(rss, cellTypeStr, ',')) {
                    rowTypes.push_back(cellTypeStr);
                }
                parsedTypes.push_back(rowTypes);
            }
        }

        for (int r = 0; r < p.rows; ++r) {
            std::string rLabel = Json::getRowLabel(r);
            for (int c = 0; c < p.cols; ++c) {
                Slot s;
                s.row = r;
                s.col = c;
                s.id = rLabel + std::to_string(c + 1);
                s.cellType = CellType::SLOT_CAR;
                s.category = SlotCategory::STANDARD;
                s.status = SlotStatus::AVAILABLE;

                if (r < static_cast<int>(parsedTypes.size()) && c < static_cast<int>(parsedTypes[r].size())) {
                    s.cellType = stringToCellType(parsedTypes[r][c]);
                    if (s.cellType == CellType::SLOT_BIKE) s.category = SlotCategory::BIKE;
                    else if (s.cellType == CellType::FACILITY) s.category = SlotCategory::EV_CHARGING;
                } else if (p.parkingType == "bike") {
                    s.cellType = CellType::SLOT_BIKE;
                    s.category = SlotCategory::BIKE;
                } else if (p.parkingType == "mixed" && r == p.rows - 1 && c >= p.cols / 2) {
                    s.cellType = CellType::SLOT_BIKE;
                    s.category = SlotCategory::BIKE;
                }
                p.grid[r][c] = s;
            }
        }

        std::string err;
        bool ok = manager->createPlot(p, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"plotId\":" << Json::stringValue(p.id)
             << ",\"message\":" << Json::stringValue(ok ? "Plot created successfully" : err) << "}";

        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 4. POST /api/plots/select - Switch active plot
    if (method == "POST" && path == "/api/plots/select") {
        std::string plotId = Json::getStringField(body, "plotId");
        bool ok = manager->setActivePlot(plotId);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"activePlotId\":" << Json::stringValue(plotId) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 5. POST /api/plots/delete - Delete plot
    if (method == "POST" && path == "/api/plots/delete") {
        std::string plotId = Json::getStringField(body, "plotId");
        std::string err;
        bool ok = manager->deletePlot(plotId, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"message\":" << Json::stringValue(ok ? "Plot deleted successfully" : err) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 6. Gate Management Endpoints (Module 1)
    if (method == "POST" && path == "/api/plots/gates/add") {
        std::string plotId = Json::getStringField(body, "plotId");
        Gate g;
        g.id = Json::getStringField(body, "id");
        g.name = Json::getStringField(body, "name", "New Gate");
        g.type = stringToGateType(Json::getStringField(body, "type", "ENTRANCE"));
        g.row = Json::getIntField(body, "row", 0);
        g.col = Json::getIntField(body, "col", 0);
        g.isActive = Json::getBoolField(body, "isActive", true);

        std::string err;
        bool ok = manager->addGate(plotId, g, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"message\":" << Json::stringValue(ok ? "Gate added successfully" : err) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    if (method == "POST" && path == "/api/plots/gates/update") {
        std::string plotId = Json::getStringField(body, "plotId");
        Gate g;
        std::string gateId = Json::getStringField(body, "id");
        if (gateId.empty()) gateId = Json::getStringField(body, "gateId");
        g.id = gateId;
        g.name = Json::getStringField(body, "name", "Gate");
        g.type = stringToGateType(Json::getStringField(body, "type", "ENTRANCE"));
        g.row = Json::getIntField(body, "row", 0);
        g.col = Json::getIntField(body, "col", 0);
        g.isActive = Json::getBoolField(body, "isActive", true);

        std::string err;
        bool ok = manager->updateGate(plotId, g, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"message\":" << Json::stringValue(ok ? "Gate updated successfully" : err) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // Cell Layout Painting & Route Editing (Module 1 Point 5)
    if (method == "POST" && path == "/api/plots/cell/edit") {
        std::string plotId = Json::getStringField(body, "plotId");
        int row = Json::getIntField(body, "row", 0);
        int col = Json::getIntField(body, "col", 0);
        std::string typeStr = Json::getStringField(body, "cellType");
        if (typeStr.empty()) typeStr = Json::getStringField(body, "type", "AISLE");
        CellType ct = stringToCellType(typeStr);

        bool ok = manager->recordCellEdit(plotId, row, col, ct);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"row\":" << row
             << ",\"col\":" << col
             << ",\"cellType\":" << Json::stringValue(cellTypeToString(ct))
             << ",\"message\":" << Json::stringValue(ok ? "Cell layout updated successfully" : "Failed to update cell layout") << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    if (method == "POST" && path == "/api/plots/cell/undo") {
        std::string plotId = Json::getStringField(body, "plotId");
        int row = 0, col = 0;
        CellType reverted = CellType::SLOT_CAR;
        bool ok = manager->undoLastEdit(plotId, row, col, reverted);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"row\":" << row
             << ",\"col\":" << col
             << ",\"revertedType\":" << Json::stringValue(cellTypeToString(reverted))
             << ",\"message\":" << Json::stringValue(ok ? "Last layout edit undone successfully" : "Undo stack is empty") << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    if (method == "POST" && path == "/api/plots/gates/delete") {
        std::string plotId = Json::getStringField(body, "plotId");
        std::string gateId = Json::getStringField(body, "gateId");
        std::string err;
        bool ok = manager->removeGate(plotId, gateId, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"message\":" << Json::stringValue(ok ? "Gate removed successfully" : err) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    if (method == "POST" && path == "/api/plots/gates/toggle") {
        std::string plotId = Json::getStringField(body, "plotId");
        std::string gateId = Json::getStringField(body, "gateId");
        bool active = Json::getBoolField(body, "active", true);
        std::string err;
        bool ok = manager->toggleGate(plotId, gateId, active, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"message\":" << Json::stringValue(ok ? "Gate status updated" : err) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    if (method == "POST" && path == "/api/plots/scale") {
        std::string plotId = Json::getStringField(body, "plotId");
        double scale = Json::getDoubleField(body, "cellScaleMeters", 0.0);
        if (scale <= 0.0) scale = Json::getDoubleField(body, "scaleMeters", 0.0);
        if (scale <= 0.0) scale = Json::getDoubleField(body, "scale", 0.0);
        if (scale <= 0.0) scale = 5.0;

        std::string err;
        bool ok = manager->updateScale(plotId, scale, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"cellScaleMeters\":" << Json::numberValue(scale)
             << ",\"message\":" << Json::stringValue(ok ? "Map scale calibrated successfully" : err) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 7. EV Charging Slot Configuration Endpoints (Module 5)
    if (method == "POST" && path == "/api/plots/slots/charger") {
        std::string plotId = Json::getStringField(body, "plotId");
        std::string slotId = Json::getStringField(body, "slotId");
        bool hasCharger = Json::getBoolField(body, "hasCharger", true);
        std::string chargerId = Json::getStringField(body, "chargerId", "EV-CHG-01");
        std::string connType = Json::getStringField(body, "connectorType", "CCS2");
        if (connType.empty() || connType == "CCS2") {
            std::string altConn = Json::getStringField(body, "connector");
            if (!altConn.empty()) connType = altConn;
        }
        double powerKw = Json::getDoubleField(body, "powerKw", 22.0);
        std::string statStr = Json::getStringField(body, "chargerStatus");
        if (statStr.empty()) statStr = Json::getStringField(body, "status", "OPERATIONAL");
        ChargerStatus status = stringToChargerStatus(statStr);
        double rate = Json::getDoubleField(body, "chargingRate", 0.0);
        if (rate <= 0.0) rate = Json::getDoubleField(body, "ratePerHour", 50.0);

        std::string err;
        bool ok = manager->configureSlotCharger(plotId, slotId, hasCharger, chargerId, connType, powerKw, status, rate, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"message\":" << Json::stringValue(ok ? "EV Charger configured successfully" : err) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    if (method == "POST" && (path == "/api/plots/slots/charger-status" || path == "/api/plots/slots/charger/status")) {
        std::string plotId = Json::getStringField(body, "plotId");
        std::string slotId = Json::getStringField(body, "slotId");
        std::string statStr = Json::getStringField(body, "chargerStatus");
        if (statStr.empty()) statStr = Json::getStringField(body, "status", "OPERATIONAL");
        ChargerStatus status = stringToChargerStatus(statStr);

        std::string err;
        bool ok = manager->toggleChargerStatus(plotId, slotId, status, err);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"message\":" << Json::stringValue(ok ? "Charger status updated" : err) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 8. Layout Validation Endpoint (Module 1 & 2)
    if (method == "GET" && path == "/api/plots/validate") {
        std::string plotId = getQueryParam("plotId");
        LayoutValidationReport rep = manager->validateLayout(plotId);

        std::ostringstream json;
        json << "{\"isValid\":" << (rep.allSlotsReachable && rep.warnings.empty() ? "true" : "false")
             << ",\"allSlotsReachable\":" << (rep.allSlotsReachable ? "true" : "false")
             << ",\"totalSlots\":" << rep.totalSlots
             << ",\"reachableSlots\":" << rep.reachableSlots
             << ",\"disconnectedSlots\":" << rep.disconnectedSlots
             << ",\"activeEntrances\":" << rep.activeEntrances
             << ",\"activeExits\":" << rep.activeExits
             << ",\"unreachableSlotIds\":[";

        for (size_t i = 0; i < rep.unreachableSlotIds.size(); ++i) {
            json << Json::stringValue(rep.unreachableSlotIds[i]);
            if (i + 1 < rep.unreachableSlotIds.size()) json << ",";
        }
        json << "],\"warnings\":[";
        for (size_t i = 0; i < rep.warnings.size(); ++i) {
            json << Json::stringValue(rep.warnings[i]);
            if (i + 1 < rep.warnings.size()) json << ",";
        }
        json << "]}";

        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 9. Route & Calibrated Distance Calculation (Modules 2, 3, 7, 8)
    if (method == "GET" && path == "/api/plots/route") {
        std::string plotId = getQueryParam("plotId");
        std::string slotId = getQueryParam("slotId");
        std::string gateId = getQueryParam("gateId");
        bool isExit = (getQueryParam("isExit") == "true" || getQueryParam("isExit") == "1");

        PathResult p = manager->getRoute(plotId, slotId, gateId, isExit);

        std::ostringstream json;
        json << "{\"found\":" << (p.found ? "true" : "false")
             << ",\"distance\":" << p.distance
             << ",\"distanceMeters\":" << Json::numberValue(p.distanceMeters)
             << ",\"estimatedTimeSeconds\":" << p.estimatedTimeSeconds
             << ",\"fromGateId\":" << Json::stringValue(p.fromGateId)
             << ",\"fromGateName\":" << Json::stringValue(p.fromGateName)
             << ",\"path\":[";
        for (size_t i = 0; i < p.path.size(); ++i) {
            json << "[" << p.path[i].first << "," << p.path[i].second << "]";
            if (i + 1 < p.path.size()) json << ",";
        }
        json << "]}";

        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 10. POST /api/park - Park vehicle (Modules 4, 6)
    if (method == "POST" && path == "/api/park") {
        std::string plotId = Json::getStringField(body, "plotId");
        std::string plate = Json::getStringField(body, "plate");
        std::string typeStr = Json::getStringField(body, "vehicleType", "CAR");
        bool isPriority = Json::getBoolField(body, "isPriority", false);
        std::string owner = Json::getStringField(body, "ownerName", "Guest");
        std::string phone = Json::getStringField(body, "phone", "");

        // Module 4 & 6: EV Charging Field & Gate
        bool requiresEVCharging = Json::getBoolField(body, "requiresEVCharging", false);
        std::string preferredConnector = Json::getStringField(body, "preferredConnector", "ANY");
        std::string selectedGateId = Json::getStringField(body, "selectedGateId", "");

        // Validate phone number: exactly 10 digits
        std::string cleanPhone = "";
        for (char ch : phone) {
            if (isdigit(static_cast<unsigned char>(ch))) cleanPhone += ch;
        }
        if (cleanPhone.length() != 10) {
            sendResponse(clientSocket, 400, "Bad Request", "application/json",
                         "{\"success\":false,\"message\":\"Phone number is compulsory and must be exactly 10 digits.\"}");
            return;
        }

        // Validate license plate format
        std::string cleanPlate = "";
        for (char ch : plate) {
            if (isalnum(static_cast<unsigned char>(ch))) cleanPlate += static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        }
        std::regex platePattern("^([A-Z]{2}[0-9]{1,2}[A-Z]{1,3}[0-9]{4}|[0-9]{2}BH[0-9]{4}[A-Z]{1,2})$");
        if (!std::regex_match(cleanPlate, platePattern)) {
            sendResponse(clientSocket, 400, "Bad Request", "application/json",
                         "{\"success\":false,\"message\":\"Invalid license plate number format (e.g. DL01AB1234).\"}");
            return;
        }

        VehicleType vt = stringToVehicleType(typeStr);
        ParkResult res = manager->parkVehicle(plotId, cleanPlate, vt, isPriority, owner, cleanPhone, 
                                             requiresEVCharging, preferredConnector, selectedGateId);

        std::ostringstream json;
        json << "{\"success\":" << (res.success ? "true" : "false")
             << ",\"isQueued\":" << (res.isQueued ? "true" : "false")
             << ",\"message\":" << Json::stringValue(res.message)
             << ",\"slotId\":" << Json::stringValue(res.slotId)
             << ",\"sessionId\":" << Json::stringValue(res.sessionId)
             << ",\"bfsDistance\":" << res.bfsDistance
             << ",\"distanceMeters\":" << Json::numberValue(res.distanceMeters)
             << ",\"estimatedTimeSeconds\":" << res.estimatedTimeSeconds
             << ",\"gateId\":" << Json::stringValue(res.gateId)
             << ",\"gateName\":" << Json::stringValue(res.gateName)
             << ",\"isEVChargingAssigned\":" << Json::boolValue(res.isEVChargingAssigned)
             << ",\"chargerId\":" << Json::stringValue(res.chargerId)
             << ",\"connectorType\":" << Json::stringValue(res.connectorType)
             << "}";

        sendResponse(clientSocket, res.success ? 200 : 400, res.success ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 11. POST /api/exit - Exit vehicle & Departure routing (Module 8)
    if (method == "POST" && path == "/api/exit") {
        std::string identifier = Json::getStringField(body, "identifier");
        std::string exitGateId = Json::getStringField(body, "exitGateId", "");

        CheckoutResult res = manager->exitVehicle(identifier, exitGateId);

        std::ostringstream json;
        json << "{\"success\":" << (res.success ? "true" : "false")
             << ",\"message\":" << Json::stringValue(res.message)
             << ",\"exitGateId\":" << Json::stringValue(res.exitGateId)
             << ",\"exitGateName\":" << Json::stringValue(res.exitGateName)
             << ",\"exitDistanceMeters\":" << Json::numberValue(res.exitDistanceMeters)
             << ",\"parkingFee\":" << Json::numberValue(res.parkingFee)
             << ",\"chargingFee\":" << Json::numberValue(res.chargingFee)
             << ",\"totalFee\":" << Json::numberValue(res.totalFee);

        if (res.success) {
            json << ",\"session\":{"
                 << "\"sessionId\":" << Json::stringValue(res.session.sessionId)
                 << ",\"plate\":" << Json::stringValue(res.session.vehiclePlate)
                 << ",\"slotId\":" << Json::stringValue(res.session.slotId)
                 << ",\"entryTime\":" << Json::stringValue(res.session.entryTimeFormatted)
                 << ",\"exitTime\":" << Json::stringValue(res.session.exitTimeFormatted)
                 << ",\"durationMinutes\":" << res.session.durationMinutes
                 << ",\"fee\":" << Json::numberValue(res.session.fee)
                 << ",\"chargingFee\":" << Json::numberValue(res.session.chargingFee)
                 << ",\"entryGateName\":" << Json::stringValue(res.session.entryGateName)
                 << ",\"entryDistanceMeters\":" << Json::numberValue(res.session.entryDistanceMeters)
                 << ",\"exitGateName\":" << Json::stringValue(res.session.exitGateName)
                 << ",\"exitDistanceMeters\":" << Json::numberValue(res.session.exitDistanceMeters)
                 << ",\"requiresEVCharging\":" << Json::boolValue(res.session.requiresEVCharging)
                 << ",\"chargerId\":" << Json::stringValue(res.session.chargerId)
                 << "}"
                 << ",\"autoAllocated\":" << (res.autoAllocatedQueued ? "true" : "false")
                 << ",\"autoPlate\":" << Json::stringValue(res.autoPlate)
                 << ",\"autoSlotId\":" << Json::stringValue(res.autoSlotId);
        }
        json << "}";

        sendResponse(clientSocket, res.success ? 200 : 400, res.success ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 12. POST /api/reservations - Create reservation
    if (method == "POST" && path == "/api/reservations") {
        Reservation r;
        r.plotId = Json::getStringField(body, "plotId");
        r.slotId = Json::getStringField(body, "slotId");
        r.vehiclePlate = Json::getStringField(body, "vehiclePlate");
        r.vehicleType = stringToVehicleType(Json::getStringField(body, "vehicleType", "CAR"));
        r.customerName = Json::getStringField(body, "customerName");
        r.phone = Json::getStringField(body, "phone");
        r.scheduledTime = Json::getStringField(body, "scheduledTime");

        std::string msg;
        bool ok = manager->createReservation(r, msg);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"message\":" << Json::stringValue(msg) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 13. GET /api/reservations - List reservations
    if (method == "GET" && path == "/api/reservations") {
        std::string plotId = getQueryParam("plotId");
        std::vector<Reservation> resList = manager->getActiveReservations(plotId);

        std::ostringstream json;
        json << "{\"reservations\":[";
        for (size_t i = 0; i < resList.size(); ++i) {
            const auto& r = resList[i];
            json << "{\"reservationId\":" << Json::stringValue(r.reservationId)
                 << ",\"plotId\":" << Json::stringValue(r.plotId)
                 << ",\"slotId\":" << Json::stringValue(r.slotId)
                 << ",\"vehiclePlate\":" << Json::stringValue(r.vehiclePlate)
                 << ",\"vehicleType\":" << Json::stringValue(vehicleTypeToString(r.vehicleType))
                 << ",\"customerName\":" << Json::stringValue(r.customerName)
                 << ",\"phone\":" << Json::stringValue(r.phone)
                 << ",\"scheduledTime\":" << Json::stringValue(r.scheduledTime)
                 << "}";
            if (i + 1 < resList.size()) json << ",";
        }
        json << "]}";

        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 14. POST /api/reservations/cancel
    if (method == "POST" && path == "/api/reservations/cancel") {
        std::string rId = Json::getStringField(body, "reservationId");
        std::string msg;
        bool ok = manager->cancelReservation(rId, msg);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false") << ",\"message\":" << Json::stringValue(msg) << "}";
        sendResponse(clientSocket, ok ? 200 : 400, ok ? "OK" : "Bad Request", "application/json", json.str());
        return;
    }

    // 15. GET /api/waiting
    if (method == "GET" && path == "/api/waiting") {
        std::string plotId = getQueryParam("plotId");
        std::vector<WaitingRequest> list = manager->getWaitingQueue(plotId);

        std::ostringstream json;
        json << "{\"waitingList\":[";
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& w = list[i];
            json << "{\"waitId\":" << Json::stringValue(w.waitId)
                 << ",\"plotId\":" << Json::stringValue(w.plotId)
                 << ",\"plate\":" << Json::stringValue(w.vehiclePlate)
                 << ",\"vehicleType\":" << Json::stringValue(vehicleTypeToString(w.vehicleType))
                 << ",\"ownerName\":" << Json::stringValue(w.ownerName)
                 << ",\"phone\":" << Json::stringValue(w.phone)
                 << ",\"isPriority\":" << (w.isPriority ? "true" : "false")
                 << ",\"requiresEVCharging\":" << (w.requiresEVCharging ? "true" : "false")
                 << ",\"preferredConnector\":" << Json::stringValue(w.preferredConnector)
                 << ",\"requestTime\":" << Json::stringValue(w.requestTimeFormatted)
                 << "}";
            if (i + 1 < list.size()) json << ",";
        }
        json << "]}";

        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 15a. POST /api/waiting/process - Force process & auto-allocate waiting queue into empty slots
    if (method == "POST" && path == "/api/waiting/process") {
        std::string plotId = Json::getStringField(body, "plotId");
        if (plotId.empty()) plotId = getQueryParam("plotId");
        std::vector<ParkResult> promoted = manager->processWaitingQueue(plotId);
        std::ostringstream json;
        json << "{\"success\":true,\"promotedCount\":" << promoted.size() << ",\"promoted\":[";
        for (size_t i = 0; i < promoted.size(); ++i) {
            json << "{\"plate\":" << Json::stringValue(promoted[i].waitingRequest.vehiclePlate)
                 << ",\"slotId\":" << Json::stringValue(promoted[i].slotId)
                 << ",\"gateName\":" << Json::stringValue(promoted[i].gateName)
                 << ",\"message\":" << Json::stringValue(promoted[i].message)
                 << "}";
            if (i + 1 < promoted.size()) json << ",";
        }
        json << "]}";
        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 15b. POST /api/waiting/cancel or POST /api/waiting/remove or DELETE /api/waiting
    if ((method == "POST" && (path == "/api/waiting/cancel" || path == "/api/waiting/remove")) ||
        (method == "DELETE" && path == "/api/waiting")) {
        std::string plotId = Json::getStringField(body, "plotId");
        std::string plate = Json::getStringField(body, "plate");
        if (plate.empty()) plate = Json::getStringField(body, "vehiclePlate");
        if (plate.empty()) plate = Json::getStringField(body, "waitId");
        if (plotId.empty()) plotId = getQueryParam("plotId");
        if (plate.empty()) plate = getQueryParam("plate");
        if (plate.empty()) plate = getQueryParam("vehiclePlate");
        if (plate.empty()) plate = getQueryParam("waitId");

        bool ok = manager->cancelWaiting(plotId, plate);
        std::ostringstream json;
        json << "{\"success\":" << (ok ? "true" : "false")
             << ",\"status\":" << Json::stringValue(ok ? "success" : "not_found")
             << ",\"message\":" << Json::stringValue(ok ? "Vehicle removed from waiting queue" : "Vehicle not found in waiting queue")
             << "}";
        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 16. GET /api/records - Quicksort-powered Vehicle Records
    if (method == "GET" && path == "/api/records") {
        std::string sort = getQueryParam("sort");
        std::vector<ParkingSession> records = manager->getRecords(sort.empty() ? "time" : sort);

        std::ostringstream json;
        json << "{\"records\":[";
        for (size_t i = 0; i < records.size(); ++i) {
            const auto& s = records[i];
            json << "{\"sessionId\":" << Json::stringValue(s.sessionId)
                 << ",\"plotId\":" << Json::stringValue(s.plotId)
                 << ",\"slotId\":" << Json::stringValue(s.slotId)
                 << ",\"plate\":" << Json::stringValue(s.vehiclePlate)
                 << ",\"vehicleType\":" << Json::stringValue(vehicleTypeToString(s.vehicleType))
                 << ",\"ownerName\":" << Json::stringValue(s.ownerName)
                 << ",\"phone\":" << Json::stringValue(s.phone)
                 << ",\"entryTime\":" << Json::stringValue(s.entryTimeFormatted)
                 << ",\"exitTime\":" << Json::stringValue(s.exitTimeFormatted)
                 << ",\"durationMinutes\":" << s.durationMinutes
                 << ",\"fee\":" << Json::numberValue(s.fee)
                 << ",\"chargingFee\":" << Json::numberValue(s.chargingFee)
                 << ",\"entryGateName\":" << Json::stringValue(s.entryGateName)
                 << ",\"entryDistMeters\":" << Json::numberValue(s.entryDistanceMeters)
                 << ",\"exitGateName\":" << Json::stringValue(s.exitGateName)
                 << ",\"exitDistMeters\":" << Json::numberValue(s.exitDistanceMeters)
                 << ",\"requiresEVCharging\":" << Json::boolValue(s.requiresEVCharging)
                 << ",\"chargerId\":" << Json::stringValue(s.chargerId)
                 << ",\"isActive\":" << (s.isActive ? "true" : "false")
                 << "}";
            if (i + 1 < records.size()) json << ",";
        }
        json << "]}";

        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 17. GET /api/search - O(1) Hash Map & O(log n) BST lookup
    if (method == "GET" && path == "/api/search") {
        std::string plate = getQueryParam("plate");
        ParkingSession sess;
        Slot slot;
        Plot plot;
        bool found = manager->searchVehicle(plate, sess, slot, plot);

        if (found) {
            std::ostringstream json;
            json << "{\"found\":true"
                 << ",\"sessionId\":" << Json::stringValue(sess.sessionId)
                 << ",\"plotName\":" << Json::stringValue(plot.name)
                 << ",\"slotId\":" << Json::stringValue(sess.slotId)
                 << ",\"plate\":" << Json::stringValue(sess.vehiclePlate)
                 << ",\"vehicleType\":" << Json::stringValue(vehicleTypeToString(sess.vehicleType))
                 << ",\"ownerName\":" << Json::stringValue(sess.ownerName)
                 << ",\"entryTime\":" << Json::stringValue(sess.entryTimeFormatted)
                 << ",\"isActive\":" << (sess.isActive ? "true" : "false")
                 << ",\"requiresEVCharging\":" << Json::boolValue(sess.requiresEVCharging)
                 << ",\"chargerId\":" << Json::stringValue(sess.chargerId)
                 << ",\"entryGateName\":" << Json::stringValue(sess.entryGateName)
                 << ",\"entryDistMeters\":" << Json::numberValue(sess.entryDistanceMeters)
                 << "}";
            sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        } else {
            sendResponse(clientSocket, 404, "Not Found", "application/json", 
                         "{\"found\":false,\"message\":\"No vehicle record found for " + plate + "\"}");
        }
        return;
    }

    // 18. GET /api/dsa/telemetry
    if (method == "GET" && path == "/api/dsa/telemetry") {
        DSATelemetryInfo t = manager->getTelemetry();
        std::ostringstream json;
        json << "{"
             << "\"totalSlots\":" << t.totalSlots
             << ",\"occupiedSlots\":" << t.occupiedSlots
             << ",\"availableSlots\":" << t.availableSlots
             << ",\"reservedSlots\":" << t.reservedSlots
             << ",\"activeSessions\":" << t.activeSessionsCount
             << ",\"bstNodes\":" << t.bstNodeCount
             << ",\"bstHeight\":" << t.bstHeight
             << ",\"waitingQueue\":" << t.waitingQueueCount
             << ",\"priorityQueue\":" << t.priorityQueueCount
             << ",\"undoStack\":" << t.undoStackSize
             << ",\"doublyLinkedList\":" << t.doublyLinkedListCount
             << ",\"graphVertices\":" << t.graphVertexCount
             << ",\"totalPlots\":" << t.totalPlotsCount
             << ",\"evSlotsCount\":" << t.evSlotsCount
             << ",\"activeEVChargingCount\":" << t.activeEVChargingCount
             << ",\"totalGatesCount\":" << t.totalGatesCount
             << "}";
        sendResponse(clientSocket, 200, "OK", "application/json", json.str());
        return;
    }

    // 19. POST /api/dsa/test - Run automated DSA test suite
    if (method == "POST" && path == "/api/dsa/test") {
        std::string testJson = manager->runDSATestsJson();
        sendResponse(clientSocket, 200, "OK", "application/json", testJson);
        return;
    }

    // ========================================================================
    // STATIC FILE SERVING
    // ========================================================================
    if (path == "/" || path.empty()) {
        path = "/index.html";
    }

    std::string fullFilePath = publicDir + path;
    std::string mime = getMimeType(fullFilePath);
    sendFileResponse(clientSocket, fullFilePath, mime);
}
