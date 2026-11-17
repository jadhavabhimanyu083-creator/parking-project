# ParkingFlow – Smart Parking Slot Management System
> **Second-Year Engineering Data Structures & Algorithms (DSA) Project**  
> Designed with high fidelity matching modern web UI/UX standards and powered by an authoritative **C++17 native backend**.

---

## 📌 Project Overview
**ParkingFlow** is an end-to-end Smart Parking Slot Management platform designed to solve vehicle congestion, manual allocation delays, and inaccurate bay tracking in modern multi-level facilities.

Unlike standard classroom web apps where business logic is mocked in JavaScript, ParkingFlow features an **authoritative native C++17 backend** implementing genuine DSA modules:
- Real-time **Graph BFS** computes the shortest driving route from Entrance gates to parking bays.
- **2D Arrays / Matrices** model customizable parking layouts (rows × columns).
- **Binary Search Trees (BST)** enable $O(\log n)$ search and alphabetical traversal of vehicle records.
- **Hash Tables** provide instant $O(1)$ license plate and slot lookups.
- **Dual Queues** manage waiting overflow vehicles with FIFO and VIP Priority promotions.
- **Stacks (LIFO)** support multi-level undo operations in the interactive visual layout designer.
- **Doubly Linked Lists** store dynamic chronological parking session audit histories.
- **Quicksort** partitions and ranks vehicle billing records by duration and timestamp.

The frontend presents a clean, modern UI with white and light-grey backgrounds, a dark navy sidebar, yellow highlights, and a 3-step wizard layout designer directly based on the design specification.

---

## 🏗️ System Architecture

```mermaid
flowchart TD
    subgraph Client["Frontend Client (HTML5 / Modern CSS / Vanilla ES6)"]
        UI_Head["Header: Logo, Search, Notifications, Theme, Profile"]
        UI_Wiz["Add New Plot: 3-Step Wizard & Live 2D Preview"]
        UI_Dash["Admin Dashboard & Interactive Bay Grid"]
        UI_Live["Live 2D Twin: BFS Route Highlight"]
        UI_Res["Reservations & Waiting List Management"]
        UI_Rec["Vehicle Records Table & Quicksort Filter"]
        UI_DSA["DSA Telemetry & Viva Testing Mode"]
    end

    subgraph Server["C++17 Authoritative Server (Port 8080)"]
        WINSOCK["WinSock2 Multithreaded HTTP Server"]
        ROUTER["REST API Request Router (/api/...)"]
        
        subgraph DSACore["Authoritative DSA Engine"]
            M2D["2D Matrix: Layout Cell Grid"]
            GRAPH["Graph (Adjacency List) + BFS & DFS"]
            BST["Vehicle BST: O(log n) License Search"]
            HASH["Hash Maps: O(1) Active Sessions"]
            QUE["Waiting Queue: FIFO + Priority Queue"]
            STK["Undo Stack: Layout Cell History"]
            DLL["Doubly Linked List: History Trail"]
            QS["Quicksort: Timestamp & Fee Partitioning"]
        end

        DISK[("JSON Persistence Engine: plots.json, sessions.json")]
    end

    Client <-->|REST Endpoints (JSON)| WINSOCK
    WINSOCK --> ROUTER
    ROUTER --> DSACore
    DSACore <--> DISK
```

---

## 🧠 Data Structures & Algorithms (DSA) Implementation Details

### 1. 2D Array / Matrix (`Matrix2D<T>`)
- **Usage**: Models parking bays, driving aisles, entrances, exits, pillars, and EV stations.
- **Coordinate Mapping**: Each cell has a row and column index $(r, c)$ and unique identifier (e.g. `A1`, `B4`).
- **Time Complexity**:
  - Cell Lookup / Mutation: $O(1)$
  - Dimension Resizing: $O(R \times C)$
- **Space Complexity**: $O(R \times C)$

### 2. Graph (Adjacency List) with BFS & DFS (`ParkingGraph`)
- **BFS (Breadth-First Search)**:
  - Finds the exact shortest driving route from Entrance to any target slot.
  - Used for **Greedy Nearest Slot Allocation**: When a car enters, the system automatically allocates the compatible bay with the lowest BFS distance from the Entrance.
  - Time Complexity: $O(V + E)$ where $V = \text{Rows} \times \text{Cols}$ and $E \le 4V$.
- **DFS (Depth-First Search)**:
  - Analyzes graph connectivity and reachability, alerting the administrator if any parking bay or aisle is blocked off by obstacles or walls.
  - Time Complexity: $O(V + E)$.

### 3. Binary Search Tree (`VehicleBST`)
- **Usage**: Indexes vehicle license plate numbers alphabetically for efficient searching across completed and active sessions.
- **Operations**:
  - Insertion: $O(\log n)$ average, $O(n)$ worst.
  - Search: $O(\log n)$ average, $O(n)$ worst.
  - In-Order Traversal: $O(n)$ yields alphabetically sorted vehicle plates.
- **Space Complexity**: $O(n)$ where each node stores session pointer and left/right child references.

### 4. Hash Table (`std::unordered_map`)
- **Usage**: Maintains $O(1)$ lookup for active vehicles by license plate (`plate -> session`) and slot indexing (`slotId -> Slot`).
- **Prevents Duplicate Entries**: Instant rejection if an active plate already exists.
- **Time Complexity**: $O(1)$ average, $O(n)$ worst.

### 5. Queues for Waiting Vehicles (`WaitingQueueManager`)
- **Usage**: When a plot is 100% full, newly arriving vehicles enter a dual queue structure:
  - **VIP / Emergency Priority Queue**: Prioritized over standard vehicles (e.g., ambulances, emergency staff).
  - **Standard FIFO Queue**: First-come, first-served order.
- **Auto-Promotion**: Whenever an active vehicle exits, the head vehicle is automatically dequeued and parked in the newly freed slot.
- **Time Complexity**: $O(1)$ enqueue and dequeue.

### 6. Stacks for Undo Operations (`UndoStack<T>`)
- **Usage**: The 3-Step Wizard layout designer tracks every cell modification (old type $\to$ new type).
- **Operation**: Clicking the "Undo (Stack)" button pops the last action from the LIFO stack and restores the previous cell state.
- **Time Complexity**: $O(1)$ push and pop.

### 7. Doubly Linked List (`DoublyLinkedList<T>`)
- **Usage**: Chronological audit trail for vehicle check-ins and check-outs.
- **Advantage**: Fast $O(1)$ prepending for incoming vehicles and bidirectional traversal for audit logs.
- **Time Complexity**: $O(1)$ prepend, $O(n)$ sequential display.

### 8. Quicksort & Binary Search (`DSASorting`)
- **Usage**: Partitions and sorts historical records by entry timestamp, parking duration, or fee collected.
- **Time Complexity**: $O(n \log n)$ average, $O(n^2)$ worst case.

---

## 📊 Summary of Time & Space Complexities

| Data Structure / Algorithm | Module / Operation | Time Complexity (Avg) | Time Complexity (Worst) | Space Complexity |
| :--- | :--- | :---: | :---: | :---: |
| **2D Array / Matrix** | Grid cell read / write | $O(1)$ | $O(1)$ | $O(R \times C)$ |
| **Graph BFS** | Shortest path route & nearest bay | $O(V + E)$ | $O(V + E)$ | $O(V)$ |
| **Graph DFS** | Lot connectivity check | $O(V + E)$ | $O(V + E)$ | $O(V)$ |
| **Binary Search Tree** | Vehicle record lookup by plate | $O(\log n)$ | $O(n)$ | $O(n)$ |
| **Hash Table** | Active vehicle duplicate check | $O(1)$ | $O(n)$ | $O(n)$ |
| **Dual Queues** | Waiting list FIFO & VIP enqueue | $O(1)$ | $O(1)$ | $O(k)$ |
| **Stack** | Layout designer Undo | $O(1)$ | $O(1)$ | $O(m)$ |
| **Doubly Linked List**| History log prepending | $O(1)$ | $O(1)$ | $O(n)$ |
| **Quicksort** | Records sorting | $O(n \log n)$ | $O(n^2)$ | $O(\log n)$ |

---

## 🚀 Setup & Execution Guide

### Prerequisites
- **Operating System**: Windows 10 / 11
- **Compiler**: MinGW GCC `g++` (supports `-std=c++17`)
- **Web Browser**: Chrome, Edge, Firefox, or Safari

### Building and Running
1. Open a terminal in the project directory `d:\new projectflow`.
2. Run the automated build script:
   ```cmd
   build_and_run.bat
   ```
   *Alternatively, compile manually with:*
   ```cmd
   g++ -std=c++17 -O2 backend\src\main.cpp backend\src\HttpServer.cpp backend\src\ParkingManager.cpp -o parkingflow_server.exe -lws2_32
   parkingflow_server.exe 8080 public
   ```
3. Open your browser and navigate to:
   ```
   http://localhost:8080
   ```

---

## 🎓 College Viva Voce Question Bank & Defense Guide

**Q1: Why did you use C++17 instead of a standard Node.js or Python backend?**  
*Answer:* C++ provides deterministic memory management, zero-overhead abstractions, and direct implementation of classical data structures (BSTs, adjacency graphs, custom linked lists, and stacks) without relying on high-level garbage-collected runtime abstractions. It allows us to demonstrate genuine DSA algorithms in action.

**Q2: How does the system choose which parking slot to assign to an incoming vehicle?**  
*Answer:* We model the layout as an undirected graph. Using BFS (Breadth-First Search) starting from the Entrance node, we compute the shortest path distance to all available bays compatible with the vehicle type (Car vs. Two-Wheeler). We then apply a greedy selection algorithm choosing the bay with the minimum BFS distance.

**Q3: How does the Undo feature work in the plot designer?**  
*Answer:* It uses a LIFO Stack. Every time a cell's type changes (e.g. from Car Slot to Driving Aisle), a `LayoutAction` struct containing row, column, old type, and new type is pushed onto the stack. When the user clicks Undo, the top element is popped and the cell is restored to its previous type in $O(1)$ time.

**Q4: How do you handle full capacity and waiting vehicles?**  
*Answer:* When no compatible slot is free, the vehicle is inserted into a dual Waiting Queue Manager. Emergency/VIP vehicles are enqueued in a Priority Queue, while regular vehicles are placed in a standard FIFO Queue. When any vehicle checks out, the backend automatically dequeues the waiting vehicle and allocates the freed slot immediately.

**Q5: Is data saved when the server restarts?**  
*Answer:* Yes. On every parking, exit, reservation, and plot creation operation, the state is persisted to disk in the `data/` directory (`plots.json`, `sessions.json`), ensuring data survives server restarts.
#   p a r k i n g - p r o j e c t  
 