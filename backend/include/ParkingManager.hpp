#ifndef PARKING_MANAGER_HPP
#define PARKING_MANAGER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <windows.h>
#include "Models.hpp"
#include "DSA_Structures.hpp"

struct ParkResult {
    bool success = false;
    bool isQueued = false;
    std::string message = "";
    std::string slotId = "";
    std::string sessionId = "";
    int bfsDistance = 0;
    double distanceMeters = 0.0;
    int estimatedTimeSeconds = 0;
    std::string gateId = "";
    std::string gateName = "";
    bool isEVChargingAssigned = false;
    std::string chargerId = "";
    std::string connectorType = "";
    WaitingRequest waitingRequest;
};

struct CheckoutResult {
    bool success = false;
    std::string message = "";
    ParkingSession session;
    bool autoAllocatedQueued = false;
    std::string autoPlate = "";
    std::string autoSlotId = "";
    std::string exitGateId = "";
    std::string exitGateName = "";
    double exitDistanceMeters = 0.0;
    double parkingFee = 0.0;
    double chargingFee = 0.0;
    double totalFee = 0.0;
};

struct LayoutValidationReport {
    bool allSlotsReachable = true;
    int totalSlots = 0;
    int reachableSlots = 0;
    int disconnectedSlots = 0;
    int activeEntrances = 0;
    int activeExits = 0;
    std::vector<std::string> unreachableSlotIds;
    std::vector<std::string> warnings;
};

struct DSATelemetryInfo {
    int totalSlots;
    int occupiedSlots;
    int availableSlots;
    int reservedSlots;
    int activeSessionsCount;
    int bstNodeCount;
    int bstHeight;
    int waitingQueueCount;
    int priorityQueueCount;
    int undoStackSize;
    int doublyLinkedListCount;
    int graphVertexCount;
    int totalPlotsCount;
    int evSlotsCount;
    int activeEVChargingCount;
    int totalGatesCount;
};

class ParkingManager {
private:
    std::vector<Plot> plots;
    std::string activePlotId;

    // Fast O(1) Hash Maps
    std::unordered_map<std::string, size_t> plotIndexMap;     // plotId -> index
    std::unordered_map<std::string, ParkingSession> activeSessions; // plate -> session
    std::unordered_map<std::string, std::string> plateToPlotSlot;   // plate -> "plotId:slotId"

    // BST for vehicle search
    VehicleBST vehicleBst;

    // Doubly Linked List for chronological audit history
    DoublyLinkedList<ParkingSession> historyList;

    // Waiting Queue Manager per plot
    std::unordered_map<std::string, WaitingQueueManager> waitingQueues;

    // Undo Stacks for plot layout designer
    std::unordered_map<std::string, UndoStack<LayoutAction>> undoStacks;

    // Reservations
    std::vector<Reservation> reservations;

    // Thread Safety Mutex
    CRITICAL_SECTION csLock;

    int nextSessionCounter;
    int nextWaitCounter;
    int nextReservationCounter;
    int nextGateCounter;

    std::string dataDir;

    // Private helper for waiting queue auto-allocation
    std::vector<ParkResult> processWaitingQueueLocked(const std::string& targetPlotId);

public:
    ParkingManager(const std::string& storageDir = "data");
    ~ParkingManager();

    void initialize();

    // Multi-Plot Operations
    std::vector<Plot> getAllPlots();
    bool getPlot(const std::string& plotId, Plot& outPlot);
    bool getActivePlot(Plot& outPlot);
    bool setActivePlot(const std::string& plotId);
    bool createPlot(const Plot& newPlot, std::string& errorMsg);
    bool updatePlot(const Plot& updatedPlot, std::string& errorMsg);
    bool deletePlot(const std::string& plotId, std::string& errorMsg);

    // Module 1: Admin-Configured Entry/Exit Gates
    bool addGate(const std::string& plotId, const Gate& gate, std::string& err);
    bool updateGate(const std::string& plotId, const Gate& gate, std::string& err);
    bool removeGate(const std::string& plotId, const std::string& gateId, std::string& err);
    bool toggleGate(const std::string& plotId, const std::string& gateId, bool active, std::string& err);
    std::vector<Gate> getGates(const std::string& plotId);
    bool updateScale(const std::string& plotId, double scaleMeters, std::string& err);

    // Module 5: EV Charging Slot Management
    bool configureSlotCharger(const std::string& plotId, const std::string& slotId, 
                              bool hasCharger, const std::string& chargerId, 
                              const std::string& connectorType, double powerKw, 
                              ChargerStatus status, double ratePerHour, std::string& err);
    bool toggleChargerStatus(const std::string& plotId, const std::string& slotId, 
                             ChargerStatus status, std::string& err);

    // Layout Design & Stack Undo
    bool recordCellEdit(const std::string& plotId, int row, int col, CellType newType);
    bool undoLastEdit(const std::string& plotId, int& outRow, int& outCol, CellType& revertedType);
    LayoutValidationReport validateLayout(const std::string& plotId);

    // Parking Operations (Modules 2, 3, 4, 6)
    ParkResult parkVehicle(const std::string& plotId, const std::string& plate, 
                           VehicleType type, bool isPriority,
                           const std::string& owner = "Guest", const std::string& phone = "",
                           bool requiresEVCharging = false, 
                           const std::string& preferredConnector = "ANY",
                           const std::string& selectedGateId = "");

    // Checkout & Departure Routing (Module 8)
    CheckoutResult exitVehicle(const std::string& plateOrSlotId, const std::string& exitGateId = "");

    // Route & Accurate Distance Calculation (Modules 2, 3, 7)
    PathResult getRoute(const std::string& plotId, const std::string& slotId, 
                        const std::string& gateId = "", bool isExitRoute = false);
    PathResult getShortestPathToSlot(const std::string& plotId, const std::string& slotId);

    // Reservations
    bool createReservation(Reservation res, std::string& outMsg);
    std::vector<Reservation> getActiveReservations(const std::string& plotId = "");
    bool cancelReservation(const std::string& reservationId, std::string& outMsg);

    // Waiting Queue & Automatic Bay Allocation
    std::vector<ParkResult> processWaitingQueue(const std::string& plotId = "");
    std::vector<WaitingRequest> getWaitingQueue(const std::string& plotId);
    bool cancelWaiting(const std::string& plotId, const std::string& plate);

    // Vehicle Search & Records
    bool searchVehicle(const std::string& plate, ParkingSession& outSession, Slot& outSlot, Plot& outPlot);
    std::vector<ParkingSession> getRecords(const std::string& sortBy = "time");

    // DSA Telemetry & Viva Testing
    DSATelemetryInfo getTelemetry();
    std::string runDSATestsJson();

    // Persistence
    void loadFromDisk();
    void saveToDisk();

private:
    void setupDefaultPlot();
    void reallotPlotSlotNumbers(Plot& plot);
    bool isCellGate(const Plot& plot, int r, int c) const;
    bool isParkableSlot(const Plot& plot, int r, int c) const;
    void recomputePlotGraph(Plot& plot);
    std::string generateSessionId();
    std::string generateWaitId();
    std::string generateReservationId();
    std::string generateGateId();
    long long getCurrentUnixTime();
    std::string getFormattedTimestamp(long long unixTime = 0);
    std::string normalizePlate(const std::string& plate);
};

#endif // PARKING_MANAGER_HPP
