#ifndef DSA_STRUCTURES_HPP
#define DSA_STRUCTURES_HPP

#include <string>
#include <vector>
#include <queue>
#include <stack>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <iostream>
#include <cmath>
#include "Models.hpp"

// ============================================================================
// 1. 2D ARRAY / MATRIX (PARKING GRID LAYOUT)
// ============================================================================
template <typename T>
class Matrix2D {
private:
    int rows;
    int cols;
    std::vector<std::vector<T>> data;

public:
    Matrix2D(int r = 4, int c = 6, const T& defaultVal = T()) : rows(r), cols(c) {
        data.resize(rows, std::vector<T>(cols, defaultVal));
    }

    int getRows() const { return rows; }
    int getCols() const { return cols; }

    bool isValid(int r, int c) const {
        return r >= 0 && r < rows && c >= 0 && c < cols;
    }

    T& at(int r, int c) {
        if (!isValid(r, c)) throw std::out_of_range("Matrix indices out of range");
        return data[r][c];
    }

    const T& at(int r, int c) const {
        if (!isValid(r, c)) throw std::out_of_range("Matrix indices out of range");
        return data[r][c];
    }

    void resize(int newRows, int newCols, const T& defaultVal = T()) {
        rows = newRows;
        cols = newCols;
        data.assign(rows, std::vector<T>(cols, defaultVal));
    }

    const std::vector<std::vector<T>>& raw() const { return data; }
    std::vector<std::vector<T>>& raw() { return data; }
};

// ============================================================================
// 2. GRAPH DATA STRUCTURE WITH BFS & DFS
// ============================================================================
struct GraphNode {
    int r;
    int c;
};

struct PathResult {
    bool found = false;
    int distance = -1;             // Total traversable segments
    double distanceMeters = 0.0;   // Calibrated real-world driving distance in meters
    int estimatedTimeSeconds = 0; // Estimated driving time (at 15 km/h)
    std::string fromGateId = "";
    std::string fromGateName = "";
    std::vector<std::pair<int, int>> path; // Sequence of (row, col)
};

class ParkingGraph {
private:
    int rows;
    int cols;

public:
    ParkingGraph(int r = 4, int c = 6) : rows(r), cols(c) {}

    void setDimensions(int r, int c) {
        rows = r;
        cols = c;
    }

    // Shortest driving path on traversable route network
    PathResult bfsShortestPath(int startR, int startC, int destR, int destC, 
                              const Matrix2D<CellType>& layout, double cellScaleMeters = 5.0) const {
        PathResult res;
        res.found = false;
        res.distance = -1;
        res.distanceMeters = 0.0;
        res.estimatedTimeSeconds = 0;

        if (!layout.isValid(startR, startC) || !layout.isValid(destR, destC)) {
            return res;
        }

        std::vector<std::vector<bool>> visited(rows, std::vector<bool>(cols, false));
        std::vector<std::vector<std::pair<int, int>>> parent(rows, std::vector<std::pair<int, int>>(cols, {-1, -1}));
        std::queue<std::pair<int, int>> q;

        q.push({startR, startC});
        visited[startR][startC] = true;

        const int dr[4] = {-1, 1, 0, 0};
        const int dc[4] = {0, 0, -1, 1};

        bool reached = false;

        bool hasAisles = false;
        for (int r = 0; r < rows && !hasAisles; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (layout.at(r, c) == CellType::AISLE) {
                    hasAisles = true;
                    break;
                }
            }
        }

        while (!q.empty()) {
            auto curr = q.front();
            q.pop();

            int cr = curr.first;
            int cc = curr.second;

            if (cr == destR && cc == destC) {
                reached = true;
                break;
            }

            for (int i = 0; i < 4; ++i) {
                int nr = cr + dr[i];
                int nc = cc + dc[i];

                if (nr >= 0 && nr < rows && nc >= 0 && nc < cols && !visited[nr][nc]) {
                    CellType cell = layout.at(nr, nc);
                    bool passable = false;
                    if (hasAisles) {
                        passable = (cell == CellType::AISLE || cell == CellType::ENTRANCE || 
                                    cell == CellType::EXIT || cell == CellType::FACILITY || (nr == destR && nc == destC));
                    } else {
                        passable = (cell != CellType::BLOCKED);
                    }
                    if (passable) {
                        visited[nr][nc] = true;
                        parent[nr][nc] = {cr, cc};
                        q.push({nr, nc});
                    }
                }
            }
        }

        if (reached) {
            res.found = true;
            // Backtrack path
            std::pair<int, int> curr = {destR, destC};
            while (curr.first != -1 && curr.second != -1) {
                res.path.push_back(curr);
                if (curr.first == startR && curr.second == startC) break;
                curr = parent[curr.first][curr.second];
            }
            std::reverse(res.path.begin(), res.path.end());
            res.distance = static_cast<int>(res.path.size()) - 1;
            res.distanceMeters = res.distance * (cellScaleMeters > 0.0 ? cellScaleMeters : 5.0);
            // 15 km/h driving speed = ~4.17 m/s
            res.estimatedTimeSeconds = static_cast<int>(std::round(res.distanceMeters / 4.167));
            if (res.estimatedTimeSeconds < 5 && res.distance > 0) res.estimatedTimeSeconds = 5;
        }

        return res;
    }

    // DFS: Validates overall layout connectivity (checks reachable slots from Entrance)
    int dfsReachability(int startR, int startC, const Matrix2D<CellType>& layout, 
                        std::vector<std::vector<bool>>& reachable) const {
        if (!layout.isValid(startR, startC)) return 0;
        
        reachable.assign(rows, std::vector<bool>(cols, false));
        std::stack<std::pair<int, int>> s;

        s.push({startR, startC});
        reachable[startR][startC] = true;

        const int dr[4] = {-1, 1, 0, 0};
        const int dc[4] = {0, 0, -1, 1};
        int visitedCount = 0;

        while (!s.empty()) {
            auto curr = s.top();
            s.pop();
            visitedCount++;

            int cr = curr.first;
            int cc = curr.second;

            for (int i = 0; i < 4; ++i) {
                int nr = cr + dr[i];
                int nc = cc + dc[i];

                if (nr >= 0 && nr < rows && nc >= 0 && nc < cols && !reachable[nr][nc]) {
                    CellType cell = layout.at(nr, nc);
                    // Any cell that isn't BLOCKED is traversed
                    if (cell != CellType::BLOCKED) {
                        reachable[nr][nc] = true;
                        // Only continue searching through AISLE, ENTRANCE, EXIT
                        if (cell == CellType::AISLE || cell == CellType::ENTRANCE || cell == CellType::EXIT) {
                            s.push({nr, nc});
                        }
                    }
                }
            }
        }

        return visitedCount;
    }
};

// ============================================================================
// 3. BINARY SEARCH TREE (BST) FOR VEHICLE RECORDS
// ============================================================================
struct BSTNode {
    std::string licensePlate;
    ParkingSession session;
    BSTNode* left;
    BSTNode* right;

    BSTNode(const ParkingSession& sess) 
        : licensePlate(sess.vehiclePlate), session(sess), left(nullptr), right(nullptr) {}
};

class VehicleBST {
private:
    BSTNode* root;
    int nodeCount;

    BSTNode* insertRec(BSTNode* node, const ParkingSession& sess) {
        if (!node) {
            nodeCount++;
            return new BSTNode(sess);
        }
        if (sess.vehiclePlate < node->licensePlate) {
            node->left = insertRec(node->left, sess);
        } else if (sess.vehiclePlate > node->licensePlate) {
            node->right = insertRec(node->right, sess);
        } else {
            // Update session data if matching plate
            node->session = sess;
        }
        return node;
    }

    BSTNode* searchRec(BSTNode* node, const std::string& plate) const {
        if (!node || node->licensePlate == plate) {
            return node;
        }
        if (plate < node->licensePlate) {
            return searchRec(node->left, plate);
        }
        return searchRec(node->right, plate);
    }

    void inOrderRec(BSTNode* node, std::vector<ParkingSession>& result) const {
        if (!node) return;
        inOrderRec(node->left, result);
        result.push_back(node->session);
        inOrderRec(node->right, result);
    }

    int getHeightRec(BSTNode* node) const {
        if (!node) return 0;
        return 1 + std::max(getHeightRec(node->left), getHeightRec(node->right));
    }

    void destroyRec(BSTNode* node) {
        if (!node) return;
        destroyRec(node->left);
        destroyRec(node->right);
        delete node;
    }

public:
    VehicleBST() : root(nullptr), nodeCount(0) {}
    ~VehicleBST() {
        destroyRec(root);
    }

    void clear() {
        destroyRec(root);
        root = nullptr;
        nodeCount = 0;
    }

    void insert(const ParkingSession& session) {
        root = insertRec(root, session);
    }

    bool search(const std::string& plate, ParkingSession& outSession) const {
        BSTNode* node = searchRec(root, plate);
        if (node) {
            outSession = node->session;
            return true;
        }
        return false;
    }

    std::vector<ParkingSession> getInOrderList() const {
        std::vector<ParkingSession> result;
        inOrderRec(root, result);
        return result;
    }

    int size() const { return nodeCount; }
    int getHeight() const { return getHeightRec(root); }
};

// ============================================================================
// 4. STACK DATA STRUCTURE (FOR UNDO OPERATIONS IN LAYOUT DESIGNER)
// ============================================================================
template <typename T>
class UndoStack {
private:
    std::stack<T> history;
    size_t maxCapacity;

public:
    UndoStack(size_t cap = 100) : maxCapacity(cap) {}

    void push(const T& item) {
        history.push(item);
    }

    bool pop(T& outItem) {
        if (history.empty()) return false;
        outItem = history.top();
        history.pop();
        return true;
    }

    bool peek(T& outItem) const {
        if (history.empty()) return false;
        outItem = history.top();
        return true;
    }

    bool empty() const { return history.empty(); }
    size_t size() const { return history.size(); }
    void clear() {
        while (!history.empty()) history.pop();
    }
};

// ============================================================================
// 5. DOUBLY LINKED LIST (PARKING AUDIT HISTORY TRAIL)
// ============================================================================
template <typename T>
class DoublyLinkedList {
private:
    struct DLLNode {
        T data;
        DLLNode* prev;
        DLLNode* next;
        DLLNode(const T& val) : data(val), prev(nullptr), next(nullptr) {}
    };

    DLLNode* head;
    DLLNode* tail;
    size_t count;

public:
    DoublyLinkedList() : head(nullptr), tail(nullptr), count(0) {}
    ~DoublyLinkedList() { clear(); }

    void clear() {
        DLLNode* curr = head;
        while (curr) {
            DLLNode* next = curr->next;
            delete curr;
            curr = next;
        }
        head = nullptr;
        tail = nullptr;
        count = 0;
    }

    void pushFront(const T& val) {
        DLLNode* node = new DLLNode(val);
        if (!head) {
            head = tail = node;
        } else {
            node->next = head;
            head->prev = node;
            head = node;
        }
        count++;
    }

    void pushBack(const T& val) {
        DLLNode* node = new DLLNode(val);
        if (!tail) {
            head = tail = node;
        } else {
            tail->next = node;
            node->prev = tail;
            tail = node;
        }
        count++;
    }

    size_t size() const { return count; }

    std::vector<T> toVector(size_t limit = 1000) const {
        std::vector<T> result;
        DLLNode* curr = head;
        size_t c = 0;
        while (curr && c < limit) {
            result.push_back(curr->data);
            curr = curr->next;
            c++;
        }
        return result;
    }
};

// ============================================================================
// 6. WAITING QUEUE MANAGER (DUAL FIFO & PRIORITY QUEUE)
// ============================================================================
class WaitingQueueManager {
private:
    std::vector<WaitingRequest> priorityQueue; // Emergency / VIP vehicles
    std::vector<WaitingRequest> standardQueue; // Regular FIFO vehicles

public:
    WaitingQueueManager() {}

    void enqueue(const WaitingRequest& req) {
        if (req.isPriority) {
            priorityQueue.push_back(req);
        } else {
            standardQueue.push_back(req);
        }
    }

    bool dequeue(WaitingRequest& outReq) {
        if (!priorityQueue.empty()) {
            outReq = priorityQueue.front();
            priorityQueue.erase(priorityQueue.begin());
            return true;
        }
        if (!standardQueue.empty()) {
            outReq = standardQueue.front();
            standardQueue.erase(standardQueue.begin());
            return true;
        }
        return false;
    }

    bool remove(const std::string& plateOrWaitId) {
        for (auto it = priorityQueue.begin(); it != priorityQueue.end(); ++it) {
            if (it->vehiclePlate == plateOrWaitId || it->waitId == plateOrWaitId) {
                priorityQueue.erase(it);
                return true;
            }
        }
        for (auto it = standardQueue.begin(); it != standardQueue.end(); ++it) {
            if (it->vehiclePlate == plateOrWaitId || it->waitId == plateOrWaitId) {
                standardQueue.erase(it);
                return true;
            }
        }
        return false;
    }

    std::vector<WaitingRequest> getAll() const {
        std::vector<WaitingRequest> combined = priorityQueue;
        combined.insert(combined.end(), standardQueue.begin(), standardQueue.end());
        return combined;
    }

    size_t size() const {
        return priorityQueue.size() + standardQueue.size();
    }

    size_t getPrioritySize() const { return priorityQueue.size(); }
    size_t getStandardSize() const { return standardQueue.size(); }
    
    void clear() {
        priorityQueue.clear();
        standardQueue.clear();
    }
};

// ============================================================================
// 7. SORTING ALGORITHMS (QUICKSORT) & SEARCHING
// ============================================================================
namespace DSASorting {
    // Quicksort partition for ParkingSession by entry time descending (most recent first)
    inline int partitionByTimeDesc(std::vector<ParkingSession>& arr, int low, int high) {
        long long pivot = arr[high].entryUnixTime;
        int i = low - 1;
        for (int j = low; j < high; ++j) {
            if (arr[j].entryUnixTime >= pivot) { // descending
                i++;
                std::swap(arr[i], arr[j]);
            }
        }
        std::swap(arr[i + 1], arr[high]);
        return i + 1;
    }

    inline void quickSortByTimeDesc(std::vector<ParkingSession>& arr, int low, int high) {
        if (low < high) {
            int pi = partitionByTimeDesc(arr, low, high);
            quickSortByTimeDesc(arr, low, pi - 1);
            quickSortByTimeDesc(arr, pi + 1, high);
        }
    }

    // Binary search on sorted array of vehicle plates
    inline int binarySearchPlate(const std::vector<ParkingSession>& sortedArr, const std::string& targetPlate) {
        int low = 0;
        int high = static_cast<int>(sortedArr.size()) - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (sortedArr[mid].vehiclePlate == targetPlate) {
                return mid;
            }
            if (sortedArr[mid].vehiclePlate < targetPlate) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return -1;
    }
}

#endif // DSA_STRUCTURES_HPP
