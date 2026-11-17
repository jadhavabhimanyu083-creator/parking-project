#ifndef MODELS_HPP
#define MODELS_HPP

#include <string>
#include <vector>

enum class VehicleType {
    CAR = 0,
    TWO_WHEELER = 1,
    UNKNOWN = 2
};

enum class CellType {
    SLOT_CAR = 0,
    SLOT_BIKE = 1,
    AISLE = 2,
    ENTRANCE = 3,
    EXIT = 4,
    BLOCKED = 5,
    FACILITY = 6
};

enum class SlotStatus {
    AVAILABLE = 0,
    OCCUPIED = 1,
    RESERVED = 2,
    BLOCKED = 3
};

enum class GateType {
    ENTRANCE = 0,
    EXIT = 1,
    BIDIRECTIONAL = 2
};

enum class SlotCategory {
    STANDARD = 0,
    EV_CHARGING = 1,
    ACCESSIBLE = 2,
    BIKE = 3
};

enum class ChargerStatus {
    OPERATIONAL = 0,
    OUT_OF_SERVICE = 1,
    MAINTENANCE = 2
};

inline std::string vehicleTypeToString(VehicleType type) {
    switch (type) {
        case VehicleType::CAR: return "CAR";
        case VehicleType::TWO_WHEELER: return "TWO_WHEELER";
        default: return "UNKNOWN";
    }
}

inline VehicleType stringToVehicleType(const std::string& str) {
    if (str == "CAR" || str == "car") return VehicleType::CAR;
    if (str == "TWO_WHEELER" || str == "bike" || str == "two_wheeler") return VehicleType::TWO_WHEELER;
    return VehicleType::CAR;
}

inline std::string cellTypeToString(CellType type) {
    switch (type) {
        case CellType::SLOT_CAR: return "SLOT_CAR";
        case CellType::SLOT_BIKE: return "SLOT_BIKE";
        case CellType::AISLE: return "AISLE";
        case CellType::ENTRANCE: return "ENTRANCE";
        case CellType::EXIT: return "EXIT";
        case CellType::BLOCKED: return "BLOCKED";
        case CellType::FACILITY: return "FACILITY";
        default: return "AISLE";
    }
}

inline CellType stringToCellType(const std::string& str) {
    if (str == "SLOT_CAR") return CellType::SLOT_CAR;
    if (str == "SLOT_BIKE") return CellType::SLOT_BIKE;
    if (str == "AISLE") return CellType::AISLE;
    if (str == "ENTRANCE") return CellType::ENTRANCE;
    if (str == "EXIT") return CellType::EXIT;
    if (str == "BLOCKED") return CellType::BLOCKED;
    if (str == "FACILITY") return CellType::FACILITY;
    return CellType::SLOT_CAR;
}

inline std::string slotStatusToString(SlotStatus status) {
    switch (status) {
        case SlotStatus::AVAILABLE: return "AVAILABLE";
        case SlotStatus::OCCUPIED: return "OCCUPIED";
        case SlotStatus::RESERVED: return "RESERVED";
        case SlotStatus::BLOCKED: return "BLOCKED";
        default: return "AVAILABLE";
    }
}

inline SlotStatus stringToSlotStatus(const std::string& str) {
    if (str == "OCCUPIED") return SlotStatus::OCCUPIED;
    if (str == "RESERVED") return SlotStatus::RESERVED;
    if (str == "BLOCKED") return SlotStatus::BLOCKED;
    return SlotStatus::AVAILABLE;
}

inline std::string gateTypeToString(GateType type) {
    switch (type) {
        case GateType::ENTRANCE: return "ENTRANCE";
        case GateType::EXIT: return "EXIT";
        case GateType::BIDIRECTIONAL: return "BIDIRECTIONAL";
        default: return "ENTRANCE";
    }
}

inline GateType stringToGateType(const std::string& str) {
    if (str == "EXIT" || str == "exit") return GateType::EXIT;
    if (str == "BIDIRECTIONAL" || str == "bidirectional" || str == "BOTH") return GateType::BIDIRECTIONAL;
    return GateType::ENTRANCE;
}

inline std::string slotCategoryToString(SlotCategory cat) {
    switch (cat) {
        case SlotCategory::EV_CHARGING: return "EV_CHARGING";
        case SlotCategory::ACCESSIBLE: return "ACCESSIBLE";
        case SlotCategory::BIKE: return "BIKE";
        default: return "STANDARD";
    }
}

inline SlotCategory stringToSlotCategory(const std::string& str) {
    if (str == "EV_CHARGING" || str == "EV" || str == "ev") return SlotCategory::EV_CHARGING;
    if (str == "ACCESSIBLE" || str == "accessible") return SlotCategory::ACCESSIBLE;
    if (str == "BIKE" || str == "bike") return SlotCategory::BIKE;
    return SlotCategory::STANDARD;
}

inline std::string chargerStatusToString(ChargerStatus status) {
    switch (status) {
        case ChargerStatus::OPERATIONAL: return "OPERATIONAL";
        case ChargerStatus::OUT_OF_SERVICE: return "OUT_OF_SERVICE";
        case ChargerStatus::MAINTENANCE: return "MAINTENANCE";
        default: return "OPERATIONAL";
    }
}

inline ChargerStatus stringToChargerStatus(const std::string& str) {
    if (str == "OUT_OF_SERVICE" || str == "broken" || str == "offline") return ChargerStatus::OUT_OF_SERVICE;
    if (str == "MAINTENANCE") return ChargerStatus::MAINTENANCE;
    return ChargerStatus::OPERATIONAL;
}

struct Gate {
    std::string id;       // e.g. "gate_in_1"
    std::string name;     // e.g. "Main Entrance (North)"
    GateType type = GateType::ENTRANCE; // ENTRANCE, EXIT, BIDIRECTIONAL
    int row = 0;
    int col = 0;
    bool isActive = true;
};

struct Slot {
    std::string id;
    int row = 0;
    int col = 0;
    CellType cellType = CellType::SLOT_CAR;
    SlotStatus status = SlotStatus::AVAILABLE;
    std::string currentPlate = "";
    std::string currentSessionId = "";
    int bfsDistance = 0;        // Segment count from active entrance
    double distanceMeters = 0.0; // Actual driving distance in meters
    VehicleType currentVehicleType = VehicleType::CAR;

    // EV Charging Facilities (Modules 5 & 6)
    SlotCategory category = SlotCategory::STANDARD;
    bool hasCharger = false;
    std::string chargerId = "";         // e.g. "EV-CHG-01"
    std::string connectorType = "CCS2"; // "CCS2", "TYPE_2_AC", "BHARAT_AC001", "CHADEMO"
    double chargingPowerKw = 22.0;       // e.g. 22kW
    ChargerStatus chargerStatus = ChargerStatus::OPERATIONAL;
    double chargingRatePerHour = 50.0;  // ₹ per hour for EV charging
};

struct Plot {
    std::string id;
    std::string name;
    std::string location;
    std::string parkingType; // "car", "bike", "mixed"
    int rows = 4;
    int cols = 6;
    double ratePerHourCar = 40.0;
    double ratePerHourBike = 20.0;
    int gracePeriodMinutes = 15;
    double cellScaleMeters = 5.0; // Configured map scale: 5 meters per grid cell

    // Multiple Admin-Configured Entry/Exit Gates (Module 1)
    std::vector<Gate> gates;

    // Grid cells (rows x cols)
    std::vector<std::vector<Slot>> grid;
};

struct Vehicle {
    std::string plate;
    VehicleType type = VehicleType::CAR;
    std::string ownerName = "Guest";
    std::string phone = "";
    bool isPriority = false;
    bool requiresEVCharging = false;
    std::string preferredConnector = "ANY";
};

struct ParkingSession {
    std::string sessionId;
    std::string plotId;
    std::string slotId;
    std::string vehiclePlate;
    VehicleType vehicleType = VehicleType::CAR;
    std::string ownerName = "Guest";
    std::string phone = "";
    long long entryUnixTime = 0;
    std::string entryTimeFormatted = "";
    long long exitUnixTime = 0;
    std::string exitTimeFormatted = "";
    long long durationMinutes = 0;
    double fee = 0.0;
    double chargingFee = 0.0;
    bool isActive = true;

    // Gate & Driving Distance (Modules 1, 2, 3, 8)
    std::string entryGateId = "";
    std::string entryGateName = "";
    std::string exitGateId = "";
    std::string exitGateName = "";
    double entryDistanceMeters = 0.0;
    double exitDistanceMeters = 0.0;

    // EV Charging Details (Modules 4, 6, 8)
    bool requiresEVCharging = false;
    std::string chargerId = "";
    std::string connectorType = "";
};

struct Reservation {
    std::string reservationId;
    std::string plotId;
    std::string slotId;
    std::string vehiclePlate;
    VehicleType vehicleType = VehicleType::CAR;
    std::string customerName;
    std::string phone;
    std::string scheduledTime;
    long long createdUnixTime = 0;
    bool isActive = true;
    bool requiresEVCharging = false;
    std::string chargerId = "";
};

struct WaitingRequest {
    std::string waitId;
    std::string plotId;
    std::string vehiclePlate;
    VehicleType vehicleType = VehicleType::CAR;
    std::string ownerName;
    std::string phone;
    bool isPriority = false;
    bool requiresEVCharging = false;
    std::string preferredConnector = "ANY";
    long long requestUnixTime = 0;
    std::string requestTimeFormatted = "";
};

struct LayoutAction {
    int row;
    int col;
    CellType previousType;
    CellType newType;
};

#endif // MODELS_HPP
