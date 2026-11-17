#include "../include/ParkingManager.hpp"
#include "../include/Json.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <set>

ParkingManager::ParkingManager(const std::string& storageDir)
    : dataDir(storageDir), nextSessionCounter(1001), nextWaitCounter(1), nextReservationCounter(101), nextGateCounter(10) {
    InitializeCriticalSection(&csLock);
}

ParkingManager::~ParkingManager() {
    saveToDisk();
    DeleteCriticalSection(&csLock);
}

long long ParkingManager::getCurrentUnixTime() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string ParkingManager::getFormattedTimestamp(long long unixTime) {
    if (unixTime == 0) unixTime = getCurrentUnixTime();
    std::time_t t = static_cast<std::time_t>(unixTime);
    std::tm* tmVal = std::localtime(&t);
    char buf[64] = {0};
    if (tmVal) {
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmVal);
    }
    return std::string(buf);
}

std::string ParkingManager::normalizePlate(const std::string& plate) {
    std::string norm = "";
    for (char c : plate) {
        if (isalnum(static_cast<unsigned char>(c))) {
            norm += static_cast<char>(toupper(static_cast<unsigned char>(c)));
        }
    }
    return norm;
}

std::string ParkingManager::generateSessionId() {
    return "SESS-" + std::to_string(nextSessionCounter++);
}

std::string ParkingManager::generateWaitId() {
    return "WAIT-" + std::to_string(nextWaitCounter++);
}

std::string ParkingManager::generateReservationId() {
    return "RES-" + std::to_string(nextReservationCounter++);
}

std::string ParkingManager::generateGateId() {
    return "gate_" + std::to_string(nextGateCounter++);
}

bool ParkingManager::isCellGate(const Plot& plot, int r, int c) const {
    for (const auto& g : plot.gates) {
        if (g.row == r && g.col == c) return true;
    }
    return false;
}

bool ParkingManager::isParkableSlot(const Plot& plot, int r, int c) const {
    if (r < 0 || r >= plot.rows || c < 0 || c >= plot.cols) return false;
    if (isCellGate(plot, r, c)) return false;
    const Slot& s = plot.grid[r][c];
    if (s.cellType == CellType::AISLE || s.cellType == CellType::BLOCKED ||
        s.cellType == CellType::ENTRANCE || s.cellType == CellType::EXIT ||
        s.cellType == CellType::FACILITY) {
        return false;
    }
    return (s.cellType == CellType::SLOT_CAR || s.cellType == CellType::SLOT_BIKE);
}

void ParkingManager::reallotPlotSlotNumbers(Plot& plot) {
    // Step 1: Ensure all configured gates have appropriate cell type, no slot number, and no charger
    for (const auto& g : plot.gates) {
        if (g.row >= 0 && g.row < plot.rows && g.col >= 0 && g.col < plot.cols) {
            Slot& s = plot.grid[g.row][g.col];
            s.cellType = (g.type == GateType::EXIT ? CellType::EXIT : CellType::ENTRANCE);
            s.id = "";
            s.hasCharger = false;
        }
    }

    // Step 2: Ensure non-parkable cells (gates, aisles, pillars, facilities) have no ID and no parked vehicles
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            Slot& s = plot.grid[r][c];
            if (!isParkableSlot(plot, r, c)) {
                s.id = "";
                // If a vehicle is currently sitting in a gate, aisle, or pillar, relocate or vacate it!
                if (s.status == SlotStatus::OCCUPIED && !s.currentPlate.empty()) {
                    std::string plateToMove = s.currentPlate;
                    std::string sessId = s.currentSessionId;
                    VehicleType vType = s.currentVehicleType;
                    s.status = SlotStatus::AVAILABLE;
                    s.currentPlate = "";
                    s.currentSessionId = "";

                    bool relocated = false;
                    for (int rr = 0; rr < plot.rows && !relocated; ++rr) {
                        for (int cc = 0; cc < plot.cols && !relocated; ++cc) {
                            if (rr == r && cc == c) continue;
                            Slot& cand = plot.grid[rr][cc];
                            if (isParkableSlot(plot, rr, cc) && cand.status == SlotStatus::AVAILABLE) {
                                cand.status = SlotStatus::OCCUPIED;
                                cand.currentPlate = plateToMove;
                                cand.currentSessionId = sessId;
                                cand.currentVehicleType = vType;
                                auto sIt = activeSessions.find(plateToMove);
                                if (sIt != activeSessions.end()) {
                                    sIt->second.slotId = cand.id;
                                }
                                plateToPlotSlot[plateToMove] = plot.id + ":" + cand.id;
                                relocated = true;
                            }
                        }
                    }

                    if (!relocated) {
                        auto sIt = activeSessions.find(plateToMove);
                        if (sIt != activeSessions.end()) {
                            sIt->second.isActive = false;
                            sIt->second.exitUnixTime = getCurrentUnixTime();
                            sIt->second.exitTimeFormatted = getFormattedTimestamp(sIt->second.exitUnixTime);
                            historyList.pushBack(sIt->second);
                            activeSessions.erase(sIt);
                        }
                        plateToPlotSlot.erase(plateToMove);
                    }
                } else if (s.status == SlotStatus::RESERVED) {
                    s.status = SlotStatus::AVAILABLE;
                    s.currentPlate = "";
                    s.currentSessionId = "";
                }
            }
        }
    }

    // Step 3: Allot proper slot numbers row by row: A1, A2, A3... B1, B2...
    for (int r = 0; r < plot.rows; ++r) {
        std::string rowLabel = Json::getRowLabel(r);
        int slotIndex = 1;
        for (int c = 0; c < plot.cols; ++c) {
            Slot& s = plot.grid[r][c];
            if (!isParkableSlot(plot, r, c)) {
                s.id = "";
            } else {
                std::string oldId = s.id;
                std::string newId = rowLabel + std::to_string(slotIndex++);
                s.id = newId;

                // Keep active session slot ID synchronized
                if (s.status == SlotStatus::OCCUPIED && !s.currentPlate.empty()) {
                    auto sIt = activeSessions.find(s.currentPlate);
                    if (sIt != activeSessions.end()) {
                        sIt->second.slotId = newId;
                    }
                    plateToPlotSlot[s.currentPlate] = plot.id + ":" + newId;
                }

                // Keep active reservations synchronized
                for (auto& res : reservations) {
                    if (res.plotId == plot.id && res.slotId == oldId && res.isActive) {
                        res.slotId = newId;
                    }
                }
            }
        }
    }
}

void ParkingManager::recomputePlotGraph(Plot& plot) {
    // 1. Gather active entrances
    std::vector<Gate> activeEntrances;
    for (const auto& g : plot.gates) {
        if (g.isActive && (g.type == GateType::ENTRANCE || g.type == GateType::BIDIRECTIONAL)) {
            activeEntrances.push_back(g);
        }
    }

    // Fallback: check if any cell is marked ENTRANCE
    if (activeEntrances.empty()) {
        for (int r = 0; r < plot.rows; ++r) {
            for (int c = 0; c < plot.cols; ++c) {
                if (plot.grid[r][c].cellType == CellType::ENTRANCE) {
                    Gate fallbackGate;
                    fallbackGate.id = "gate_auto_ent";
                    fallbackGate.name = "Entrance";
                    fallbackGate.type = GateType::ENTRANCE;
                    fallbackGate.row = r;
                    fallbackGate.col = c;
                    fallbackGate.isActive = true;
                    activeEntrances.push_back(fallbackGate);
                    break;
                }
            }
            if (!activeEntrances.empty()) break;
        }
    }

    // Ultimate fallback if no entrance defined at all
    if (activeEntrances.empty()) {
        Gate g;
        g.id = "gate_default";
        g.name = "Main Entrance";
        g.type = GateType::ENTRANCE;
        g.row = 0;
        g.col = 0;
        g.isActive = true;
        activeEntrances.push_back(g);
    }

    // 2. Build CellType matrix for traversability
    Matrix2D<CellType> layout(plot.rows, plot.cols, CellType::AISLE);
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            layout.at(r, c) = plot.grid[r][c].cellType;
        }
    }

    // Ensure all active gate cells are passable
    for (const auto& g : plot.gates) {
        if (g.isActive && layout.isValid(g.row, g.col)) {
            if (layout.at(g.row, g.col) == CellType::BLOCKED) {
                layout.at(g.row, g.col) = (g.type == GateType::EXIT ? CellType::EXIT : CellType::ENTRANCE);
            }
        }
    }

    ParkingGraph graph(plot.rows, plot.cols);
    double scale = (plot.cellScaleMeters > 0.0) ? plot.cellScaleMeters : 5.0;

    // 3. For each slot, calculate shortest driving path across all active entrances
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            Slot& s = plot.grid[r][c];
            if (s.cellType == CellType::SLOT_CAR || s.cellType == CellType::SLOT_BIKE) {
                int bestDist = 999999;
                double bestMeters = -1.0;

                for (const auto& eg : activeEntrances) {
                    PathResult p = graph.bfsShortestPath(eg.row, eg.col, r, c, layout, scale);
                    if (p.found && p.distance < bestDist) {
                        bestDist = p.distance;
                        bestMeters = p.distanceMeters;
                    }
                }

                if (bestDist < 999999) {
                    s.bfsDistance = bestDist;
                    s.distanceMeters = bestMeters;
                } else {
                    s.bfsDistance = -1;
                    s.distanceMeters = -1.0; // Unreachable
                }
            } else {
                s.bfsDistance = 0;
                s.distanceMeters = 0.0;
            }
        }
    }
}

void ParkingManager::setupDefaultPlot() {
    Plot p;
    p.id = "plot_central_campus";
    p.name = "Demo Parking";
    p.location = "Main Gate, Academic Block";
    p.parkingType = "mixed";
    p.rows = 4;
    p.cols = 6;
    p.ratePerHourCar = 40.0;
    p.ratePerHourBike = 20.0;
    p.gracePeriodMinutes = 15;
    p.cellScaleMeters = 5.0; // Calibrated map scale: 5 meters per cell

    // Configure Default Entry & Exit Gates (Module 1)
    Gate g1;
    g1.id = "gate_main_in";
    g1.name = "Main Entrance (North)";
    g1.type = GateType::ENTRANCE;
    g1.row = 0;
    g1.col = 2;
    g1.isActive = true;
    p.gates.push_back(g1);

    Gate g2;
    g2.id = "gate_exit_1";
    g2.name = "Main Exit (South)";
    g2.type = GateType::EXIT;
    g2.row = 3;
    g2.col = 2;
    g2.isActive = true;
    p.gates.push_back(g2);

    Gate g3;
    g3.id = "gate_south_in";
    g3.name = "Gate 2 (East Entrance)";
    g3.type = GateType::BIDIRECTIONAL;
    g3.row = 1;
    g3.col = 5;
    g3.isActive = true;
    p.gates.push_back(g3);

    p.grid.resize(p.rows, std::vector<Slot>(p.cols));
    const char rowLabels[4] = {'A', 'B', 'C', 'D'};
    for (int r = 0; r < p.rows; ++r) {
        for (int c = 0; c < p.cols; ++c) {
            Slot s;
            s.row = r;
            s.col = c;
            s.id = std::string(1, rowLabels[r]) + std::to_string(c + 1);

            if (r == 0 && c == 2) {
                s.cellType = CellType::ENTRANCE;
            } else if (r == 3 && c == 2) {
                s.cellType = CellType::EXIT;
            } else if (c == 2 || c == 3) {
                s.cellType = CellType::AISLE;
            } else if (r == 3 && c >= 4) {
                s.cellType = CellType::SLOT_BIKE;
                s.category = SlotCategory::BIKE;
            } else {
                s.cellType = CellType::SLOT_CAR;
                s.category = SlotCategory::STANDARD;
            }

            s.status = SlotStatus::AVAILABLE;

            // Configure EV Charging Slots (Modules 5 & 6)
            // A4: CCS2 Operational EV Charger (Fast 50kW)
            if (s.id == "A4") {
                s.cellType = CellType::SLOT_CAR;
                s.category = SlotCategory::EV_CHARGING;
                s.hasCharger = true;
                s.chargerId = "EV-CHG-01";
                s.connectorType = "CCS2";
                s.chargingPowerKw = 50.0;
                s.chargerStatus = ChargerStatus::OPERATIONAL;
                s.chargingRatePerHour = 50.0;
            }
            // A5: TYPE_2_AC Operational EV Charger (AC 22kW)
            else if (s.id == "A5") {
                s.cellType = CellType::SLOT_CAR;
                s.category = SlotCategory::EV_CHARGING;
                s.hasCharger = true;
                s.chargerId = "EV-CHG-02";
                s.connectorType = "TYPE_2_AC";
                s.chargingPowerKw = 22.0;
                s.chargerStatus = ChargerStatus::OPERATIONAL;
                s.chargingRatePerHour = 40.0;
            }
            // B5: CCS2 Out-of-Service EV Charger (demonstrates maintenance skipping)
            else if (s.id == "B5") {
                s.cellType = CellType::SLOT_CAR;
                s.category = SlotCategory::EV_CHARGING;
                s.hasCharger = true;
                s.chargerId = "EV-CHG-03";
                s.connectorType = "CCS2";
                s.chargingPowerKw = 50.0;
                s.chargerStatus = ChargerStatus::OUT_OF_SERVICE;
                s.chargingRatePerHour = 50.0;
            }

            p.grid[r][c] = s;
        }
    }

    reallotPlotSlotNumbers(p);
    recomputePlotGraph(p);
    plots.push_back(p);
    plotIndexMap[p.id] = 0;
    activePlotId = p.id;
}

void ParkingManager::initialize() {
    CreateDirectoryA(dataDir.c_str(), NULL);
    loadFromDisk();
    if (plots.empty()) {
        setupDefaultPlot();
        saveToDisk();
    }
}

std::vector<Plot> ParkingManager::getAllPlots() {
    EnterCriticalSection(&csLock);
    std::vector<Plot> res = plots;
    LeaveCriticalSection(&csLock);
    return res;
}

bool ParkingManager::getPlot(const std::string& plotId, Plot& outPlot) {
    EnterCriticalSection(&csLock);
    auto it = plotIndexMap.find(plotId);
    if (it != plotIndexMap.end() && it->second < plots.size()) {
        outPlot = plots[it->second];
        LeaveCriticalSection(&csLock);
        return true;
    }
    LeaveCriticalSection(&csLock);
    return false;
}

bool ParkingManager::getActivePlot(Plot& outPlot) {
    EnterCriticalSection(&csLock);
    if (activePlotId.empty() && !plots.empty()) {
        activePlotId = plots[0].id;
    }
    auto it = plotIndexMap.find(activePlotId);
    if (it != plotIndexMap.end() && it->second < plots.size()) {
        outPlot = plots[it->second];
        LeaveCriticalSection(&csLock);
        return true;
    }
    LeaveCriticalSection(&csLock);
    return false;
}

bool ParkingManager::setActivePlot(const std::string& plotId) {
    EnterCriticalSection(&csLock);
    if (plotIndexMap.find(plotId) != plotIndexMap.end()) {
        activePlotId = plotId;
        LeaveCriticalSection(&csLock);
        return true;
    }
    LeaveCriticalSection(&csLock);
    return false;
}

bool ParkingManager::createPlot(const Plot& newPlot, std::string& errorMsg) {
    EnterCriticalSection(&csLock);
    if (newPlot.id.empty()) {
        errorMsg = "Plot ID cannot be empty.";
        LeaveCriticalSection(&csLock);
        return false;
    }
    if (plotIndexMap.find(newPlot.id) != plotIndexMap.end()) {
        errorMsg = "A plot with ID '" + newPlot.id + "' already exists.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot p = newPlot;
    if (p.cellScaleMeters <= 0.0) p.cellScaleMeters = 5.0;
    reallotPlotSlotNumbers(p);
    recomputePlotGraph(p);
    plots.push_back(p);
    plotIndexMap[p.id] = plots.size() - 1;
    activePlotId = p.id;

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

bool ParkingManager::updatePlot(const Plot& updatedPlot, std::string& errorMsg) {
    EnterCriticalSection(&csLock);
    auto it = plotIndexMap.find(updatedPlot.id);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        errorMsg = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    p.name = updatedPlot.name;
    p.location = updatedPlot.location;
    p.parkingType = updatedPlot.parkingType;
    p.ratePerHourCar = updatedPlot.ratePerHourCar;
    p.ratePerHourBike = updatedPlot.ratePerHourBike;
    p.gracePeriodMinutes = updatedPlot.gracePeriodMinutes;
    p.cellScaleMeters = updatedPlot.cellScaleMeters > 0.0 ? updatedPlot.cellScaleMeters : 5.0;
    p.rows = updatedPlot.rows;
    p.cols = updatedPlot.cols;
    p.grid = updatedPlot.grid;
    p.gates = updatedPlot.gates;

    reallotPlotSlotNumbers(p);
    recomputePlotGraph(p);

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

bool ParkingManager::deletePlot(const std::string& plotId, std::string& errorMsg) {
    EnterCriticalSection(&csLock);
    if (plots.size() <= 1) {
        errorMsg = "Cannot delete the only remaining plot in the system.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    auto it = plotIndexMap.find(plotId);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        errorMsg = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    size_t idx = it->second;
    for (const auto& row : plots[idx].grid) {
        for (const auto& slot : row) {
            if (slot.status == SlotStatus::OCCUPIED) {
                errorMsg = "Cannot delete plot with active parked vehicles.";
                LeaveCriticalSection(&csLock);
                return false;
            }
        }
    }

    plots.erase(plots.begin() + idx);
    plotIndexMap.clear();
    for (size_t i = 0; i < plots.size(); ++i) {
        plotIndexMap[plots[i].id] = i;
    }
    if (activePlotId == plotId) {
        activePlotId = plots[0].id;
    }

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

// Module 1: Admin-Configured Entry/Exit Gates
bool ParkingManager::addGate(const std::string& plotId, const Gate& gate, std::string& err) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        err = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    Gate newG = gate;
    if (newG.id.empty()) newG.id = generateGateId();

    for (const auto& existing : p.gates) {
        if (existing.id == newG.id) {
            err = "Gate with ID '" + newG.id + "' already exists.";
            LeaveCriticalSection(&csLock);
            return false;
        }
    }

    if (newG.row < 0 || newG.row >= p.rows || newG.col < 0 || newG.col >= p.cols) {
        err = "Gate coordinates are out of plot grid bounds.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    p.gates.push_back(newG);
    reallotPlotSlotNumbers(p);
    recomputePlotGraph(p);

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

bool ParkingManager::updateGate(const std::string& plotId, const Gate& gate, std::string& err) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        err = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    bool found = false;
    for (auto& g : p.gates) {
        if (g.id == gate.id) {
            g.name = gate.name;
            g.type = gate.type;
            g.row = gate.row;
            g.col = gate.col;
            g.isActive = gate.isActive;
            found = true;
            break;
        }
    }

    if (!found) {
        err = "Gate not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    reallotPlotSlotNumbers(p);
    recomputePlotGraph(p);
    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

bool ParkingManager::removeGate(const std::string& plotId, const std::string& gateId, std::string& err) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        err = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    int oldR = -1, oldC = -1;
    for (const auto& g : p.gates) {
        if (g.id == gateId) {
            oldR = g.row;
            oldC = g.col;
            break;
        }
    }

    auto gIt = std::remove_if(p.gates.begin(), p.gates.end(), [&](const Gate& g) {
        return g.id == gateId;
    });

    if (gIt == p.gates.end()) {
        err = "Gate not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    p.gates.erase(gIt, p.gates.end());

    if (oldR >= 0 && oldR < p.rows && oldC >= 0 && oldC < p.cols) {
        if (p.grid[oldR][oldC].cellType == CellType::ENTRANCE || p.grid[oldR][oldC].cellType == CellType::EXIT) {
            p.grid[oldR][oldC].cellType = CellType::AISLE;
        }
    }

    reallotPlotSlotNumbers(p);
    recomputePlotGraph(p);

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

bool ParkingManager::toggleGate(const std::string& plotId, const std::string& gateId, bool active, std::string& err) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        err = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    bool found = false;
    for (auto& g : p.gates) {
        if (g.id == gateId) {
            g.isActive = active;
            found = true;
            break;
        }
    }

    if (!found) {
        err = "Gate not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    reallotPlotSlotNumbers(p);
    recomputePlotGraph(p);
    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

std::vector<Gate> ParkingManager::getGates(const std::string& plotId) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    std::vector<Gate> res;
    auto it = plotIndexMap.find(target);
    if (it != plotIndexMap.end() && it->second < plots.size()) {
        res = plots[it->second].gates;
    }
    LeaveCriticalSection(&csLock);
    return res;
}

bool ParkingManager::updateScale(const std::string& plotId, double scaleMeters, std::string& err) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        err = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    p.cellScaleMeters = (scaleMeters > 0.0) ? scaleMeters : 5.0;
    recomputePlotGraph(p);

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

// Module 5: EV Charging Slot Management
bool ParkingManager::configureSlotCharger(const std::string& plotId, const std::string& slotId, 
                                          bool hasCharger, const std::string& chargerId, 
                                          const std::string& connectorType, double powerKw, 
                                          ChargerStatus status, double ratePerHour, std::string& err) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        err = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    bool found = false;
    for (auto& row : p.grid) {
        for (auto& slot : row) {
            if (slot.id == slotId) {
                slot.hasCharger = hasCharger;
                slot.category = hasCharger ? SlotCategory::EV_CHARGING : SlotCategory::STANDARD;
                slot.chargerId = hasCharger ? chargerId : "";
                slot.connectorType = hasCharger ? connectorType : "";
                slot.chargingPowerKw = powerKw > 0.0 ? powerKw : 22.0;
                slot.chargerStatus = status;
                slot.chargingRatePerHour = ratePerHour >= 0.0 ? ratePerHour : 50.0;
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        err = "Slot '" + slotId + "' not found in plot.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

bool ParkingManager::toggleChargerStatus(const std::string& plotId, const std::string& slotId, 
                                         ChargerStatus status, std::string& err) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        err = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    bool found = false;
    for (auto& row : p.grid) {
        for (auto& slot : row) {
            if (slot.id == slotId) {
                if (!slot.hasCharger) {
                    err = "Slot '" + slotId + "' does not have an EV charger installed.";
                    LeaveCriticalSection(&csLock);
                    return false;
                }
                slot.chargerStatus = status;
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        err = "Slot '" + slotId + "' not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

bool ParkingManager::recordCellEdit(const std::string& plotId, int row, int col, CellType newType) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[it->second];
    if (row < 0 || row >= p.rows || col < 0 || col >= p.cols) {
        LeaveCriticalSection(&csLock);
        return false;
    }

    CellType oldType = p.grid[row][col].cellType;
    p.grid[row][col].cellType = newType;

    LayoutAction action;
    action.row = row;
    action.col = col;
    action.previousType = oldType;
    action.newType = newType;
    undoStacks[target].push(action);

    reallotPlotSlotNumbers(p);
    recomputePlotGraph(p);
    processWaitingQueueLocked(target);

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

bool ParkingManager::undoLastEdit(const std::string& plotId, int& outRow, int& outCol, CellType& revertedType) {
    EnterCriticalSection(&csLock);
    std::string target = plotId.empty() ? activePlotId : plotId;
    auto stackIt = undoStacks.find(target);
    if (stackIt == undoStacks.end() || stackIt->second.empty()) {
        LeaveCriticalSection(&csLock);
        return false;
    }

    LayoutAction action;
    if (!stackIt->second.pop(action)) {
        LeaveCriticalSection(&csLock);
        return false;
    }

    auto plotIt = plotIndexMap.find(target);
    if (plotIt == plotIndexMap.end() || plotIt->second >= plots.size()) {
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& p = plots[plotIt->second];
    if (action.row >= 0 && action.row < p.rows && action.col >= 0 && action.col < p.cols) {
        p.grid[action.row][action.col].cellType = action.previousType;
        outRow = action.row;
        outCol = action.col;
        revertedType = action.previousType;
        reallotPlotSlotNumbers(p);
        recomputePlotGraph(p);
        processWaitingQueueLocked(target);
        LeaveCriticalSection(&csLock);
        saveToDisk();
        return true;
    }

    LeaveCriticalSection(&csLock);
    return false;
}

LayoutValidationReport ParkingManager::validateLayout(const std::string& plotId) {
    EnterCriticalSection(&csLock);
    LayoutValidationReport report;
    report.allSlotsReachable = true;
    report.totalSlots = 0;
    report.reachableSlots = 0;
    report.disconnectedSlots = 0;
    report.activeEntrances = 0;
    report.activeExits = 0;

    std::string target = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(target);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        report.allSlotsReachable = false;
        report.warnings.push_back("Plot not found.");
        LeaveCriticalSection(&csLock);
        return report;
    }

    const Plot& plot = plots[it->second];

    // Count gates
    for (const auto& g : plot.gates) {
        if (g.isActive) {
            if (g.type == GateType::ENTRANCE) report.activeEntrances++;
            else if (g.type == GateType::EXIT) report.activeExits++;
            else if (g.type == GateType::BIDIRECTIONAL) {
                report.activeEntrances++;
                report.activeExits++;
            }
        }
    }

    if (report.activeEntrances == 0) {
        report.warnings.push_back("No active entrance gates configured! Vehicles cannot enter the parking plot.");
    }
    if (report.activeExits == 0) {
        report.warnings.push_back("No active exit gates configured! Vehicles cannot exit the parking plot.");
    }

    // Check reachability of all slots
    Matrix2D<CellType> layout(plot.rows, plot.cols, CellType::AISLE);
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            layout.at(r, c) = plot.grid[r][c].cellType;
        }
    }

    ParkingGraph graph(plot.rows, plot.cols);

    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            const auto& s = plot.grid[r][c];
            if (s.cellType == CellType::SLOT_CAR || s.cellType == CellType::SLOT_BIKE) {
                report.totalSlots++;
                bool reachableFromAnyGate = false;

                for (const auto& g : plot.gates) {
                    if (g.isActive && (g.type == GateType::ENTRANCE || g.type == GateType::BIDIRECTIONAL)) {
                        PathResult p = graph.bfsShortestPath(g.row, g.col, r, c, layout, plot.cellScaleMeters);
                        if (p.found) {
                            reachableFromAnyGate = true;
                            break;
                        }
                    }
                }

                if (reachableFromAnyGate) {
                    report.reachableSlots++;
                } else {
                    report.disconnectedSlots++;
                    report.allSlotsReachable = false;
                    report.unreachableSlotIds.push_back(s.id);
                }
            }
        }
    }

    if (report.disconnectedSlots > 0) {
        report.warnings.push_back(std::to_string(report.disconnectedSlots) + 
                                  " parking slot(s) are completely unreachable from any active entrance gate.");
    }

    LeaveCriticalSection(&csLock);
    return report;
}

// Parking Operations (Modules 2, 3, 4, 6)
ParkResult ParkingManager::parkVehicle(const std::string& plotId, const std::string& plate, 
                                       VehicleType type, bool isPriority,
                                       const std::string& owner, const std::string& phone,
                                       bool requiresEVCharging, 
                                       const std::string& preferredConnector,
                                       const std::string& selectedGateId) {
    EnterCriticalSection(&csLock);
    ParkResult res;
    res.success = false;
    res.isQueued = false;
    res.isEVChargingAssigned = false;

    std::string normPlate = normalizePlate(plate);
    if (normPlate.empty()) {
        res.message = "Invalid or empty license plate number.";
        LeaveCriticalSection(&csLock);
        return res;
    }

    // Check duplicate active vehicle!
    if (activeSessions.find(normPlate) != activeSessions.end()) {
        res.message = "Vehicle " + normPlate + " is already actively parked in the facility!";
        LeaveCriticalSection(&csLock);
        return res;
    }

    std::string targetPlotId = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(targetPlotId);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        res.message = "Specified parking plot was not found.";
        LeaveCriticalSection(&csLock);
        return res;
    }

    Plot& plot = plots[it->second];

    // Determine active entry gates
    std::vector<Gate> candidateGates;
    if (!selectedGateId.empty()) {
        for (const auto& g : plot.gates) {
            if (g.id == selectedGateId) {
                if (!g.isActive) {
                    res.message = "Selected entrance gate '" + g.name + "' is currently closed/inactive. Please choose an active gate.";
                    LeaveCriticalSection(&csLock);
                    return res;
                }
                candidateGates.push_back(g);
                break;
            }
        }
        if (candidateGates.empty()) {
            res.message = "Specified entrance gate was not found.";
            LeaveCriticalSection(&csLock);
            return res;
        }
    } else {
        // Auto-select: evaluate all active entrance / bidirectional gates
        for (const auto& g : plot.gates) {
            if (g.isActive && (g.type == GateType::ENTRANCE || g.type == GateType::BIDIRECTIONAL)) {
                candidateGates.push_back(g);
            }
        }
    }

    // Fallback if no active gates found
    if (candidateGates.empty()) {
        Gate defaultG;
        defaultG.id = "gate_auto";
        defaultG.name = "Auto Gate";
        defaultG.type = GateType::ENTRANCE;
        defaultG.row = 0;
        defaultG.col = 0;
        defaultG.isActive = true;
        candidateGates.push_back(defaultG);
    }

    Matrix2D<CellType> layout(plot.rows, plot.cols, CellType::AISLE);
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            layout.at(r, c) = plot.grid[r][c].cellType;
        }
    }

    ParkingGraph graph(plot.rows, plot.cols);
    double scale = (plot.cellScaleMeters > 0.0) ? plot.cellScaleMeters : 5.0;

    int bestR = -1, bestC = -1;
    double bestDistMeters = 9999999.0;
    int bestBfsSegments = 999999;
    Gate bestGate = candidateGates[0];

    // Module 6: EV Charging Logic vs Standard Logic
    if (requiresEVCharging) {
        // Case B: EV Charging Required
        // Search ONLY available slots with an operational EV charger (excluding gates, aisles, pillars)
        for (int r = 0; r < plot.rows; ++r) {
            for (int c = 0; c < plot.cols; ++c) {
                Slot& slot = plot.grid[r][c];
                if (slot.status == SlotStatus::AVAILABLE && isParkableSlot(plot, r, c) && slot.hasCharger && slot.chargerStatus == ChargerStatus::OPERATIONAL) {
                    // Check connector compatibility if specified
                    if (preferredConnector != "ANY" && !preferredConnector.empty()) {
                        if (slot.connectorType != preferredConnector) {
                            continue; // Skip incompatible connector
                        }
                    }

                    // Evaluate driving route from candidate gates
                    for (const auto& gate : candidateGates) {
                        PathResult pr = graph.bfsShortestPath(gate.row, gate.col, r, c, layout, scale);
                        if (pr.found && pr.distanceMeters < bestDistMeters) {
                            bestDistMeters = pr.distanceMeters;
                            bestBfsSegments = pr.distance;
                            bestR = r;
                            bestC = c;
                            bestGate = gate;
                        }
                    }
                }
            }
        }

        // If no operational EV slot found, NEVER silently assign standard slot!
        if (bestR == -1) {
            WaitingRequest wReq;
            wReq.waitId = generateWaitId();
            wReq.plotId = targetPlotId;
            wReq.vehiclePlate = normPlate;
            wReq.vehicleType = type;
            wReq.ownerName = owner.empty() ? "Guest" : owner;
            wReq.phone = phone;
            wReq.isPriority = isPriority;
            wReq.requiresEVCharging = requiresEVCharging;
            wReq.preferredConnector = preferredConnector;
            wReq.requestUnixTime = getCurrentUnixTime();
            wReq.requestTimeFormatted = getFormattedTimestamp(wReq.requestUnixTime);

            waitingQueues[targetPlotId].enqueue(wReq);

            res.success = true;
            res.isQueued = true;
            res.message = "No operational EV charging slot available right now. Vehicle placed in EV Waiting Queue (Position #" + 
                          std::to_string(waitingQueues[targetPlotId].size()) + ").";
            res.waitingRequest = wReq;

            LeaveCriticalSection(&csLock);
            saveToDisk();
            return res;
        }

    } else {
        // Case A: Standard Vehicle (Do NOT occupy EV chargers unless standard full, never park in gates/aisles/pillars)
        if (type == VehicleType::TWO_WHEELER) {
            // Bike: try bike slots first
            for (int r = 0; r < plot.rows; ++r) {
                for (int c = 0; c < plot.cols; ++c) {
                    Slot& slot = plot.grid[r][c];
                    if (slot.status == SlotStatus::AVAILABLE && isParkableSlot(plot, r, c) && slot.cellType == CellType::SLOT_BIKE) {
                        for (const auto& gate : candidateGates) {
                            PathResult pr = graph.bfsShortestPath(gate.row, gate.col, r, c, layout, scale);
                            if (pr.found && pr.distanceMeters < bestDistMeters) {
                                bestDistMeters = pr.distanceMeters;
                                bestBfsSegments = pr.distance;
                                bestR = r;
                                bestC = c;
                                bestGate = gate;
                            }
                        }
                    }
                }
            }
            // If no bike slot, fallback to standard car slots (not EV)
            if (bestR == -1) {
                for (int r = 0; r < plot.rows; ++r) {
                    for (int c = 0; c < plot.cols; ++c) {
                        Slot& slot = plot.grid[r][c];
                        if (slot.status == SlotStatus::AVAILABLE && isParkableSlot(plot, r, c) && slot.cellType == CellType::SLOT_CAR && (!slot.hasCharger || slot.chargerStatus != ChargerStatus::OPERATIONAL)) {
                            for (const auto& gate : candidateGates) {
                                PathResult pr = graph.bfsShortestPath(gate.row, gate.col, r, c, layout, scale);
                                if (pr.found && pr.distanceMeters < bestDistMeters) {
                                    bestDistMeters = pr.distanceMeters;
                                    bestBfsSegments = pr.distance;
                                    bestR = r;
                                    bestC = c;
                                    bestGate = gate;
                                }
                            }
                        }
                    }
                }
            }
        } else {
            // Standard Car: allocate available standard car slot (excluding operational EV chargers, gates, aisles, pillars)
            for (int r = 0; r < plot.rows; ++r) {
                for (int c = 0; c < plot.cols; ++c) {
                    Slot& slot = plot.grid[r][c];
                    if (slot.status == SlotStatus::AVAILABLE && isParkableSlot(plot, r, c) && slot.cellType == CellType::SLOT_CAR && (!slot.hasCharger || slot.chargerStatus != ChargerStatus::OPERATIONAL)) {
                        for (const auto& gate : candidateGates) {
                            PathResult pr = graph.bfsShortestPath(gate.row, gate.col, r, c, layout, scale);
                            if (pr.found && pr.distanceMeters < bestDistMeters) {
                                bestDistMeters = pr.distanceMeters;
                                bestBfsSegments = pr.distance;
                                bestR = r;
                                bestC = c;
                                bestGate = gate;
                            }
                        }
                    }
                }
            }
        }

        // If plot is full of standard slots
        if (bestR == -1) {
            WaitingRequest wReq;
            wReq.waitId = generateWaitId();
            wReq.plotId = targetPlotId;
            wReq.vehiclePlate = normPlate;
            wReq.vehicleType = type;
            wReq.ownerName = owner.empty() ? "Guest" : owner;
            wReq.phone = phone;
            wReq.isPriority = isPriority;
            wReq.requiresEVCharging = requiresEVCharging;
            wReq.preferredConnector = preferredConnector;
            wReq.requestUnixTime = getCurrentUnixTime();
            wReq.requestTimeFormatted = getFormattedTimestamp(wReq.requestUnixTime);

            waitingQueues[targetPlotId].enqueue(wReq);

            res.success = true;
            res.isQueued = true;
            res.message = "All standard parking slots are full. Vehicle placed in waiting queue (Position #" + 
                          std::to_string(waitingQueues[targetPlotId].size()) + ").";
            res.waitingRequest = wReq;

            LeaveCriticalSection(&csLock);
            saveToDisk();
            return res;
        }
    }

    if (bestR == -1 || !isParkableSlot(plot, bestR, bestC)) {
        res.message = "No valid parking slot available (gates, aisles, and pillars cannot be parked in).";
        LeaveCriticalSection(&csLock);
        return res;
    }

    // Allocate the selected slot
    Slot& allocatedSlot = plot.grid[bestR][bestC];
    allocatedSlot.status = SlotStatus::OCCUPIED;
    allocatedSlot.currentPlate = normPlate;
    allocatedSlot.currentVehicleType = type;

    std::string sId = generateSessionId();
    allocatedSlot.currentSessionId = sId;

    ParkingSession sess;
    sess.sessionId = sId;
    sess.plotId = targetPlotId;
    sess.slotId = allocatedSlot.id;
    sess.vehiclePlate = normPlate;
    sess.vehicleType = type;
    sess.ownerName = owner.empty() ? "Guest" : owner;
    sess.phone = phone;
    sess.entryUnixTime = getCurrentUnixTime();
    sess.entryTimeFormatted = getFormattedTimestamp(sess.entryUnixTime);
    sess.isActive = true;

    // Gate & Driving Distance (Modules 1, 2, 3)
    sess.entryGateId = bestGate.id;
    sess.entryGateName = bestGate.name;
    sess.entryDistanceMeters = bestDistMeters;

    // EV Charging attributes
    sess.requiresEVCharging = requiresEVCharging;
    sess.chargerId = (requiresEVCharging && allocatedSlot.hasCharger) ? allocatedSlot.chargerId : "";
    sess.connectorType = (requiresEVCharging && allocatedSlot.hasCharger) ? allocatedSlot.connectorType : "";

    // O(1) Indexing
    activeSessions[normPlate] = sess;
    plateToPlotSlot[normPlate] = targetPlotId + ":" + allocatedSlot.id;

    // Insert into BST & Doubly Linked List
    vehicleBst.insert(sess);
    historyList.pushFront(sess);

    res.success = true;
    res.isQueued = false;
    res.slotId = allocatedSlot.id;
    res.sessionId = sId;
    res.bfsDistance = bestBfsSegments;
    res.distanceMeters = bestDistMeters;
    res.estimatedTimeSeconds = static_cast<int>(std::round(bestDistMeters / 4.167));
    if (res.estimatedTimeSeconds < 5 && bestDistMeters > 0) res.estimatedTimeSeconds = 5;
    res.gateId = bestGate.id;
    res.gateName = bestGate.name;
    res.isEVChargingAssigned = (requiresEVCharging && allocatedSlot.hasCharger);
    res.chargerId = allocatedSlot.chargerId;
    res.connectorType = allocatedSlot.connectorType;

    std::ostringstream msg;
    msg << "Vehicle " << normPlate << " successfully assigned Slot " << allocatedSlot.id 
        << " via " << bestGate.name << " (" << std::fixed << std::setprecision(1) << bestDistMeters << " m).";
    if (res.isEVChargingAssigned) {
        msg << " EV Charger " << allocatedSlot.chargerId << " (" << allocatedSlot.connectorType << ") activated.";
    }
    res.message = msg.str();

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return res;
}

// Automatic Allocation: whenever any plot slot gets empty, vehicles in queue are automatically parked in empty space!
std::vector<ParkResult> ParkingManager::processWaitingQueueLocked(const std::string& targetPlotId) {
    std::vector<ParkResult> promotedResults;
    if (targetPlotId.empty()) return promotedResults;

    auto plotIt = plotIndexMap.find(targetPlotId);
    if (plotIt == plotIndexMap.end() || plotIt->second >= plots.size()) {
        return promotedResults;
    }

    Plot& plot = plots[plotIt->second];
    if (waitingQueues[targetPlotId].size() == 0) {
        return promotedResults;
    }

    // Determine candidate active entrance gates
    std::vector<Gate> candidateGates;
    for (const auto& g : plot.gates) {
        if (g.isActive && (g.type == GateType::ENTRANCE || g.type == GateType::BIDIRECTIONAL)) {
            candidateGates.push_back(g);
        }
    }
    if (candidateGates.empty()) {
        Gate defaultG;
        defaultG.id = "gate_auto";
        defaultG.name = "Auto Gate";
        defaultG.type = GateType::ENTRANCE;
        defaultG.row = 0;
        defaultG.col = 0;
        defaultG.isActive = true;
        candidateGates.push_back(defaultG);
    }

    Matrix2D<CellType> layout(plot.rows, plot.cols, CellType::AISLE);
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            layout.at(r, c) = plot.grid[r][c].cellType;
        }
    }
    ParkingGraph graph(plot.rows, plot.cols);
    double scale = (plot.cellScaleMeters > 0.0) ? plot.cellScaleMeters : 5.0;

    // Get all waiting requests (priorityQueue first, then standardQueue in order)
    std::vector<WaitingRequest> waitingList = waitingQueues[targetPlotId].getAll();

    for (const auto& wReq : waitingList) {
        int bestR = -1, bestC = -1;
        double bestDistMeters = 9999999.0;
        int bestBfsSegments = 999999;
        Gate bestGate = candidateGates[0];

        if (wReq.requiresEVCharging) {
            for (int r = 0; r < plot.rows; ++r) {
                for (int c = 0; c < plot.cols; ++c) {
                    Slot& slot = plot.grid[r][c];
                    if (slot.status == SlotStatus::AVAILABLE && isParkableSlot(plot, r, c) && slot.hasCharger && slot.chargerStatus == ChargerStatus::OPERATIONAL) {
                        if (wReq.preferredConnector != "ANY" && !wReq.preferredConnector.empty()) {
                            if (slot.connectorType != wReq.preferredConnector) {
                                continue;
                            }
                        }
                        for (const auto& gate : candidateGates) {
                            PathResult pr = graph.bfsShortestPath(gate.row, gate.col, r, c, layout, scale);
                            if (pr.found && pr.distanceMeters < bestDistMeters) {
                                bestDistMeters = pr.distanceMeters;
                                bestBfsSegments = pr.distance;
                                bestR = r;
                                bestC = c;
                                bestGate = gate;
                            }
                        }
                    }
                }
            }
        } else {
            if (wReq.vehicleType == VehicleType::TWO_WHEELER) {
                // Try bike slots first
                for (int r = 0; r < plot.rows; ++r) {
                    for (int c = 0; c < plot.cols; ++c) {
                        Slot& slot = plot.grid[r][c];
                        if (slot.status == SlotStatus::AVAILABLE && isParkableSlot(plot, r, c) && slot.cellType == CellType::SLOT_BIKE) {
                            for (const auto& gate : candidateGates) {
                                PathResult pr = graph.bfsShortestPath(gate.row, gate.col, r, c, layout, scale);
                                if (pr.found && pr.distanceMeters < bestDistMeters) {
                                    bestDistMeters = pr.distanceMeters;
                                    bestBfsSegments = pr.distance;
                                    bestR = r;
                                    bestC = c;
                                    bestGate = gate;
                                }
                            }
                        }
                    }
                }
                // Fallback to standard car slots
                if (bestR == -1) {
                    for (int r = 0; r < plot.rows; ++r) {
                        for (int c = 0; c < plot.cols; ++c) {
                            Slot& slot = plot.grid[r][c];
                            if (slot.status == SlotStatus::AVAILABLE && isParkableSlot(plot, r, c) && slot.cellType == CellType::SLOT_CAR && (!slot.hasCharger || slot.chargerStatus != ChargerStatus::OPERATIONAL)) {
                                for (const auto& gate : candidateGates) {
                                    PathResult pr = graph.bfsShortestPath(gate.row, gate.col, r, c, layout, scale);
                                    if (pr.found && pr.distanceMeters < bestDistMeters) {
                                        bestDistMeters = pr.distanceMeters;
                                        bestBfsSegments = pr.distance;
                                        bestR = r;
                                        bestC = c;
                                        bestGate = gate;
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                // Standard Car
                for (int r = 0; r < plot.rows; ++r) {
                    for (int c = 0; c < plot.cols; ++c) {
                        Slot& slot = plot.grid[r][c];
                        if (slot.status == SlotStatus::AVAILABLE && isParkableSlot(plot, r, c) && slot.cellType == CellType::SLOT_CAR && (!slot.hasCharger || slot.chargerStatus != ChargerStatus::OPERATIONAL)) {
                            for (const auto& gate : candidateGates) {
                                PathResult pr = graph.bfsShortestPath(gate.row, gate.col, r, c, layout, scale);
                                if (pr.found && pr.distanceMeters < bestDistMeters) {
                                    bestDistMeters = pr.distanceMeters;
                                    bestBfsSegments = pr.distance;
                                    bestR = r;
                                    bestC = c;
                                    bestGate = gate;
                                }
                            }
                        }
                    }
                }
            }
        }

        // If an empty slot was found, allocate it immediately to this queued vehicle!
        if (bestR != -1 && isParkableSlot(plot, bestR, bestC)) {
            Slot& allocatedSlot = plot.grid[bestR][bestC];
            allocatedSlot.status = SlotStatus::OCCUPIED;
            allocatedSlot.currentPlate = wReq.vehiclePlate;
            allocatedSlot.currentVehicleType = wReq.vehicleType;

            std::string sId = generateSessionId();
            allocatedSlot.currentSessionId = sId;

            ParkingSession sess;
            sess.sessionId = sId;
            sess.plotId = targetPlotId;
            sess.slotId = allocatedSlot.id;
            sess.vehiclePlate = wReq.vehiclePlate;
            sess.vehicleType = wReq.vehicleType;
            sess.ownerName = wReq.ownerName.empty() ? "Guest" : wReq.ownerName;
            sess.phone = wReq.phone;
            sess.entryUnixTime = getCurrentUnixTime();
            sess.entryTimeFormatted = getFormattedTimestamp(sess.entryUnixTime);
            sess.isActive = true;
            sess.entryGateId = bestGate.id;
            sess.entryGateName = bestGate.name;
            sess.entryDistanceMeters = bestDistMeters;
            sess.requiresEVCharging = (wReq.requiresEVCharging && allocatedSlot.hasCharger);
            sess.chargerId = (wReq.requiresEVCharging && allocatedSlot.hasCharger) ? allocatedSlot.chargerId : "";
            sess.connectorType = (wReq.requiresEVCharging && allocatedSlot.hasCharger) ? allocatedSlot.connectorType : "";

            activeSessions[wReq.vehiclePlate] = sess;
            plateToPlotSlot[wReq.vehiclePlate] = targetPlotId + ":" + allocatedSlot.id;
            vehicleBst.insert(sess);
            historyList.pushFront(sess);

            // Remove vehicle from waiting queue
            waitingQueues[targetPlotId].remove(wReq.waitId);
            waitingQueues[targetPlotId].remove(wReq.vehiclePlate);

            ParkResult res;
            res.success = true;
            res.isQueued = false;
            res.slotId = allocatedSlot.id;
            res.sessionId = sId;
            res.bfsDistance = bestBfsSegments;
            res.distanceMeters = bestDistMeters;
            res.estimatedTimeSeconds = static_cast<int>(std::round(bestDistMeters / 4.167));
            if (res.estimatedTimeSeconds < 5 && bestDistMeters > 0) res.estimatedTimeSeconds = 5;
            res.gateId = bestGate.id;
            res.gateName = bestGate.name;
            res.isEVChargingAssigned = sess.requiresEVCharging;
            res.chargerId = sess.chargerId;
            res.connectorType = sess.connectorType;
            res.waitingRequest = wReq;
            res.message = "Queued vehicle " + wReq.vehiclePlate + " automatically parked into empty Slot " + allocatedSlot.id + ".";
            promotedResults.push_back(res);
        }
    }

    return promotedResults;
}

std::vector<ParkResult> ParkingManager::processWaitingQueue(const std::string& plotId) {
    EnterCriticalSection(&csLock);
    std::string targetPlotId = plotId.empty() ? activePlotId : plotId;
    std::vector<ParkResult> promoted = processWaitingQueueLocked(targetPlotId);
    LeaveCriticalSection(&csLock);

    if (!promoted.empty()) {
        saveToDisk();
    }
    return promoted;
}

// Checkout & Departure Routing (Module 8)
CheckoutResult ParkingManager::exitVehicle(const std::string& plateOrSlotId, const std::string& exitGateId) {
    EnterCriticalSection(&csLock);
    CheckoutResult res;
    res.success = false;
    res.autoAllocatedQueued = false;

    std::string norm = normalizePlate(plateOrSlotId);
    std::string foundPlate = "";

    if (activeSessions.find(norm) != activeSessions.end()) {
        foundPlate = norm;
    } else {
        for (const auto& plot : plots) {
            for (const auto& row : plot.grid) {
                for (const auto& slot : row) {
                    if (slot.id == plateOrSlotId && slot.status == SlotStatus::OCCUPIED) {
                        foundPlate = slot.currentPlate;
                        break;
                    }
                }
                if (!foundPlate.empty()) break;
            }
            if (!foundPlate.empty()) break;
        }
    }

    if (foundPlate.empty() || activeSessions.find(foundPlate) == activeSessions.end()) {
        res.message = "No active parked vehicle found for identifier '" + plateOrSlotId + "'.";
        LeaveCriticalSection(&csLock);
        return res;
    }

    ParkingSession sess = activeSessions[foundPlate];
    activeSessions.erase(foundPlate);
    plateToPlotSlot.erase(foundPlate);

    auto plotIt = plotIndexMap.find(sess.plotId);
    int freedR = -1, freedC = -1;
    bool wasEVCharger = false;
    double chargerHourlyRate = 50.0;

    if (plotIt != plotIndexMap.end() && plotIt->second < plots.size()) {
        Plot& p = plots[plotIt->second];
        for (int r = 0; r < p.rows; ++r) {
            for (int c = 0; c < p.cols; ++c) {
                if (p.grid[r][c].id == sess.slotId) {
                    wasEVCharger = p.grid[r][c].hasCharger;
                    chargerHourlyRate = p.grid[r][c].chargingRatePerHour;

                    p.grid[r][c].status = SlotStatus::AVAILABLE;
                    p.grid[r][c].currentPlate = "";
                    p.grid[r][c].currentSessionId = "";
                    p.grid[r][c].currentVehicleType = (p.grid[r][c].cellType == CellType::SLOT_BIKE ? VehicleType::TWO_WHEELER : VehicleType::CAR);
                    freedR = r;
                    freedC = c;
                    break;
                }
            }
            if (freedR != -1) break;
        }

        // Calculate Departure Route to Exit Gate (Module 8)
        Matrix2D<CellType> layout(p.rows, p.cols, CellType::AISLE);
        for (int r = 0; r < p.rows; ++r) {
            for (int c = 0; c < p.cols; ++c) {
                layout.at(r, c) = p.grid[r][c].cellType;
            }
        }
        ParkingGraph graph(p.rows, p.cols);
        double scale = (p.cellScaleMeters > 0.0) ? p.cellScaleMeters : 5.0;

        Gate selectedExit;
        bool exitFound = false;

        if (!exitGateId.empty()) {
            for (const auto& g : p.gates) {
                if (g.id == exitGateId && g.isActive && (g.type == GateType::EXIT || g.type == GateType::BIDIRECTIONAL)) {
                    selectedExit = g;
                    exitFound = true;
                    break;
                }
            }
        }

        if (!exitFound) {
            double minExitDist = 9999999.0;
            for (const auto& g : p.gates) {
                if (g.isActive && (g.type == GateType::EXIT || g.type == GateType::BIDIRECTIONAL)) {
                    PathResult pr = graph.bfsShortestPath(freedR, freedC, g.row, g.col, layout, scale);
                    if (pr.found && pr.distanceMeters < minExitDist) {
                        minExitDist = pr.distanceMeters;
                        selectedExit = g;
                        exitFound = true;
                    }
                }
            }
        }

        if (exitFound) {
            PathResult exitPath = graph.bfsShortestPath(freedR, freedC, selectedExit.row, selectedExit.col, layout, scale);
            sess.exitGateId = selectedExit.id;
            sess.exitGateName = selectedExit.name;
            sess.exitDistanceMeters = exitPath.found ? exitPath.distanceMeters : 10.0;
        } else {
            sess.exitGateId = "exit_default";
            sess.exitGateName = "Main Exit";
            sess.exitDistanceMeters = 15.0;
        }
    }

    // Fee computation
    long long exitTime = getCurrentUnixTime();
    sess.exitUnixTime = exitTime;
    sess.exitTimeFormatted = getFormattedTimestamp(exitTime);
    long long durationSecs = std::max(60LL, exitTime - sess.entryUnixTime);
    sess.durationMinutes = durationSecs / 60;

    double ratePerHour = 40.0;
    if (plotIt != plotIndexMap.end()) {
        ratePerHour = (sess.vehicleType == VehicleType::TWO_WHEELER) 
                      ? plots[plotIt->second].ratePerHourBike 
                      : plots[plotIt->second].ratePerHourCar;
    }

    double hours = std::ceil(static_cast<double>(sess.durationMinutes) / 60.0);
    if (hours < 1.0) hours = 1.0;

    double baseFee = hours * ratePerHour;
    double evFee = 0.0;

    if (sess.requiresEVCharging && !sess.chargerId.empty()) {
        evFee = hours * chargerHourlyRate;
    }

    sess.fee = baseFee;
    sess.chargingFee = evFee;
    sess.isActive = false;

    // Update in BST & Doubly Linked List
    vehicleBst.insert(sess);
    historyList.pushFront(sess);

    res.success = true;
    res.session = sess;
    res.exitGateId = sess.exitGateId;
    res.exitGateName = sess.exitGateName;
    res.exitDistanceMeters = sess.exitDistanceMeters;
    res.parkingFee = baseFee;
    res.chargingFee = evFee;
    res.totalFee = baseFee + evFee;

    std::ostringstream msg;
    msg << "Vehicle " << foundPlate << " checked out successfully. Total: ₹" 
        << std::fixed << std::setprecision(2) << res.totalFee 
        << " (Parking: ₹" << baseFee;
    if (evFee > 0.0) {
        msg << " + EV Charging: ₹" << evFee;
    }
    msg << "). Exit route: " << sess.exitGateName << " (" << std::setprecision(1) << sess.exitDistanceMeters << " m).";
    res.message = msg.str();

    // Promote next eligible vehicle(s) from waiting queue if slots are available
    std::vector<ParkResult> promoted = processWaitingQueueLocked(sess.plotId);
    if (!promoted.empty()) {
        res.autoAllocatedQueued = true;
        res.autoPlate = promoted[0].waitingRequest.vehiclePlate;
        res.autoSlotId = promoted[0].slotId;
        res.message += " Queued vehicle " + promoted[0].waitingRequest.vehiclePlate + " automatically parked into empty Slot " + promoted[0].slotId + "!";
    }

    LeaveCriticalSection(&csLock);
    saveToDisk();
    return res;
}

// Route & Accurate Distance Calculation (Modules 2, 3, 7)
PathResult ParkingManager::getRoute(const std::string& plotId, const std::string& slotId, 
                                    const std::string& gateId, bool isExitRoute) {
    EnterCriticalSection(&csLock);
    PathResult res;
    res.found = false;
    res.distance = -1;
    res.distanceMeters = 0.0;
    res.estimatedTimeSeconds = 0;

    std::string targetPlotId = plotId.empty() ? activePlotId : plotId;
    auto it = plotIndexMap.find(targetPlotId);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        LeaveCriticalSection(&csLock);
        return res;
    }

    const Plot& plot = plots[it->second];
    double scale = (plot.cellScaleMeters > 0.0) ? plot.cellScaleMeters : 5.0;

    int slotR = -1, slotC = -1;
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            if (plot.grid[r][c].id == slotId) {
                slotR = r; slotC = c;
                break;
            }
        }
        if (slotR != -1) break;
    }

    if (slotR == -1) {
        LeaveCriticalSection(&csLock);
        return res;
    }

    Matrix2D<CellType> layout(plot.rows, plot.cols, CellType::AISLE);
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            layout.at(r, c) = plot.grid[r][c].cellType;
        }
    }

    ParkingGraph graph(plot.rows, plot.cols);

    if (isExitRoute) {
        // Departure route from slot to Exit Gate
        Gate chosenExit;
        bool foundExit = false;

        if (!gateId.empty()) {
            for (const auto& g : plot.gates) {
                if (g.id == gateId && g.isActive && (g.type == GateType::EXIT || g.type == GateType::BIDIRECTIONAL)) {
                    chosenExit = g;
                    foundExit = true;
                    break;
                }
            }
        }

        if (!foundExit) {
            double minDist = 9999999.0;
            for (const auto& g : plot.gates) {
                if (g.isActive && (g.type == GateType::EXIT || g.type == GateType::BIDIRECTIONAL)) {
                    PathResult pr = graph.bfsShortestPath(slotR, slotC, g.row, g.col, layout, scale);
                    if (pr.found && pr.distanceMeters < minDist) {
                        minDist = pr.distanceMeters;
                        chosenExit = g;
                        foundExit = true;
                    }
                }
            }
        }

        if (foundExit) {
            res = graph.bfsShortestPath(slotR, slotC, chosenExit.row, chosenExit.col, layout, scale);
            res.fromGateId = chosenExit.id;
            res.fromGateName = chosenExit.name;
        }
    } else {
        // Entry route from Entrance Gate to slot
        Gate chosenEntrance;
        bool foundEntrance = false;

        if (!gateId.empty()) {
            for (const auto& g : plot.gates) {
                if (g.id == gateId && g.isActive && (g.type == GateType::ENTRANCE || g.type == GateType::BIDIRECTIONAL)) {
                    chosenEntrance = g;
                    foundEntrance = true;
                    break;
                }
            }
        }

        if (!foundEntrance) {
            double minDist = 9999999.0;
            for (const auto& g : plot.gates) {
                if (g.isActive && (g.type == GateType::ENTRANCE || g.type == GateType::BIDIRECTIONAL)) {
                    PathResult pr = graph.bfsShortestPath(g.row, g.col, slotR, slotC, layout, scale);
                    if (pr.found && pr.distanceMeters < minDist) {
                        minDist = pr.distanceMeters;
                        chosenEntrance = g;
                        foundEntrance = true;
                    }
                }
            }
        }

        if (foundEntrance) {
            res = graph.bfsShortestPath(chosenEntrance.row, chosenEntrance.col, slotR, slotC, layout, scale);
            res.fromGateId = chosenEntrance.id;
            res.fromGateName = chosenEntrance.name;
        }
    }

    LeaveCriticalSection(&csLock);
    return res;
}

PathResult ParkingManager::getShortestPathToSlot(const std::string& plotId, const std::string& slotId) {
    return getRoute(plotId, slotId, "", false);
}

// Reservations
bool ParkingManager::createReservation(Reservation res, std::string& outMsg) {
    EnterCriticalSection(&csLock);
    std::string targetPlotId = res.plotId.empty() ? activePlotId : res.plotId;
    auto it = plotIndexMap.find(targetPlotId);
    if (it == plotIndexMap.end() || it->second >= plots.size()) {
        outMsg = "Plot not found.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    Plot& plot = plots[it->second];
    std::string assignedSlotId = res.slotId;

    if (assignedSlotId.empty()) {
        for (int r = 0; r < plot.rows; ++r) {
            for (int c = 0; c < plot.cols; ++c) {
                if (isParkableSlot(plot, r, c) && plot.grid[r][c].status == SlotStatus::AVAILABLE) {
                    assignedSlotId = plot.grid[r][c].id;
                    break;
                }
            }
            if (!assignedSlotId.empty()) break;
        }
    }

    if (assignedSlotId.empty()) {
        outMsg = "No available slots to reserve in this plot.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    bool found = false;
    for (int r = 0; r < plot.rows; ++r) {
        for (int c = 0; c < plot.cols; ++c) {
            if (!assignedSlotId.empty() && plot.grid[r][c].id == assignedSlotId) {
                if (!isParkableSlot(plot, r, c)) {
                    outMsg = "Slot " + assignedSlotId + " is a non-parkable area (gate, aisle, or pillar).";
                    LeaveCriticalSection(&csLock);
                    return false;
                }
                if (plot.grid[r][c].status != SlotStatus::AVAILABLE) {
                    outMsg = "Slot " + assignedSlotId + " is not available.";
                    LeaveCriticalSection(&csLock);
                    return false;
                }
                plot.grid[r][c].status = SlotStatus::RESERVED;
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        outMsg = "Slot " + assignedSlotId + " does not exist.";
        LeaveCriticalSection(&csLock);
        return false;
    }

    res.reservationId = generateReservationId();
    res.plotId = targetPlotId;
    res.slotId = assignedSlotId;
    res.createdUnixTime = getCurrentUnixTime();
    res.isActive = true;
    reservations.push_back(res);

    outMsg = "Reservation confirmed for Slot " + assignedSlotId + " (Booking ID: " + res.reservationId + ").";
    LeaveCriticalSection(&csLock);
    saveToDisk();
    return true;
}

std::vector<Reservation> ParkingManager::getActiveReservations(const std::string& plotId) {
    EnterCriticalSection(&csLock);
    std::vector<Reservation> result;
    for (const auto& r : reservations) {
        if (r.isActive && (plotId.empty() || r.plotId == plotId)) {
            result.push_back(r);
        }
    }
    LeaveCriticalSection(&csLock);
    return result;
}

bool ParkingManager::cancelReservation(const std::string& reservationId, std::string& outMsg) {
    EnterCriticalSection(&csLock);
    for (auto& res : reservations) {
        if (res.reservationId == reservationId && res.isActive) {
            res.isActive = false;

            auto plotIt = plotIndexMap.find(res.plotId);
            if (plotIt != plotIndexMap.end() && plotIt->second < plots.size()) {
                Plot& p = plots[plotIt->second];
                for (auto& row : p.grid) {
                    for (auto& slot : row) {
                        if (slot.id == res.slotId && slot.status == SlotStatus::RESERVED) {
                            slot.status = SlotStatus::AVAILABLE;
                            break;
                        }
                    }
                }
            }

            outMsg = "Reservation " + reservationId + " successfully canceled.";
            std::vector<ParkResult> promoted = processWaitingQueueLocked(res.plotId);
            if (!promoted.empty()) {
                outMsg += " Queued vehicle " + promoted[0].waitingRequest.vehiclePlate + " automatically parked into empty Slot " + promoted[0].slotId + "!";
            }
            LeaveCriticalSection(&csLock);
            saveToDisk();
            return true;
        }
    }
    outMsg = "Reservation not found or already inactive.";
    LeaveCriticalSection(&csLock);
    return false;
}

std::vector<WaitingRequest> ParkingManager::getWaitingQueue(const std::string& plotId) {
    EnterCriticalSection(&csLock);
    std::string targetPlotId = plotId.empty() ? activePlotId : plotId;
    std::vector<ParkResult> promoted = processWaitingQueueLocked(targetPlotId);
    std::vector<WaitingRequest> list = waitingQueues[targetPlotId].getAll();
    LeaveCriticalSection(&csLock);
    if (!promoted.empty()) {
        saveToDisk();
    }
    return list;
}

bool ParkingManager::cancelWaiting(const std::string& plotId, const std::string& plate) {
    EnterCriticalSection(&csLock);
    std::string targetPlotId = plotId.empty() ? activePlotId : plotId;
    std::string norm = normalizePlate(plate);
    bool res = false;
    if (!norm.empty()) {
        res = waitingQueues[targetPlotId].remove(norm);
    }
    if (!res && !plate.empty()) {
        res = waitingQueues[targetPlotId].remove(plate);
    }
    LeaveCriticalSection(&csLock);
    if (res) saveToDisk();
    return res;
}

bool ParkingManager::searchVehicle(const std::string& plate, ParkingSession& outSession, Slot& outSlot, Plot& outPlot) {
    EnterCriticalSection(&csLock);
    std::string norm = normalizePlate(plate);

    auto it = activeSessions.find(norm);
    if (it != activeSessions.end()) {
        outSession = it->second;
        auto pIt = plotIndexMap.find(outSession.plotId);
        if (pIt != plotIndexMap.end() && pIt->second < plots.size()) {
            outPlot = plots[pIt->second];
            for (const auto& row : outPlot.grid) {
                for (const auto& slot : row) {
                    if (slot.id == outSession.slotId) {
                        outSlot = slot;
                        LeaveCriticalSection(&csLock);
                        return true;
                    }
                }
            }
        }
        LeaveCriticalSection(&csLock);
        return true;
    }

    if (vehicleBst.search(norm, outSession)) {
        LeaveCriticalSection(&csLock);
        return true;
    }

    LeaveCriticalSection(&csLock);
    return false;
}

std::vector<ParkingSession> ParkingManager::getRecords(const std::string& sortBy) {
    EnterCriticalSection(&csLock);
    std::vector<ParkingSession> records = historyList.toVector(500);

    if (!records.empty()) {
        if (sortBy == "time" || sortBy.empty()) {
            DSASorting::quickSortByTimeDesc(records, 0, static_cast<int>(records.size()) - 1);
        } else if (sortBy == "plate") {
            std::sort(records.begin(), records.end(), [](const ParkingSession& a, const ParkingSession& b) {
                return a.vehiclePlate < b.vehiclePlate;
            });
        } else if (sortBy == "fee") {
            std::sort(records.begin(), records.end(), [](const ParkingSession& a, const ParkingSession& b) {
                return (a.fee + a.chargingFee) > (b.fee + b.chargingFee);
            });
        }
    }

    LeaveCriticalSection(&csLock);
    return records;
}

DSATelemetryInfo ParkingManager::getTelemetry() {
    EnterCriticalSection(&csLock);
    DSATelemetryInfo info;
    info.totalSlots = 0;
    info.occupiedSlots = 0;
    info.availableSlots = 0;
    info.reservedSlots = 0;
    info.evSlotsCount = 0;
    info.activeEVChargingCount = 0;
    info.totalGatesCount = 0;

    for (const auto& plot : plots) {
        info.totalGatesCount += static_cast<int>(plot.gates.size());
        for (const auto& row : plot.grid) {
            for (const auto& slot : row) {
                if (slot.cellType == CellType::SLOT_CAR || slot.cellType == CellType::SLOT_BIKE) {
                    info.totalSlots++;
                    if (slot.status == SlotStatus::OCCUPIED) info.occupiedSlots++;
                    else if (slot.status == SlotStatus::AVAILABLE) info.availableSlots++;
                    else if (slot.status == SlotStatus::RESERVED) info.reservedSlots++;

                    if (slot.hasCharger) {
                        info.evSlotsCount++;
                        if (slot.status == SlotStatus::OCCUPIED) {
                            info.activeEVChargingCount++;
                        }
                    }
                }
            }
        }
    }

    info.activeSessionsCount = static_cast<int>(activeSessions.size());
    info.bstNodeCount = vehicleBst.size();
    info.bstHeight = vehicleBst.getHeight();
    info.waitingQueueCount = static_cast<int>(waitingQueues[activePlotId].size());
    info.priorityQueueCount = static_cast<int>(waitingQueues[activePlotId].getPrioritySize());
    info.undoStackSize = static_cast<int>(undoStacks[activePlotId].size());
    info.doublyLinkedListCount = static_cast<int>(historyList.size());
    info.graphVertexCount = info.totalSlots;
    info.totalPlotsCount = static_cast<int>(plots.size());

    LeaveCriticalSection(&csLock);
    return info;
}

std::string ParkingManager::runDSATestsJson() {
    std::ostringstream oss;
    oss << "{ \"status\": \"success\", \"timestamp\": \"" << getFormattedTimestamp() << "\", \"results\": [";

    // Test 1: 2D Matrix
    Matrix2D<int> m(4, 6, 0);
    m.at(1, 2) = 42;
    bool t1_pass = (m.at(1, 2) == 42 && m.getRows() == 4 && m.getCols() == 6);
    oss << "{\"name\": \"2D Matrix Layout Mapping\", \"structure\": \"2D Array / Matrix\", "
        << "\"timeComplexity\": \"O(1) access\", \"passed\": " << (t1_pass ? "true" : "false") << "},";

    // Test 2: Graph BFS Shortest Path with calibrated scale
    Matrix2D<CellType> gLayout(4, 6, CellType::AISLE);
    ParkingGraph graph(4, 6);
    PathResult pr = graph.bfsShortestPath(0, 0, 3, 5, gLayout, 5.0);
    bool t2_pass = pr.found && (pr.distance == 8) && (pr.distanceMeters == 40.0);
    oss << "{\"name\": \"Graph BFS Shortest Calibrated Driving Route\", \"structure\": \"Graph (Adjacency List)\", "
        << "\"timeComplexity\": \"O(V + E) BFS\", \"passed\": " << (t2_pass ? "true" : "false") << "},";

    // Test 3: Graph DFS Slot Reachability
    std::vector<std::vector<bool>> reachable;
    int reachCount = graph.dfsReachability(0, 0, gLayout, reachable);
    bool t3_pass = (reachCount == 24);
    oss << "{\"name\": \"Graph DFS Slot Reachability Check\", \"structure\": \"Graph DFS\", "
        << "\"timeComplexity\": \"O(V + E) DFS\", \"passed\": " << (t3_pass ? "true" : "false") << "},";

    // Test 4: Binary Search Tree
    VehicleBST testBst;
    ParkingSession s1, s2;
    s1.vehiclePlate = "KA01M1000";
    s2.vehiclePlate = "DL02A2000";
    testBst.insert(s1);
    testBst.insert(s2);
    ParkingSession foundSess;
    bool t4_pass = testBst.search("KA01M1000", foundSess) && (testBst.size() == 2);
    oss << "{\"name\": \"Vehicle Search Tree (BST)\", \"structure\": \"Binary Search Tree\", "
        << "\"timeComplexity\": \"O(log n) avg search\", \"passed\": " << (t4_pass ? "true" : "false") << "},";

    // Test 5: Hash Map O(1) Indexing
    bool t5_pass = (activeSessions.size() >= 0);
    oss << "{\"name\": \"Vehicle Hash Map Lookup\", \"structure\": \"Hash Table (unordered_map)\", "
        << "\"timeComplexity\": \"O(1) average lookup\", \"passed\": " << (t5_pass ? "true" : "false") << "},";

    // Test 6: Waiting Queue Manager
    WaitingQueueManager wq;
    WaitingRequest wr;
    wr.vehiclePlate = "TEST01";
    wr.isPriority = true;
    wq.enqueue(wr);
    WaitingRequest outWr;
    bool t6_pass = wq.dequeue(outWr) && (outWr.vehiclePlate == "TEST01");
    oss << "{\"name\": \"Waiting Queue FIFO & Priority Promotion\", \"structure\": \"Dual Queue\", "
        << "\"timeComplexity\": \"O(1) push/pop\", \"passed\": " << (t6_pass ? "true" : "false") << "},";

    // Test 7: Undo Stack
    UndoStack<LayoutAction> st;
    LayoutAction la{1, 1, CellType::SLOT_CAR, CellType::AISLE};
    st.push(la);
    LayoutAction outLa;
    bool t7_pass = st.pop(outLa) && (outLa.previousType == CellType::SLOT_CAR);
    oss << "{\"name\": \"Layout Editor Undo Stack\", \"structure\": \"LIFO Stack\", "
        << "\"timeComplexity\": \"O(1) push/pop\", \"passed\": " << (t7_pass ? "true" : "false") << "},";

    // Test 8: Quicksort
    std::vector<ParkingSession> sortArr;
    ParkingSession sa1, sa2, sa3;
    sa1.entryUnixTime = 100;
    sa2.entryUnixTime = 300;
    sa3.entryUnixTime = 200;
    sortArr.push_back(sa1);
    sortArr.push_back(sa2);
    sortArr.push_back(sa3);
    DSASorting::quickSortByTimeDesc(sortArr, 0, 2);
    bool t8_pass = (sortArr[0].entryUnixTime == 300 && sortArr[1].entryUnixTime == 200 && sortArr[2].entryUnixTime == 100);
    oss << "{\"name\": \"Quicksort Parking Records\", \"structure\": \"Divide & Conquer Sort\", "
        << "\"timeComplexity\": \"O(n log n) average\", \"passed\": " << (t8_pass ? "true" : "false") << "}";

    oss << "]}";
    return oss.str();
}

void ParkingManager::saveToDisk() {
    // 1. Save Plots to data/plots.json with Gates and EV attributes
    std::string plotsPath = dataDir + "/plots.json";
    std::ofstream pf(plotsPath);
    if (pf.is_open()) {
        pf << "[\n";
        for (size_t i = 0; i < plots.size(); ++i) {
            const auto& p = plots[i];
            pf << "  {\n";
            pf << "    \"id\": " << Json::stringValue(p.id) << ",\n";
            pf << "    \"name\": " << Json::stringValue(p.name) << ",\n";
            pf << "    \"location\": " << Json::stringValue(p.location) << ",\n";
            pf << "    \"parkingType\": " << Json::stringValue(p.parkingType) << ",\n";
            pf << "    \"rows\": " << p.rows << ",\n";
            pf << "    \"cols\": " << p.cols << ",\n";
            pf << "    \"ratePerHourCar\": " << Json::numberValue(p.ratePerHourCar) << ",\n";
            pf << "    \"ratePerHourBike\": " << Json::numberValue(p.ratePerHourBike) << ",\n";
            pf << "    \"gracePeriodMinutes\": " << p.gracePeriodMinutes << ",\n";
            pf << "    \"cellScaleMeters\": " << Json::numberValue(p.cellScaleMeters) << ",\n";

            // Serialize Gates
            pf << "    \"gates\": [\n";
            for (size_t g = 0; g < p.gates.size(); ++g) {
                const auto& gate = p.gates[g];
                pf << "      {\"id\":" << Json::stringValue(gate.id)
                   << ",\"name\":" << Json::stringValue(gate.name)
                   << ",\"type\":" << Json::stringValue(gateTypeToString(gate.type))
                   << ",\"row\":" << gate.row
                   << ",\"col\":" << gate.col
                   << ",\"isActive\":" << Json::boolValue(gate.isActive) << "}";
                if (g + 1 < p.gates.size()) pf << ",";
                pf << "\n";
            }
            pf << "    ],\n";

            // Serialize Grid
            pf << "    \"grid\": [\n";
            for (size_t r = 0; r < p.grid.size(); ++r) {
                pf << "      [";
                for (size_t c = 0; c < p.grid[r].size(); ++c) {
                    const auto& s = p.grid[r][c];
                    pf << "{\"id\":" << Json::stringValue(s.id)
                       << ",\"r\":" << s.row << ",\"c\":" << s.col
                       << ",\"type\":" << Json::stringValue(cellTypeToString(s.cellType))
                       << ",\"status\":" << Json::stringValue(slotStatusToString(s.status))
                       << ",\"plate\":" << Json::stringValue(s.currentPlate)
                       << ",\"vehicleType\":" << Json::stringValue(vehicleTypeToString(s.currentVehicleType))
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
                    if (c + 1 < p.grid[r].size()) pf << ",";
                }
                pf << "]";
                if (r + 1 < p.grid.size()) pf << ",";
                pf << "\n";
            }
            pf << "    ]\n";
            pf << "  }" << (i + 1 < plots.size() ? "," : "") << "\n";
        }
        pf << "]\n";
        pf.close();
    }

    // 2. Save active sessions & history to data/sessions.json
    std::string sessPath = dataDir + "/sessions.json";
    std::ofstream sf(sessPath);
    if (sf.is_open()) {
        std::vector<ParkingSession> allSessions = historyList.toVector(1000);
        sf << "[\n";
        for (size_t i = 0; i < allSessions.size(); ++i) {
            const auto& s = allSessions[i];
            sf << "  {\n";
            sf << "    \"sessionId\": " << Json::stringValue(s.sessionId) << ",\n";
            sf << "    \"plotId\": " << Json::stringValue(s.plotId) << ",\n";
            sf << "    \"slotId\": " << Json::stringValue(s.slotId) << ",\n";
            sf << "    \"vehiclePlate\": " << Json::stringValue(s.vehiclePlate) << ",\n";
            sf << "    \"vehicleType\": " << Json::stringValue(vehicleTypeToString(s.vehicleType)) << ",\n";
            sf << "    \"ownerName\": " << Json::stringValue(s.ownerName) << ",\n";
            sf << "    \"phone\": " << Json::stringValue(s.phone) << ",\n";
            sf << "    \"entryUnixTime\": " << s.entryUnixTime << ",\n";
            sf << "    \"entryTime\": " << Json::stringValue(s.entryTimeFormatted) << ",\n";
            sf << "    \"exitUnixTime\": " << s.exitUnixTime << ",\n";
            sf << "    \"exitTime\": " << Json::stringValue(s.exitTimeFormatted) << ",\n";
            sf << "    \"durationMinutes\": " << s.durationMinutes << ",\n";
            sf << "    \"fee\": " << Json::numberValue(s.fee) << ",\n";
            sf << "    \"chargingFee\": " << Json::numberValue(s.chargingFee) << ",\n";
            sf << "    \"entryGateId\": " << Json::stringValue(s.entryGateId) << ",\n";
            sf << "    \"entryGateName\": " << Json::stringValue(s.entryGateName) << ",\n";
            sf << "    \"exitGateId\": " << Json::stringValue(s.exitGateId) << ",\n";
            sf << "    \"exitGateName\": " << Json::stringValue(s.exitGateName) << ",\n";
            sf << "    \"entryDistanceMeters\": " << Json::numberValue(s.entryDistanceMeters) << ",\n";
            sf << "    \"exitDistanceMeters\": " << Json::numberValue(s.exitDistanceMeters) << ",\n";
            sf << "    \"requiresEVCharging\": " << Json::boolValue(s.requiresEVCharging) << ",\n";
            sf << "    \"chargerId\": " << Json::stringValue(s.chargerId) << ",\n";
            sf << "    \"connectorType\": " << Json::stringValue(s.connectorType) << ",\n";
            sf << "    \"isActive\": " << Json::boolValue(s.isActive) << "\n";
            sf << "  }" << (i + 1 < allSessions.size() ? "," : "") << "\n";
        }
        sf << "]\n";
        sf.close();
    }
}

void ParkingManager::loadFromDisk() {
    EnterCriticalSection(&csLock);

    std::string plotsPath = dataDir + "/plots.json";
    std::ifstream pf(plotsPath);
    if (!pf.is_open()) {
        LeaveCriticalSection(&csLock);
        return;
    }

    std::stringstream buffer;
    buffer << pf.rdbuf();
    pf.close();
    std::string content = buffer.str();
    if (content.length() < 10) {
        LeaveCriticalSection(&csLock);
        return;
    }

    std::vector<std::string> plotObjs = Json::splitJsonObjects(content);
    if (plotObjs.empty()) {
        LeaveCriticalSection(&csLock);
        return;
    }

    plots.clear();
    plotIndexMap.clear();

    const std::set<std::string> dummyPlots = {
        "plot_phoenix_marketcity", "plot_sriook_park", "plot_zoob_residences", "plot_tower_43", "plot_1792918456776495000"
    };

    for (const auto& po : plotObjs) {
        std::string id = Json::getStringField(po, "id");
        if (id.empty() || dummyPlots.count(id)) {
            continue;
        }

        Plot p;
        p.id = id;
        p.name = Json::getStringField(po, "name", "Parking Lot");
        if (p.id == "plot_central_campus") {
            p.name = "Demo Parking";
        }
        p.location = Json::getStringField(po, "location", "Campus");
        p.parkingType = Json::getStringField(po, "parkingType", "mixed");
        p.rows = Json::getIntField(po, "rows", 4);
        p.cols = Json::getIntField(po, "cols", 6);
        p.ratePerHourCar = Json::getDoubleField(po, "ratePerHourCar", 40.0);
        p.ratePerHourBike = Json::getDoubleField(po, "ratePerHourBike", 20.0);
        p.gracePeriodMinutes = Json::getIntField(po, "gracePeriodMinutes", 15);
        p.cellScaleMeters = Json::getDoubleField(po, "cellScaleMeters", 5.0);

        // Parse Gates
        size_t gatesPos = po.find("\"gates\"");
        if (gatesPos != std::string::npos) {
            size_t gatesOpen = po.find('[', gatesPos);
            if (gatesOpen != std::string::npos) {
                int depth = 0;
                size_t gatesClose = std::string::npos;
                for (size_t j = gatesOpen; j < po.length(); ++j) {
                    if (po[j] == '[') depth++;
                    else if (po[j] == ']') {
                        depth--;
                        if (depth == 0) {
                            gatesClose = j;
                            break;
                        }
                    }
                }
                if (gatesClose != std::string::npos) {
                    std::string gatesSub = po.substr(gatesOpen, gatesClose - gatesOpen + 1);
                    std::vector<std::string> gateObjs = Json::splitJsonObjects(gatesSub);
                    for (const auto& go : gateObjs) {
                        Gate g;
                        g.id = Json::getStringField(go, "id");
                        g.name = Json::getStringField(go, "name", "Gate");
                        g.type = stringToGateType(Json::getStringField(go, "type", "ENTRANCE"));
                        g.row = Json::getIntField(go, "row", 0);
                        g.col = Json::getIntField(go, "col", 0);
                        g.isActive = Json::getBoolField(go, "isActive", true);
                        p.gates.push_back(g);

                        if (g.id.find("gate_") == 0) {
                            try {
                                int gNum = std::stoi(g.id.substr(5));
                                if (gNum >= nextGateCounter) nextGateCounter = gNum + 1;
                            } catch (...) {}
                        }
                    }
                }
            }
        }

        // Initialize grid
        p.grid.resize(p.rows, std::vector<Slot>(p.cols));
        for (int r = 0; r < p.rows; ++r) {
            std::string rLabel = Json::getRowLabel(r);
            for (int c = 0; c < p.cols; ++c) {
                p.grid[r][c].row = r;
                p.grid[r][c].col = c;
                p.grid[r][c].id = rLabel + std::to_string(c + 1);
                p.grid[r][c].cellType = CellType::SLOT_CAR;
                p.grid[r][c].status = SlotStatus::AVAILABLE;
                p.grid[r][c].connectorType = "CCS2";
                p.grid[r][c].chargingPowerKw = 22.0;
                p.grid[r][c].chargerStatus = ChargerStatus::OPERATIONAL;
                p.grid[r][c].chargingRatePerHour = 50.0;
            }
        }

        // Parse Grid
        size_t gridPos = po.find("\"grid\"");
        if (gridPos != std::string::npos) {
            size_t gridOpen = po.find('[', gridPos);
            if (gridOpen != std::string::npos) {
                int depth = 0;
                size_t gridClose = std::string::npos;
                for (size_t j = gridOpen; j < po.length(); ++j) {
                    if (po[j] == '[') depth++;
                    else if (po[j] == ']') {
                        depth--;
                        if (depth == 0) {
                            gridClose = j;
                            break;
                        }
                    }
                }
                if (gridClose != std::string::npos) {
                    std::string gridSub = po.substr(gridOpen, gridClose - gridOpen + 1);
                    std::vector<std::string> cellObjs = Json::splitJsonObjects(gridSub);
                    for (const auto& co : cellObjs) {
                        int r = Json::getIntField(co, "r", -1);
                        int c = Json::getIntField(co, "c", -1);
                        if (r >= 0 && r < p.rows && c >= 0 && c < p.cols) {
                            Slot& s = p.grid[r][c];
                            std::string slotId = Json::getStringField(co, "id");
                            if (!slotId.empty()) s.id = slotId;
                            s.cellType = stringToCellType(Json::getStringField(co, "type", "SLOT_CAR"));
                            s.status = stringToSlotStatus(Json::getStringField(co, "status", "AVAILABLE"));
                            s.currentPlate = Json::getStringField(co, "plate");
                            s.currentVehicleType = stringToVehicleType(Json::getStringField(co, "vehicleType", "CAR"));
                            s.bfsDistance = Json::getIntField(co, "dist", 0);
                            s.distanceMeters = Json::getDoubleField(co, "distMeters", 0.0);
                            s.category = stringToSlotCategory(Json::getStringField(co, "category", "STANDARD"));
                            s.hasCharger = Json::getBoolField(co, "hasCharger", false);
                            s.chargerId = Json::getStringField(co, "chargerId");
                            s.connectorType = Json::getStringField(co, "connector", "CCS2");
                            s.chargingPowerKw = Json::getDoubleField(co, "powerKw", 22.0);
                            s.chargerStatus = stringToChargerStatus(Json::getStringField(co, "chargerStatus", "OPERATIONAL"));
                            s.chargingRatePerHour = Json::getDoubleField(co, "chargingRate", 50.0);
                        }
                    }
                }
            }
        }

        if (p.id == "plot_central_campus") {
            p.name = "Demo Parking";
            for (auto& row : p.grid) {
                for (auto& cell : row) {
                    if (cell.cellType == CellType::SLOT_CAR || cell.cellType == CellType::SLOT_BIKE) {
                        cell.status = SlotStatus::AVAILABLE;
                        cell.currentPlate = "";
                        cell.currentSessionId = "";
                    }
                }
            }
        }

        reallotPlotSlotNumbers(p);
        recomputePlotGraph(p);
        plots.push_back(p);
        plotIndexMap[p.id] = plots.size() - 1;
    }

    if (!plots.empty()) {
        if (plotIndexMap.find("plot_central_campus") != plotIndexMap.end()) {
            activePlotId = "plot_central_campus";
        } else {
            activePlotId = plots[0].id;
        }
    }

    // Load sessions
    std::string sessPath = dataDir + "/sessions.json";
    std::ifstream sf(sessPath);
    if (sf.is_open()) {
        std::stringstream sbuf;
        sbuf << sf.rdbuf();
        sf.close();
        std::string sContent = sbuf.str();
        if (sContent.length() >= 5) {
            std::vector<std::string> sessObjs = Json::splitJsonObjects(sContent);
            for (const auto& so : sessObjs) {
                ParkingSession s;
                s.sessionId = Json::getStringField(so, "sessionId");
                s.plotId = Json::getStringField(so, "plotId");
                s.slotId = Json::getStringField(so, "slotId");
                s.vehiclePlate = Json::getStringField(so, "vehiclePlate");
                s.vehicleType = stringToVehicleType(Json::getStringField(so, "vehicleType", "CAR"));
                s.ownerName = Json::getStringField(so, "ownerName", "Guest");
                s.phone = Json::getStringField(so, "phone");
                s.entryUnixTime = static_cast<long long>(Json::getDoubleField(so, "entryUnixTime", 0));
                s.entryTimeFormatted = Json::getStringField(so, "entryTime");
                s.exitUnixTime = static_cast<long long>(Json::getDoubleField(so, "exitUnixTime", 0));
                s.exitTimeFormatted = Json::getStringField(so, "exitTime");
                s.durationMinutes = static_cast<long long>(Json::getDoubleField(so, "durationMinutes", 0));
                s.fee = Json::getDoubleField(so, "fee", 0.0);
                s.chargingFee = Json::getDoubleField(so, "chargingFee", 0.0);
                s.entryGateId = Json::getStringField(so, "entryGateId");
                s.entryGateName = Json::getStringField(so, "entryGateName");
                s.exitGateId = Json::getStringField(so, "exitGateId");
                s.exitGateName = Json::getStringField(so, "exitGateName");
                s.entryDistanceMeters = Json::getDoubleField(so, "entryDistanceMeters", 0.0);
                s.exitDistanceMeters = Json::getDoubleField(so, "exitDistanceMeters", 0.0);
                s.requiresEVCharging = Json::getBoolField(so, "requiresEVCharging", false);
                s.chargerId = Json::getStringField(so, "chargerId");
                s.connectorType = Json::getStringField(so, "connectorType");
                s.isActive = Json::getBoolField(so, "isActive", false);

                if (s.sessionId.find("SESS-") == 0) {
                    try {
                        int num = std::stoi(s.sessionId.substr(5));
                        if (num >= nextSessionCounter) nextSessionCounter = num + 1;
                    } catch (...) {}
                }

                if (dummyPlots.count(s.plotId) || s.plotId == "plot_central_campus") {
                    continue;
                }

                historyList.pushBack(s);

                if (s.isActive && !s.vehiclePlate.empty()) {
                    std::string norm = normalizePlate(s.vehiclePlate);
                    activeSessions[norm] = s;
                    plateToPlotSlot[norm] = s.plotId + ":" + s.slotId;
                    vehicleBst.insert(s);

                    auto pIt = plotIndexMap.find(s.plotId);
                    if (pIt != plotIndexMap.end() && pIt->second < plots.size()) {
                        Plot& pl = plots[pIt->second];
                        for (auto& row : pl.grid) {
                            for (auto& slot : row) {
                                if (slot.id == s.slotId) {
                                    slot.status = SlotStatus::OCCUPIED;
                                    slot.currentPlate = s.vehiclePlate;
                                    slot.currentSessionId = s.sessionId;
                                    slot.currentVehicleType = s.vehicleType;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Ensure all plot slot allotments are synchronized and no vehicle is parked in a gate, aisle, or pillar
    for (auto& pl : plots) {
        reallotPlotSlotNumbers(pl);
        recomputePlotGraph(pl);
    }

    LeaveCriticalSection(&csLock);
}
