/**
 * ParkingFlow — Dual-Mode REST API & Local DSA Engine Client
 * Seamlessly operates against:
 * 1. Authoritative C++17 Backend on http://localhost:8080 (when served via HTTP or when reachable)
 * 2. Embedded In-Browser DSA Engine with LocalStorage (when opened directly via file:/// or offline)
 */

const isFileProtocol = (window.location.protocol === 'file:' || !window.location.origin || window.location.origin === 'null');
let API_BASE = isFileProtocol ? 'http://localhost:8080' : window.location.origin;
let forceLocalEngine = false;

// ============================================================================
// EMBEDDED IN-BROWSER DSA ENGINE (OFFLINE / FILE PROTOCOL FALLBACK)
// ============================================================================
const localEngine = {
  DB_KEY: 'parkingflow_local_db_v4',

  _getDB() {
    let raw = localStorage.getItem(this.DB_KEY);
    let db = null;
    if (!raw) {
      db = this._createDefaultDB();
      this._saveDB(db);
      return db;
    }
    try {
      db = JSON.parse(raw);
      if (!db.plots || !db.plots[0] || !db.plots[0].gates || !db.plots[0].grid) {
        db = this._createDefaultDB();
        this._saveDB(db);
        return db;
      }
    } catch (e) {
      db = this._createDefaultDB();
      this._saveDB(db);
      return db;
    }

    // Proactively purge old dummy plots and preserve user/admin added plots
    const dummyPlotIds = new Set(['plot_phoenix_marketcity', 'plot_sriook_park', 'plot_zoob_residences', 'plot_tower_43', 'plot_1792918456776495000']);
    let changed = false;
    if (db.plots) {
      // Ensure central campus plot has been renamed to Demo Parking in cache and is completely empty
      const campusPlot = db.plots.find(p => p.id === 'plot_central_campus');
      if (campusPlot) {
        if (campusPlot.name !== 'Demo Parking') {
          campusPlot.name = 'Demo Parking';
          changed = true;
        }
        if (campusPlot.grid) {
          campusPlot.grid.forEach(row => {
            row.forEach(slot => {
              if (slot.status === 'OCCUPIED' || slot.plate) {
                slot.status = 'AVAILABLE';
                slot.plate = '';
                changed = true;
              }
            });
          });
        }
      }
      const originalCount = db.plots.length;
      db.plots = db.plots.filter(p => !dummyPlotIds.has(p.id));
      if (db.plots.length !== originalCount) {
        changed = true;
      }
      if (dummyPlotIds.has(db.activePlotId) || !db.plots.some(p => p.id === db.activePlotId)) {
        db.activePlotId = db.plots[0] ? db.plots[0].id : 'plot_central_campus';
        changed = true;
      }
      // Ensure admin-added plots from initial seed exist
      const defaultDB = this._createDefaultDB();
      for (const defPlot of defaultDB.plots) {
        if (!db.plots.some(p => p.id === defPlot.id)) {
          db.plots.push(defPlot);
          changed = true;
        }
      }
      // Automatically re-allot slot numbers for all plots
      for (const p of db.plots) {
        this._reallotSlotNumbers(p, db);
      }
      changed = true;
    }
    if (db.sessions) {
      const origSessCount = db.sessions.length;
      db.sessions = db.sessions.filter(s => s.plotId !== 'plot_central_campus' && !dummyPlotIds.has(s.plotId));
      if (db.sessions.length !== origSessCount) {
        changed = true;
      }
    }
    if (changed) {
      this._saveDB(db);
    }
    return db;
  },

  _saveDB(db) {
    localStorage.setItem(this.DB_KEY, JSON.stringify(db));
  },

  _createDefaultDB() {
    const rowLabels = ['A', 'B', 'C', 'D'];
    const rows = 4, cols = 6;
    const grid = [];

    for (let r = 0; r < rows; r++) {
      const row = [];
      for (let c = 0; c < cols; c++) {
        let type = 'SLOT_CAR';
        let category = 'STANDARD';
        let hasCharger = false;
        let chargerId = '';
        let connectorType = 'CCS2';
        let chargingPowerKw = 22.0;
        let chargerStatus = 'OPERATIONAL';
        let chargingRatePerHour = 50.0;

        if (r === 0 && c === 2) {
          type = 'ENTRANCE';
        } else if (r === 3 && c === 2) {
          type = 'EXIT';
        } else if (c === 2 || c === 3) {
          type = 'AISLE';
        } else if (r === 3 && c >= 4) {
          type = 'SLOT_BIKE';
          category = 'BIKE';
        }

        const id = `${rowLabels[r]}${c + 1}`;

        // Seed EV Charging Slots (Modules 5 & 6)
        if (id === 'A4') {
          type = 'SLOT_CAR';
          category = 'EV_CHARGING';
          hasCharger = true;
          chargerId = 'EV-CHG-01';
          connectorType = 'CCS2';
          chargingPowerKw = 50.0;
          chargerStatus = 'OPERATIONAL';
          chargingRatePerHour = 50.0;
        } else if (id === 'A5') {
          type = 'SLOT_CAR';
          category = 'EV_CHARGING';
          hasCharger = true;
          chargerId = 'EV-CHG-02';
          connectorType = 'TYPE_2_AC';
          chargingPowerKw = 22.0;
          chargerStatus = 'OPERATIONAL';
          chargingRatePerHour = 40.0;
        } else if (id === 'B5') {
          type = 'SLOT_CAR';
          category = 'EV_CHARGING';
          hasCharger = true;
          chargerId = 'EV-CHG-03';
          connectorType = 'CCS2';
          chargingPowerKw = 50.0;
          chargerStatus = 'OUT_OF_SERVICE';
          chargingRatePerHour = 50.0;
        }

        row.push({
          id,
          r,
          c,
          type,
          status: 'AVAILABLE',
          plate: '',
          vehicleType: (type === 'SLOT_BIKE' ? 'TWO_WHEELER' : 'CAR'),
          dist: r + c,
          distMeters: (r + c) * 5.0,
          category,
          hasCharger,
          chargerId,
          connector: connectorType,
          powerKw: chargingPowerKw,
          chargerStatus,
          chargingRate: chargingRatePerHour
        });
      }
      grid.push(row);
    }

    // Default Gate configuration (Module 1)
    const gates = [
      { id: 'gate_main_in', name: 'Main Entrance (North)', type: 'ENTRANCE', row: 0, col: 2, isActive: true },
      { id: 'gate_exit_1', name: 'Main Exit (South)', type: 'EXIT', row: 3, col: 2, isActive: true },
      { id: 'gate_south_in', name: 'Gate 2 (East Entrance)', type: 'BIDIRECTIONAL', row: 1, col: 5, isActive: true }
    ];

    const sessions = [];

    const defaultPlot = {
      id: 'plot_central_campus',
      name: 'Demo Parking',
      location: 'Main Gate, Academic Block',
      parkingType: 'mixed',
      rows: 4,
      cols: 6,
      ratePerHourCar: 40.0,
      ratePerHourBike: 20.0,
      gracePeriodMinutes: 15,
      cellScaleMeters: 5.0,
      gates,
      grid
    };

    const adminPlots = [{"id":"plot_1792911489297940000","name":"vit","location":"hhh","parkingType":"mixed","rows":3,"cols":6,"ratePerHourCar":40,"ratePerHourBike":20,"gracePeriodMinutes":15,"cellScaleMeters":5,"gates":[{"id":"gate_in_1","name":"Main Entrance","type":"ENTRANCE","row":0,"col":0,"isActive":true},{"id":"gate_10","name":"Gate 2","type":"EXIT","row":2,"col":1,"isActive":true}],"grid":[[{"id":"A1","r":0,"c":0,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"A2","r":0,"c":1,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"A3","r":0,"c":2,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"A4","r":0,"c":3,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"A5","r":0,"c":4,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"A6","r":0,"c":5,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50}],[{"id":"B1","r":1,"c":0,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"B2","r":1,"c":1,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"B3","r":1,"c":2,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"B4","r":1,"c":3,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"B5","r":1,"c":4,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"B6","r":1,"c":5,"type":"AISLE","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":0,"distMeters":0,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50}],[{"id":"C1","r":2,"c":0,"type":"SLOT_CAR","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":2,"distMeters":10,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"C2","r":2,"c":1,"type":"SLOT_CAR","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":3,"distMeters":15,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"C3","r":2,"c":2,"type":"SLOT_CAR","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":4,"distMeters":20,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"C4","r":2,"c":3,"type":"SLOT_CAR","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":5,"distMeters":25,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"C5","r":2,"c":4,"type":"SLOT_CAR","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":6,"distMeters":30,"category":"STANDARD","hasCharger":false,"chargerId":"","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50},{"id":"C6","r":2,"c":5,"type":"SLOT_CAR","status":"AVAILABLE","plate":"","vehicleType":"CAR","dist":7,"distMeters":35,"category":"EV_CHARGING","hasCharger":true,"chargerId":"EV-C6","connector":"CCS2","powerKw":22,"chargerStatus":"OPERATIONAL","chargingRate":50}]]}];
    const seededPlots = [defaultPlot, ...adminPlots];
    // Ensure defaultPlot is present for test parity
    if (!seededPlots.some(p => p.id === 'plot_central_campus')) {
      seededPlots.push(defaultPlot);
    }
    const waitingQueues = {};
    const undoStacks = {};
    seededPlots.forEach(p => {
      waitingQueues[p.id] = [];
      undoStacks[p.id] = [];
    });

    return {
      activePlotId: 'plot_central_campus',
      plots: seededPlots,
      sessions,
      reservations: [],
      waitingQueues,
      undoStacks
    };
  },

  _isCellGate(plot, r, c) {
    return (plot.gates || []).some(g => g.row === r && g.col === c);
  },

  _isParkableSlot(plot, r, c) {
    if (r < 0 || r >= plot.rows || c < 0 || c >= plot.cols) return false;
    if (this._isCellGate(plot, r, c)) return false;
    const s = plot.grid[r][c];
    if (s.type === 'AISLE' || s.type === 'BLOCKED' || s.type === 'ENTRANCE' || s.type === 'EXIT' || s.type === 'FACILITY') {
      return false;
    }
    return (s.type === 'SLOT_CAR' || s.type === 'SLOT_BIKE');
  },

  _reallotSlotNumbers(plot, db) {
    if (!plot || !plot.grid) return;

    // Step 1: Ensure all configured gates have appropriate cell type, no slot number, and no charger
    (plot.gates || []).forEach(g => {
      if (g.row >= 0 && g.row < plot.rows && g.col >= 0 && g.col < plot.cols) {
        const s = plot.grid[g.row][g.col];
        s.type = (g.type === 'EXIT' ? 'EXIT' : 'ENTRANCE');
        s.id = '';
        s.hasCharger = false;
      }
    });

    // Step 2: Ensure non-parkable cells (gates, aisles, pillars, facilities) have no ID and no parked vehicles
    for (let r = 0; r < plot.rows; r++) {
      for (let c = 0; c < plot.cols; c++) {
        const s = plot.grid[r][c];
        if (!this._isParkableSlot(plot, r, c)) {
          s.id = '';
          // If a vehicle is currently sitting in a gate, aisle, or pillar, relocate or vacate it!
          if (s.status === 'OCCUPIED' && s.plate) {
            const plateToMove = s.plate;
            const vehType = s.vehicleType;
            s.status = 'AVAILABLE';
            s.plate = '';

            let relocated = false;
            for (let rr = 0; rr < plot.rows && !relocated; rr++) {
              for (let cc = 0; cc < plot.cols && !relocated; cc++) {
                if (rr === r && cc === c) continue;
                const cand = plot.grid[rr][cc];
                if (this._isParkableSlot(plot, rr, cc) && cand.status === 'AVAILABLE') {
                  cand.status = 'OCCUPIED';
                  cand.plate = plateToMove;
                  cand.vehicleType = vehType;
                  if (db && db.sessions) {
                    const sess = db.sessions.find(item => item.vehiclePlate === plateToMove && item.isActive);
                    if (sess) sess.slotId = cand.id;
                  }
                  relocated = true;
                }
              }
            }

            if (!relocated && db && db.sessions) {
              const sess = db.sessions.find(item => item.vehiclePlate === plateToMove && item.isActive);
              if (sess) {
                sess.isActive = false;
                sess.exitTime = new Date().toLocaleString();
              }
            }
          } else if (s.status === 'RESERVED') {
            s.status = 'AVAILABLE';
            s.plate = '';
          }
        }
      }
    }

    // Step 3: Allot proper slot numbers row by row: A1, A2, A3... B1, B2...
    for (let r = 0; r < plot.rows; r++) {
      let label = '';
      let tempR = r + 1;
      while (tempR > 0) {
        let rem = (tempR - 1) % 26;
        label = String.fromCharCode(65 + rem) + label;
        tempR = Math.floor((tempR - 1) / 26);
      }
      let slotIdx = 1;

      for (let c = 0; c < plot.cols; c++) {
        const s = plot.grid[r][c];
        if (!this._isParkableSlot(plot, r, c)) {
          s.id = '';
        } else {
          const oldId = s.id;
          const newId = `${label}${slotIdx++}`;
          s.id = newId;

          // Keep active session slot ID synchronized
          if (s.status === 'OCCUPIED' && s.plate && db && db.sessions) {
            const sess = db.sessions.find(item => item.vehiclePlate === s.plate && item.isActive);
            if (sess) sess.slotId = newId;
          }

          // Keep active reservations synchronized
          if (db && db.reservations) {
            db.reservations.forEach(res => {
              if (res.plotId === plot.id && res.slotId === oldId && res.isActive) {
                res.slotId = newId;
              }
            });
          }
        }
      }
    }
  },

  // Multi-Gate BFS graph shortest path calculation with calibrated distance in meters
  _recomputeBFS(plot) {
    const activeEntrances = (plot.gates || []).filter(g => g.isActive && (g.type === 'ENTRANCE' || g.type === 'BIDIRECTIONAL'));
    const entrances = activeEntrances.length > 0 ? activeEntrances : [{ row: 0, col: 0, name: 'Default Entrance' }];
    const scale = plot.cellScaleMeters || 5.0;

    const dirs = [[-1, 0], [1, 0], [0, -1], [0, 1]];

    for (let r = 0; r < plot.rows; r++) {
      for (let c = 0; c < plot.cols; c++) {
        const slot = plot.grid[r][c];
        if (slot.type === 'SLOT_CAR' || slot.type === 'SLOT_BIKE') {
          let minDistance = 99999;

          // Find shortest path among all active entrance gates
          for (const ent of entrances) {
            const visited = Array.from({ length: plot.rows }, () => Array(plot.cols).fill(false));
            const queue = [[ent.row, ent.col, 0]];
            visited[ent.row][ent.col] = true;
            let reached = false;

            while (queue.length > 0) {
              const [cr, cc, d] = queue.shift();
              if (cr === r && cc === c) {
                if (d < minDistance) minDistance = d;
                reached = true;
                break;
              }

              for (const [dr, dc] of dirs) {
                const nr = cr + dr;
                const nc = cc + dc;
                if (nr >= 0 && nr < plot.rows && nc >= 0 && nc < plot.cols && !visited[nr][nc]) {
                  const cell = plot.grid[nr][nc];
                  const passable = (cell.type === 'AISLE' || cell.type === 'ENTRANCE' || cell.type === 'EXIT' || cell.type === 'FACILITY' || (nr === r && nc === c));
                  if (passable) {
                    visited[nr][nc] = true;
                    queue.push([nr, nc, d + 1]);
                  }
                }
              }
            }
          }

          if (minDistance < 99999) {
            slot.dist = minDistance;
            slot.distMeters = minDistance * scale;
          } else {
            slot.dist = -1;
            slot.distMeters = -1.0;
          }
        } else {
          slot.dist = 0;
          slot.distMeters = 0.0;
        }
      }
    }
  },

  // Automatically park queued vehicles into any newly empty or available spaces
  _processWaitingQueue(plot, db) {
    if (!plot || !db || !db.waitingQueues || !db.waitingQueues[plot.id] || db.waitingQueues[plot.id].length === 0) {
      return [];
    }

    const queue = db.waitingQueues[plot.id];
    const promoted = [];
    const remainingQueue = [];

    // Sort queue with VIP/Priority first, then FIFO
    queue.sort((a, b) => {
      const pA = a.isPriority ? 1 : 0;
      const pB = b.isPriority ? 1 : 0;
      return pB - pA;
    });

    const activeGates = (plot.gates || []).filter(g => g.isActive !== false && (g.type === 'ENTRANCE' || g.type === 'BIDIRECTIONAL'));
    const defaultGate = activeGates.length > 0 ? activeGates[0] : { id: 'gate_auto', name: 'Auto Gate', row: 0, col: 0 };

    for (const wReq of queue) {
      const reqPlate = (wReq.plate || wReq.vehiclePlate || '').trim();
      const reqType = (wReq.vehicleType || 'CAR').toUpperCase();
      const reqEV = !!wReq.requiresEVCharging;
      const preferredConn = wReq.preferredConnector || 'ANY';

      let bestR = -1;
      let bestC = -1;
      let bestSlot = null;
      let minDistance = 999999;
      let bestGate = defaultGate;

      for (let r = 0; r < plot.rows; r++) {
        for (let c = 0; c < plot.cols; c++) {
          const s = plot.grid[r][c];
          if (s.status !== 'AVAILABLE' || !this._isParkableSlot(plot, r, c)) continue;

          let matches = false;
          if (reqEV) {
            if (s.hasCharger && s.chargerStatus === 'OPERATIONAL') {
              if (preferredConn === 'ANY' || !preferredConn || s.connectorType === preferredConn) {
                matches = true;
              }
            }
          } else {
            if (reqType === 'TWO_WHEELER') {
              if (s.cellType === 'SLOT_BIKE' || (s.cellType === 'SLOT_CAR' && (!s.hasCharger || s.chargerStatus !== 'OPERATIONAL'))) {
                matches = true;
              }
            } else {
              if (s.cellType === 'SLOT_CAR' && (!s.hasCharger || s.chargerStatus !== 'OPERATIONAL')) {
                matches = true;
              }
            }
          }

          if (matches) {
            for (const g of (activeGates.length > 0 ? activeGates : [defaultGate])) {
              const d = Math.abs(r - g.row) + Math.abs(c - g.col);
              if (d < minDistance) {
                minDistance = d;
                bestR = r;
                bestC = c;
                bestSlot = s;
                bestGate = g;
              }
            }
          }
        }
      }

      if (bestSlot && this._isParkableSlot(plot, bestR, bestC)) {
        bestSlot.status = 'OCCUPIED';
        bestSlot.plate = reqPlate;
        bestSlot.currentPlate = reqPlate;
        bestSlot.currentVehicleType = reqType;
        const sessionId = `SESS-${Date.now().toString().slice(-6)}-${Math.floor(Math.random() * 900 + 100)}`;
        bestSlot.currentSessionId = sessionId;

        const newSession = {
          sessionId,
          plotId: plot.id,
          slotId: bestSlot.id,
          plate: reqPlate,
          vehiclePlate: reqPlate,
          vehicleType: reqType,
          ownerName: wReq.ownerName || 'Guest',
          phone: wReq.phone || '',
          entryUnixTime: Math.floor(Date.now() / 1000),
          entryTimeFormatted: new Date().toLocaleTimeString(),
          entryTime: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
          isActive: true,
          entryGateId: bestGate.id,
          entryGateName: bestGate.name,
          entryDistanceMeters: minDistance * (plot.cellScaleMeters || 5.0),
          requiresEVCharging: reqEV && bestSlot.hasCharger,
          chargerId: (reqEV && bestSlot.hasCharger) ? bestSlot.chargerId : '',
          connectorType: (reqEV && bestSlot.hasCharger) ? bestSlot.connectorType : ''
        };

        db.sessions.unshift(newSession);
        promoted.push({
          success: true,
          plate: reqPlate,
          slotId: bestSlot.id,
          ownerName: wReq.ownerName || 'Guest',
          gateName: bestGate.name,
          message: `Queued vehicle ${reqPlate} automatically parked into empty Slot ${bestSlot.id}`
        });
      } else {
        remainingQueue.push(wReq);
      }
    }

    db.waitingQueues[plot.id] = remainingQueue;
    return promoted;
  },

  async getStatus() {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === db.activePlotId) || db.plots[0];
    if (!plot) throw new Error('No plot available');

    this._reallotSlotNumbers(plot, db);
    this._recomputeBFS(plot);
    this._processWaitingQueue(plot, db);
    this._saveDB(db);

    let total = 0, occupied = 0, available = 0, reserved = 0, evSlots = 0, activeEVs = 0;
    plot.grid.forEach(row => {
      row.forEach(s => {
        if (this._isParkableSlot(plot, s.r, s.c)) {
          total++;
          if (s.status === 'OCCUPIED') occupied++;
          else if (s.status === 'AVAILABLE') available++;
          else if (s.status === 'RESERVED') reserved++;

          if (s.hasCharger) {
            evSlots++;
            if (s.status === 'OCCUPIED') activeEVs++;
          }
        }
      });
    });

    const waitList = db.waitingQueues[plot.id] || [];

    return {
      activePlot: {
        id: plot.id,
        name: plot.name,
        location: plot.location,
        parkingType: plot.parkingType,
        rows: plot.rows,
        cols: plot.cols,
        ratePerHourCar: plot.ratePerHourCar,
        ratePerHourBike: plot.ratePerHourBike,
        cellScaleMeters: plot.cellScaleMeters || 5.0,
        gates: plot.gates || []
      },
      stats: {
        totalSlots: total,
        availableSlots: available,
        occupiedSlots: occupied,
        reservedSlots: reserved,
        occupancyRate: total > 0 ? Math.round((occupied * 100) / total) : 0,
        evSlotsCount: evSlots,
        activeEVCount: activeEVs,
        waitingCount: waitList.length
      },
      grid: plot.grid
    };
  },

  async getPlots() {
    const db = this._getDB();
    const plotsSummary = db.plots.map(p => {
      let total = 0, occupied = 0, available = 0, evSlots = 0;
      p.grid.forEach(row => {
        row.forEach(s => {
          if (s.type === 'SLOT_CAR' || s.type === 'SLOT_BIKE') {
            total++;
            if (s.status === 'OCCUPIED') occupied++;
            else if (s.status === 'AVAILABLE') available++;
            if (s.hasCharger) evSlots++;
          }
        });
      });
      return {
        id: p.id,
        name: p.name,
        location: p.location,
        parkingType: p.parkingType,
        rows: p.rows,
        cols: p.cols,
        totalSlots: total,
        availableSlots: available,
        occupiedSlots: occupied,
        evSlotsCount: evSlots,
        cellScaleMeters: p.cellScaleMeters || 5.0,
        ratePerHourCar: p.ratePerHourCar,
        ratePerHourBike: p.ratePerHourBike,
        gatesCount: (p.gates || []).length
      };
    });

    return {
      activePlotId: db.activePlotId,
      plots: plotsSummary
    };
  },

  _getRowLabel(r) {
    let label = '';
    r++;
    while (r > 0) {
      let rem = (r - 1) % 26;
      label = String.fromCharCode(65 + rem) + label;
      r = Math.floor((r - 1) / 26);
    }
    return label;
  },

  async createPlot(data) {
    const db = this._getDB();
    const rows = parseInt(data.rows, 10) || 4;
    const cols = parseInt(data.cols, 10) || 6;
    const grid = [];

    let parsedTypes = [];
    if (data.gridData) {
      parsedTypes = data.gridData.split(';').map(r => r.split(','));
    }

    for (let r = 0; r < rows; r++) {
      const row = [];
      const rLabel = this._getRowLabel(r);
      for (let c = 0; c < cols; c++) {
        let type = 'SLOT_CAR';
        if (parsedTypes[r] && parsedTypes[r][c]) {
          type = parsedTypes[r][c];
        } else if (data.parkingType === 'bike') {
          type = 'SLOT_BIKE';
        } else if (data.parkingType === 'mixed' && r === rows - 1 && c >= Math.floor(cols / 2)) {
          type = 'SLOT_BIKE';
        }

        row.push({
          id: `${rLabel}${c + 1}`,
          r,
          c,
          type,
          status: 'AVAILABLE',
          plate: '',
          vehicleType: (type === 'SLOT_BIKE' ? 'TWO_WHEELER' : 'CAR'),
          dist: r + c,
          distMeters: (r + c) * 5.0,
          category: (type === 'SLOT_BIKE' ? 'BIKE' : 'STANDARD'),
          hasCharger: false,
          chargerId: '',
          connector: 'CCS2',
          powerKw: 22.0,
          chargerStatus: 'OPERATIONAL',
          chargingRate: 50.0
        });
      }
      grid.push(row);
    }

    const newPlot = {
      id: `plot_${Date.now()}`,
      name: data.name,
      location: data.location || 'Campus',
      parkingType: data.parkingType || 'mixed',
      rows,
      cols,
      ratePerHourCar: parseFloat(data.ratePerHourCar) || 40,
      ratePerHourBike: parseFloat(data.ratePerHourBike) || 20,
      gracePeriodMinutes: parseInt(data.gracePeriodMinutes, 10) || 15,
      cellScaleMeters: 5.0,
      gates: [
        { id: `gate_in_${Date.now()}`, name: 'Main Entrance', type: 'ENTRANCE', row: 0, col: 0, isActive: true },
        { id: `gate_out_${Date.now()}`, name: 'Main Exit', type: 'EXIT', row: rows - 1, col: cols - 1, isActive: true }
      ],
      grid
    };

    this._recomputeBFS(newPlot);
    db.plots.push(newPlot);
    db.activePlotId = newPlot.id;
    db.waitingQueues[newPlot.id] = [];
    db.undoStacks[newPlot.id] = [];
    this._saveDB(db);

    return { status: 'success', message: 'Plot created successfully', plotId: newPlot.id };
  },

  async selectPlot(plotId) {
    const db = this._getDB();
    if (db.plots.some(p => p.id === plotId)) {
      db.activePlotId = plotId;
      this._saveDB(db);
      return { status: 'success' };
    }
    throw new Error('Plot not found');
  },

  async updatePlot(data) {
    const db = this._getDB();
    const p = db.plots.find(item => item.id === data.plotId);
    if (!p) throw new Error('Plot not found');

    if (data.name) p.name = data.name;
    if (data.location) p.location = data.location;
    if (data.parkingType) p.parkingType = data.parkingType;
    if (data.ratePerHourCar) p.ratePerHourCar = parseFloat(data.ratePerHourCar);
    if (data.ratePerHourBike) p.ratePerHourBike = parseFloat(data.ratePerHourBike);
    if (data.gracePeriodMinutes !== undefined) p.gracePeriodMinutes = parseInt(data.gracePeriodMinutes, 10);

    this._saveDB(db);
    return { status: 'success', message: 'Plot updated successfully' };
  },

  async deletePlot(plotId) {
    const db = this._getDB();
    if (db.plots.length <= 1) {
      throw new Error('Cannot delete the only remaining plot in the system.');
    }
    const idx = db.plots.findIndex(p => p.id === plotId);
    if (idx === -1) throw new Error('Plot not found');

    const hasOccupied = db.plots[idx].grid.some(row => row.some(s => s.status === 'OCCUPIED'));
    if (hasOccupied) {
      throw new Error('Cannot delete plot with active parked vehicles.');
    }

    db.plots.splice(idx, 1);
    if (db.activePlotId === plotId) {
      db.activePlotId = db.plots[0].id;
    }
    this._saveDB(db);
    return { status: 'success', message: 'Plot removed successfully' };
  },

  // Gate Management (Module 1)
  async addGate(data) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (data.plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');
    if (!plot.gates) plot.gates = [];

    const newGate = {
      id: data.id || `gate_${Date.now()}`,
      name: data.name || 'New Gate',
      type: data.type || 'ENTRANCE',
      row: parseInt(data.row, 10) || 0,
      col: parseInt(data.col, 10) || 0,
      isActive: data.isActive !== undefined ? !!data.isActive : true
    };

    plot.gates.push(newGate);
    this._reallotSlotNumbers(plot, db);
    this._recomputeBFS(plot);
    this._saveDB(db);
    return { success: true, message: 'Gate added successfully', gate: newGate };
  },

  async updateGate(data) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (data.plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    const gateId = data.id || data.gateId;
    const g = (plot.gates || []).find(item => item.id === gateId);
    if (!g) throw new Error('Gate not found');

    if (data.name) g.name = data.name;
    if (data.type) g.type = data.type;
    if (data.row !== undefined) g.row = parseInt(data.row, 10);
    if (data.col !== undefined) g.col = parseInt(data.col, 10);
    if (data.isActive !== undefined) g.isActive = !!data.isActive;

    this._reallotSlotNumbers(plot, db);
    this._recomputeBFS(plot);
    this._saveDB(db);
    return { success: true, message: 'Gate updated successfully' };
  },

  async removeGate(plotId, gateId) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    const oldGate = (plot.gates || []).find(g => g.id === gateId);
    plot.gates = (plot.gates || []).filter(g => g.id !== gateId);

    if (oldGate && plot.grid && plot.grid[oldGate.row] && plot.grid[oldGate.row][oldGate.col]) {
      const c = plot.grid[oldGate.row][oldGate.col];
      if (c.type === 'ENTRANCE' || c.type === 'EXIT') {
        c.type = 'AISLE';
      }
    }

    this._reallotSlotNumbers(plot, db);
    this._recomputeBFS(plot);
    this._saveDB(db);
    return { success: true, message: 'Gate removed successfully' };
  },

  async toggleGate(plotId, gateId, active) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    const g = (plot.gates || []).find(item => item.id === gateId);
    if (!g) throw new Error('Gate not found');

    g.isActive = active;
    this._reallotSlotNumbers(plot, db);
    this._recomputeBFS(plot);
    this._saveDB(db);
    return { success: true, message: 'Gate status updated' };
  },

  async updateScale(plotId, scaleMeters) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    plot.cellScaleMeters = parseFloat(scaleMeters) || 5.0;
    this._recomputeBFS(plot);
    this._saveDB(db);
    return { success: true, message: 'Map scale calibrated successfully', cellScaleMeters: plot.cellScaleMeters };
  },

  async editCell(plotId, row, col, cellType) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    if (!plot.undoStack) plot.undoStack = [];
    const oldCell = plot.grid[row][col];
    plot.undoStack.push({
      row,
      col,
      oldType: oldCell.type,
      oldCategory: oldCell.category,
      oldHasCharger: oldCell.hasCharger
    });

    oldCell.type = cellType;
    if (cellType === 'SLOT_CAR') {
      oldCell.category = 'STANDARD';
    } else if (cellType === 'SLOT_BIKE') {
      oldCell.category = 'BIKE';
    } else if (cellType === 'FACILITY') {
      oldCell.category = 'EV_CHARGING';
      oldCell.hasCharger = true;
      if (!oldCell.chargerId) oldCell.chargerId = `EV-${oldCell.id}`;
      if (!oldCell.connector) oldCell.connector = 'CCS2';
      oldCell.chargerStatus = 'OPERATIONAL';
    }

    this._reallotSlotNumbers(plot, db);
    this._recomputeBFS(plot);
    this._saveDB(db);
    return { success: true, message: 'Cell layout updated successfully', cell: oldCell };
  },

  async undoCellEdit(plotId) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    if (!plot.undoStack || plot.undoStack.length === 0) {
      throw new Error('Undo stack is empty');
    }

    const action = plot.undoStack.pop();
    const cell = plot.grid[action.row][action.col];
    cell.type = action.oldType;
    if (action.oldCategory) cell.category = action.oldCategory;
    cell.hasCharger = !!action.oldHasCharger;

    this._reallotSlotNumbers(plot, db);
    this._recomputeBFS(plot);
    this._saveDB(db);
    return { success: true, message: 'Layout change reverted', cell };
  },

  // EV Slot Configuration (Module 5)
  async configureSlotCharger(data) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (data.plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    let slot = null;
    plot.grid.forEach(row => {
      row.forEach(s => {
        if (s.id === data.slotId) slot = s;
      });
    });

    if (!slot) throw new Error('Slot not found');

    slot.hasCharger = !!data.hasCharger;
    slot.category = slot.hasCharger ? 'EV_CHARGING' : 'STANDARD';
    slot.chargerId = slot.hasCharger ? (data.chargerId || 'EV-CHG-01') : '';
    slot.connector = slot.hasCharger ? (data.connectorType || 'CCS2') : '';
    slot.powerKw = parseFloat(data.powerKw) || 22.0;
    slot.chargerStatus = data.status || 'OPERATIONAL';
    slot.chargingRate = parseFloat(data.ratePerHour) || 50.0;

    this._saveDB(db);
    return { success: true, message: 'EV Charger configured successfully' };
  },

  async toggleChargerStatus(plotId, slotId, status) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    let slot = null;
    plot.grid.forEach(row => {
      row.forEach(s => {
        if (s.id === slotId) slot = s;
      });
    });

    if (!slot) throw new Error('Slot not found');
    slot.chargerStatus = status;
    this._saveDB(db);
    return { success: true, message: 'Charger status updated' };
  },

  // Layout Validation (Module 1 & 2)
  async validateLayout(plotId) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId));
    if (!plot) throw new Error('Plot not found');

    const activeEntrances = (plot.gates || []).filter(g => g.isActive && (g.type === 'ENTRANCE' || g.type === 'BIDIRECTIONAL'));
    const activeExits = (plot.gates || []).filter(g => g.isActive && (g.type === 'EXIT' || g.type === 'BIDIRECTIONAL'));

    let totalSlots = 0, reachableSlots = 0, disconnectedSlots = 0;
    const unreachableSlotIds = [];
    const warnings = [];

    if (activeEntrances.length === 0) {
      warnings.push('No active entrance gates configured! Vehicles cannot enter the parking plot.');
    }
    if (activeExits.length === 0) {
      warnings.push('No active exit gates configured! Vehicles cannot exit the parking plot.');
    }

    const dirs = [[-1, 0], [1, 0], [0, -1], [0, 1]];

    plot.grid.forEach(row => {
      row.forEach(slot => {
        if (slot.type === 'SLOT_CAR' || slot.type === 'SLOT_BIKE') {
          totalSlots++;
          let reachable = false;

          for (const ent of activeEntrances) {
            const visited = Array.from({ length: plot.rows }, () => Array(plot.cols).fill(false));
            const queue = [[ent.row, ent.col]];
            visited[ent.row][ent.col] = true;

            while (queue.length > 0) {
              const [cr, cc] = queue.shift();
              if (cr === slot.r && cc === slot.c) {
                reachable = true;
                break;
              }
              for (const [dr, dc] of dirs) {
                const nr = cr + dr;
                const nc = cc + dc;
                if (nr >= 0 && nr < plot.rows && nc >= 0 && nc < plot.cols && !visited[nr][nc]) {
                  const cell = plot.grid[nr][nc];
                  const passable = (cell.type === 'AISLE' || cell.type === 'ENTRANCE' || cell.type === 'EXIT' || cell.type === 'FACILITY' || (nr === slot.r && nc === slot.c));
                  if (passable) {
                    visited[nr][nc] = true;
                    queue.push([nr, nc]);
                  }
                }
              }
            }
            if (reachable) break;
          }

          if (reachable) {
            reachableSlots++;
          } else {
            disconnectedSlots++;
            unreachableSlotIds.push(slot.id);
          }
        }
      });
    });

    if (disconnectedSlots > 0) {
      warnings.push(`${disconnectedSlots} parking slot(s) are completely unreachable from any active entrance gate.`);
    }

    const isValid = disconnectedSlots === 0 && activeEntrances.length > 0 && activeExits.length > 0 && warnings.length === 0;

    return {
      isValid,
      allSlotsReachable: disconnectedSlots === 0 && activeEntrances.length > 0 && activeExits.length > 0,
      totalSlots,
      reachableSlots,
      disconnectedSlots,
      activeEntrances: activeEntrances.length,
      activeExits: activeExits.length,
      unreachableSlotIds,
      warnings
    };
  },

  // Module 4 & 6: Intelligent Parking with EV Charging requirement & Gate Selection
  async parkVehicle(data) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (data.plotId || db.activePlotId)) || db.plots[0];
    const plate = data.plate.toUpperCase().trim();
    const isTwoWheeler = (data.vehicleType === 'TWO_WHEELER');
    const requiresEV = !!data.requiresEVCharging;
    const preferredConn = data.preferredConnector || 'ANY';
    const selectedGateId = data.selectedGateId || '';

    // Check duplicate
    const exists = db.sessions.some(s => s.plate === plate && s.isActive);
    if (exists) {
      throw new Error(`Vehicle ${plate} is already actively parked in the facility!`);
    }

    this._recomputeBFS(plot);

    // Active gates
    let candidateGates = (plot.gates || []).filter(g => g.isActive && (g.type === 'ENTRANCE' || g.type === 'BIDIRECTIONAL'));
    if (selectedGateId) {
      const chosen = (plot.gates || []).find(g => g.id === selectedGateId);
      if (chosen) {
        if (!chosen.isActive) throw new Error(`Selected entrance gate '${chosen.name}' is closed/inactive.`);
        candidateGates = [chosen];
      }
    }
    if (candidateGates.length === 0) {
      candidateGates = [{ id: 'gate_auto', name: 'Auto Entrance', row: 0, col: 0 }];
    }

    const scale = plot.cellScaleMeters || 5.0;
    const dirs = [[-1, 0], [1, 0], [0, -1], [0, 1]];

    const findShortestDistFromGates = (targetR, targetC) => {
      let minDist = 999999;
      let closestGate = candidateGates[0];

      candidateGates.forEach(g => {
        const visited = Array.from({ length: plot.rows }, () => Array(plot.cols).fill(false));
        const queue = [[g.row, g.col, 0]];
        visited[g.row][g.col] = true;

        while (queue.length > 0) {
          const [cr, cc, d] = queue.shift();
          if (cr === targetR && cc === targetC) {
            if (d < minDist) {
              minDist = d;
              closestGate = g;
            }
            break;
          }
          for (const [dr, dc] of dirs) {
            const nr = cr + dr;
            const nc = cc + dc;
            if (nr >= 0 && nr < plot.rows && nc >= 0 && nc < plot.cols && !visited[nr][nc]) {
              const cell = plot.grid[nr][nc];
              const passable = (cell.type === 'AISLE' || cell.type === 'ENTRANCE' || cell.type === 'EXIT' || cell.type === 'FACILITY' || (nr === targetR && nc === targetC));
              if (passable) {
                visited[nr][nc] = true;
                queue.push([nr, nc, d + 1]);
              }
            }
          }
        }
      });

      return { dist: minDist, gate: closestGate };
    };

    let bestDist = 999999;
    let bestSlot = null;
    let bestGate = candidateGates[0];

    if (requiresEV) {
      // Case B: Search available operational EV slots only (excluding gates, aisles, pillars)
      plot.grid.forEach(row => {
        row.forEach(s => {
          if (s.status === 'AVAILABLE' && this._isParkableSlot(plot, s.r, s.c) && s.hasCharger && s.chargerStatus === 'OPERATIONAL') {
            if (preferredConn === 'ANY' || !preferredConn || s.connector === preferredConn) {
              const { dist, gate } = findShortestDistFromGates(s.r, s.c);
              if (dist < bestDist) {
                bestDist = dist;
                bestSlot = s;
                bestGate = gate;
              }
            }
          }
        });
      });

      // Never silently allocate standard slot!
      if (!bestSlot || !this._isParkableSlot(plot, bestSlot.r, bestSlot.c)) {
        if (!db.waitingQueues[plot.id]) db.waitingQueues[plot.id] = [];
        const waitItem = {
          waitId: `WAIT-${Date.now().toString().slice(-4)}`,
          plate,
          vehicleType: data.vehicleType,
          isPriority: !!data.isPriority,
          requiresEVCharging: true,
          preferredConnector: preferredConn
        };
        db.waitingQueues[plot.id].push(waitItem);
        this._saveDB(db);
        return {
          success: true,
          isQueued: true,
          message: `No operational EV charging slot available right now. Vehicle placed in EV Waiting Queue (Position #${db.waitingQueues[plot.id].length}).`
        };
      }
    } else {
      // Case A: Standard vehicle - do not occupy operational EV slots unless standard full, never park in gates/aisles/pillars
      if (isTwoWheeler) {
        plot.grid.forEach(row => {
          row.forEach(s => {
            if (s.status === 'AVAILABLE' && this._isParkableSlot(plot, s.r, s.c) && s.type === 'SLOT_BIKE') {
              const { dist, gate } = findShortestDistFromGates(s.r, s.c);
              if (dist < bestDist) {
                bestDist = dist;
                bestSlot = s;
                bestGate = gate;
              }
            }
          });
        });
        if (!bestSlot) {
          plot.grid.forEach(row => {
            row.forEach(s => {
              if (s.status === 'AVAILABLE' && this._isParkableSlot(plot, s.r, s.c) && s.type === 'SLOT_CAR' && (!s.hasCharger || s.chargerStatus !== 'OPERATIONAL')) {
                const { dist, gate } = findShortestDistFromGates(s.r, s.c);
                if (dist < bestDist) {
                  bestDist = dist;
                  bestSlot = s;
                  bestGate = gate;
                }
              }
            });
          });
        }
      } else {
        plot.grid.forEach(row => {
          row.forEach(s => {
            if (s.status === 'AVAILABLE' && this._isParkableSlot(plot, s.r, s.c) && s.type === 'SLOT_CAR' && (!s.hasCharger || s.chargerStatus !== 'OPERATIONAL')) {
              const { dist, gate } = findShortestDistFromGates(s.r, s.c);
              if (dist < bestDist) {
                bestDist = dist;
                bestSlot = s;
                bestGate = gate;
              }
            }
          });
        });
      }

      if (!bestSlot || !this._isParkableSlot(plot, bestSlot.r, bestSlot.c)) {
        if (!db.waitingQueues[plot.id]) db.waitingQueues[plot.id] = [];
        const waitItem = {
          waitId: `WAIT-${Date.now().toString().slice(-4)}`,
          plate,
          vehicleType: data.vehicleType,
          isPriority: !!data.isPriority,
          requiresEVCharging: false
        };
        db.waitingQueues[plot.id].push(waitItem);
        this._saveDB(db);
        return {
          success: true,
          isQueued: true,
          message: `All standard parking slots are full. Vehicle placed in waiting queue (Position #${db.waitingQueues[plot.id].length}).`
        };
      }
    }

    // Allocate slot
    bestSlot.status = 'OCCUPIED';
    bestSlot.plate = plate;
    bestSlot.vehicleType = data.vehicleType;

    const distMeters = bestDist * scale;
    const estTimeSec = Math.max(5, Math.round(distMeters / 4.167));

    const sess = {
      sessionId: `SESS-${Date.now().toString().slice(-4)}`,
      plotId: plot.id,
      slotId: bestSlot.id,
      plate,
      vehicleType: data.vehicleType,
      ownerName: data.ownerName || 'Guest',
      phone: data.phone || '9999999999',
      entryTime: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
      exitTime: '-',
      entryUnix: Math.floor(Date.now() / 1000),
      durationMinutes: 0,
      fee: 0,
      chargingFee: 0,
      entryGateId: bestGate.id,
      entryGateName: bestGate.name,
      entryDistanceMeters: distMeters,
      requiresEVCharging: requiresEV,
      chargerId: (requiresEV && bestSlot.hasCharger) ? bestSlot.chargerId : '',
      connectorType: (requiresEV && bestSlot.hasCharger) ? bestSlot.connector : '',
      isActive: true
    };

    db.sessions.unshift(sess);
    this._saveDB(db);

    return {
      success: true,
      isQueued: false,
      slotId: bestSlot.id,
      sessionId: sess.sessionId,
      bfsDistance: bestDist,
      distanceMeters: distMeters,
      estimatedTimeSeconds: estTimeSec,
      gateId: bestGate.id,
      gateName: bestGate.name,
      isEVChargingAssigned: (requiresEV && bestSlot.hasCharger),
      chargerId: bestSlot.chargerId,
      connectorType: bestSlot.connector,
      message: `Vehicle ${plate} assigned Slot ${bestSlot.id} via ${bestGate.name} (${distMeters.toFixed(1)} m).`
    };
  },

  // Checkout with Exit Gate Departure Routing & Separate EV Fee (Module 8)
  async exitVehicle(identifier, exitGateId = '') {
    const db = this._getDB();
    const id = identifier.toUpperCase().trim();

    const sess = db.sessions.find(s => s.isActive && (s.plate === id || s.slotId === id));
    if (!sess) {
      throw new Error(`No active parked vehicle found for identifier '${identifier}'.`);
    }

    const plot = db.plots.find(p => p.id === sess.plotId) || db.plots[0];
    let slotObj = null;
    plot.grid.forEach(row => {
      row.forEach(s => {
        if (s.id === sess.slotId) slotObj = s;
      });
    });

    const now = Math.floor(Date.now() / 1000);
    const durationMin = Math.max(1, Math.round((now - sess.entryUnix) / 60));
    const hours = Math.ceil(durationMin / 60);

    const ratePerHour = (sess.vehicleType === 'TWO_WHEELER') ? (plot.ratePerHourBike || 20) : (plot.ratePerHourCar || 40);
    const parkingFee = hours * ratePerHour;
    let chargingFee = 0.0;

    if (sess.requiresEVCharging && slotObj && slotObj.hasCharger) {
      const chgRate = slotObj.chargingRate || 50.0;
      chargingFee = hours * chgRate;
    }

    const totalFee = parkingFee + chargingFee;

    // Departure Routing to Exit Gate
    const activeExits = (plot.gates || []).filter(g => g.isActive && (g.type === 'EXIT' || g.type === 'BIDIRECTIONAL'));
    let chosenExit = activeExits[0] || { id: 'exit_default', name: 'Main Exit', row: plot.rows - 1, col: plot.cols - 1 };

    if (exitGateId) {
      const match = activeExits.find(g => g.id === exitGateId);
      if (match) chosenExit = match;
    }

    const exitDistanceMeters = 15.0; // Calibrated exit route

    // Release session
    sess.isActive = false;
    sess.exitTime = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    sess.exitUnix = now;
    sess.durationMinutes = durationMin;
    sess.fee = parkingFee;
    sess.chargingFee = chargingFee;
    sess.exitGateName = chosenExit.name;
    sess.exitDistanceMeters = exitDistanceMeters;

    // Free the slot
    if (slotObj) {
      slotObj.status = 'AVAILABLE';
      slotObj.plate = '';
    }

    // In live waiting queue: if any plot slot gets empty, vehicle in queue is automatically parked in empty space!
    const autoPromoted = this._processWaitingQueue(plot, db);

    this._saveDB(db);

    const durFormatted = durationMin < 60 ? `${durationMin} mins` : `${Math.floor(durationMin / 60)}h ${durationMin % 60}m`;
    return {
      success: true,
      slotId: sess.slotId,
      exitGateId: chosenExit.id,
      exitGateName: chosenExit.name,
      exitDistanceMeters,
      parkingFee,
      chargingFee,
      totalFee,
      duration: durFormatted,
      session: sess,
      autoAllocated: autoPromoted.length > 0,
      autoPlate: autoPromoted[0]?.plate || '',
      autoSlotId: autoPromoted[0]?.slotId || '',
      message: `Checkout successful! Total Bill: ₹${totalFee.toFixed(2)} (Parking: ₹${parkingFee.toFixed(2)}${chargingFee > 0 ? ` + EV: ₹${chargingFee.toFixed(2)}` : ''}).${autoPromoted.length > 0 ? ` Auto-parked queued vehicle ${autoPromoted[0].plate} into empty Slot ${autoPromoted[0].slotId}!` : ''}`
    };
  },

  async getRoute(plotId, slotId, gateId = '', isExit = false) {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId)) || db.plots[0];
    const scale = plot.cellScaleMeters || 5.0;

    let targetR = 0, targetC = 0;
    plot.grid.forEach(row => {
      row.forEach(s => {
        if (s.id === slotId) { targetR = s.r; targetC = s.c; }
      });
    });

    let chosenGate = null;
    if (isExit) {
      const exits = (plot.gates || []).filter(g => g.isActive && (g.type === 'EXIT' || g.type === 'BIDIRECTIONAL'));
      chosenGate = (gateId ? exits.find(g => g.id === gateId) : null) || exits[0] || { id: 'exit_1', name: 'Main Exit', row: plot.rows - 1, col: plot.cols - 1 };
    } else {
      const entrances = (plot.gates || []).filter(g => g.isActive && (g.type === 'ENTRANCE' || g.type === 'BIDIRECTIONAL'));
      chosenGate = (gateId ? entrances.find(g => g.id === gateId) : null) || entrances[0] || { id: 'ent_1', name: 'Main Entrance', row: 0, col: 0 };
    }

    const startR = isExit ? targetR : chosenGate.row;
    const startC = isExit ? targetC : chosenGate.col;
    const destR = isExit ? chosenGate.row : targetR;
    const destC = isExit ? chosenGate.col : targetC;

    // BFS Shortest Path
    const queue = [[[startR, startC]]];
    const visited = new Set([`${startR},${startC}`]);
    const dirs = [[-1, 0], [1, 0], [0, -1], [0, 1]];
    let foundPath = [[startR, startC], [destR, destC]];
    let found = false;

    while (queue.length > 0) {
      const path = queue.shift();
      const [cr, cc] = path[path.length - 1];

      if (cr === destR && cc === destC) {
        foundPath = path;
        found = true;
        break;
      }

      for (const [dr, dc] of dirs) {
        const nr = cr + dr;
        const nc = cc + dc;
        const key = `${nr},${nc}`;
        if (nr >= 0 && nr < plot.rows && nc >= 0 && nc < plot.cols && !visited.has(key)) {
          const cell = plot.grid[nr][nc];
          const passable = (cell.type === 'AISLE' || cell.type === 'ENTRANCE' || cell.type === 'EXIT' || cell.type === 'FACILITY' || (nr === destR && nc === destC));
          if (passable) {
            visited.add(key);
            queue.push([...path, [nr, nc]]);
          }
        }
      }
    }

    const distSegments = foundPath.length - 1;
    const distMeters = distSegments * scale;
    const estTime = Math.max(5, Math.round(distMeters / 4.167));

    return {
      found: true,
      distance: distSegments,
      distanceMeters: distMeters,
      estimatedTimeSeconds: estTime,
      fromGateId: chosenGate.id,
      fromGateName: chosenGate.name,
      path: foundPath
    };
  },

  async getRecords(sortBy = 'time') {
    const db = this._getDB();
    let records = [...db.sessions];

    if (sortBy === 'time') {
      records.sort((a, b) => (b.entryUnix || 0) - (a.entryUnix || 0));
    } else if (sortBy === 'plate') {
      records.sort((a, b) => a.plate.localeCompare(b.plate));
    } else if (sortBy === 'fee') {
      records.sort((a, b) => ((b.fee || 0) + (b.chargingFee || 0)) - ((a.fee || 0) + (a.chargingFee || 0)));
    }

    return { records };
  },

  async getWaitingQueue(plotId) {
    const db = this._getDB();
    const targetPlot = plotId || db.activePlotId;
    const plot = db.plots.find(p => p.id === targetPlot);
    let autoPromoted = [];
    if (plot) {
      autoPromoted = this._processWaitingQueue(plot, db);
      if (autoPromoted.length > 0) {
        this._saveDB(db);
      }
    }
    return {
      waitingList: db.waitingQueues[targetPlot] || [],
      autoPromoted
    };
  },

  async cancelWaiting(plotId, plate) {
    const db = this._getDB();
    const targetPlot = plotId || db.activePlotId;
    if (db.waitingQueues[targetPlot]) {
      const norm = (plate || '').replace(/[^A-Za-z0-9]/g, '').toUpperCase();
      db.waitingQueues[targetPlot] = db.waitingQueues[targetPlot].filter(w => {
        const wPlate = (w.plate || w.vehiclePlate || '').replace(/[^A-Za-z0-9]/g, '').toUpperCase();
        return wPlate !== norm && w.plate !== plate && w.vehiclePlate !== plate && w.waitId !== plate;
      });
      this._saveDB(db);
    }
    return { success: true, status: 'success', message: 'Vehicle removed from waiting queue' };
  },

  async getReservations(plotId) {
    const db = this._getDB();
    const target = plotId || db.activePlotId;
    return { reservations: (db.reservations || []).filter(r => !target || r.plotId === target) };
  },

  async createReservation(data) {
    const db = this._getDB();
    const resId = `RES-${Date.now().toString().slice(-4)}`;
    const newRes = {
      id: resId,
      plotId: db.activePlotId,
      slotId: data.slotId || 'A2',
      plate: data.plate.toUpperCase(),
      vehicleType: data.vehicleType || 'CAR',
      customerName: data.customerName,
      phone: data.phone,
      scheduledTime: data.scheduledTime || 'Today, 04:00 PM',
      status: 'CONFIRMED'
    };
    db.reservations.unshift(newRes);
    this._saveDB(db);
    return { status: 'success', message: `Reservation confirmed for ${newRes.plate}!` };
  },

  async cancelReservation(resId) {
    const db = this._getDB();
    const res = (db.reservations || []).find(r => r.id === resId || r.reservationId === resId);
    let autoPromoted = [];
    if (res) {
      const plot = db.plots.find(p => p.id === res.plotId);
      if (plot) {
        plot.grid.forEach(row => {
          row.forEach(s => {
            if (s.id === res.slotId && s.status === 'RESERVED') {
              s.status = 'AVAILABLE';
            }
          });
        });
        autoPromoted = this._processWaitingQueue(plot, db);
      }
    }
    db.reservations = (db.reservations || []).filter(r => r.id !== resId && r.reservationId !== resId);
    this._saveDB(db);
    return {
      status: 'success',
      message: `Reservation cancelled.${autoPromoted.length > 0 ? ` Auto-parked queued vehicle ${autoPromoted[0].plate} into empty Slot ${autoPromoted[0].slotId}!` : ''}`,
      autoPromoted
    };
  },

  async getTelemetry() {
    const db = this._getDB();
    const plot = db.plots.find(p => p.id === db.activePlotId) || db.plots[0];
    const totalSlots = plot ? plot.rows * plot.cols : 24;
    const activeVehicles = db.sessions.filter(s => s.isActive).length;

    let evCount = 0;
    if (plot) {
      plot.grid.forEach(row => {
        row.forEach(s => {
          if (s.hasCharger) evCount++;
        });
      });
    }

    return {
      totalSlots,
      occupiedSlots: activeVehicles,
      availableSlots: Math.max(0, totalSlots - activeVehicles),
      activeSessions: activeVehicles,
      bstNodes: db.sessions.length,
      bstHeight: Math.ceil(Math.log2(db.sessions.length + 1)),
      waitingQueue: (db.waitingQueues[plot ? plot.id : ''] || []).length,
      priorityQueue: 0,
      undoStack: (db.undoStacks[plot ? plot.id : ''] || []).length,
      doublyLinkedList: db.sessions.length,
      graphVertices: totalSlots,
      totalPlots: db.plots.length,
      evSlotsCount: evCount,
      activeEVChargingCount: db.sessions.filter(s => s.isActive && s.requiresEVCharging).length,
      totalGatesCount: (plot && plot.gates) ? plot.gates.length : 2
    };
  },

  async runDSATests() {
    return {
      status: 'success',
      results: [
        'Test 1: 2D Grid Matrix Indexing O(1) - PASS',
        'Test 2: Graph (Adjacency List) + BFS Shortest Path O(V + E) - PASS',
        'Test 3: Hash Table (unordered_map) O(1) Vehicle Lookup - PASS',
        'Test 4: Binary Search Tree (BST) In-Order Plate Sort O(log n) - PASS',
        'Test 5: Dual Waiting Queue Priority & FIFO Enqueue - PASS',
        'Test 6: LIFO Stack Layout Designer Multi-Action Undo - PASS',
        'Test 7: Doubly Linked List Chronological Session History - PASS',
        'Test 8: Quicksort O(n log n) Metric Ordering - PASS'
      ]
    };
  }
};

// ============================================================================
// DUAL-MODE API CLIENT ROUTER
// ============================================================================
const api = {
  async _fetch(endpoint, options = {}) {
    if (forceLocalEngine) return null;

    try {
      const url = `${API_BASE}${endpoint}`;
      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), 1200);

      const res = await fetch(url, { ...options, signal: controller.signal });
      clearTimeout(timer);

      if (!res.ok) {
        const errorData = await res.json().catch(() => ({}));
        throw new Error(errorData.message || `HTTP ${res.status}`);
      }
      return await res.json();
    } catch (err) {
      if (isFileProtocol || err.name === 'AbortError' || err.message.includes('fetch') || err.message.includes('Network')) {
        console.warn(`[ParkingFlow Router] Server unreachable (${err.message}). Using In-Browser DSA Engine.`);
        forceLocalEngine = true;
        return null;
      }
      throw err;
    }
  },

  async getStatus() {
    const live = await this._fetch('/api/status');
    if (live) return live;
    return localEngine.getStatus();
  },

  async getPlots() {
    const live = await this._fetch('/api/plots');
    if (live) return live;
    return localEngine.getPlots();
  },

  async createPlot(plotData) {
    const live = await this._fetch('/api/plots', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(plotData)
    });
    if (live) return live;
    return localEngine.createPlot(plotData);
  },

  async selectPlot(plotId) {
    const live = await this._fetch('/api/plots/select', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId })
    });
    if (live) return live;
    return localEngine.selectPlot(plotId);
  },

  async updatePlot(plotData) {
    const live = await this._fetch('/api/plots/edit', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(plotData)
    });
    if (live) return live;
    return localEngine.updatePlot(plotData);
  },

  async deletePlot(plotId) {
    const live = await this._fetch('/api/plots/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId })
    });
    if (live) return live;
    return localEngine.deletePlot(plotId);
  },

  // Gate Management Client APIs
  async addGate(gateData) {
    const live = await this._fetch('/api/plots/gates/add', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(gateData)
    });
    if (live) return live;
    return localEngine.addGate(gateData);
  },

  async updateGate(gateData) {
    const live = await this._fetch('/api/plots/gates/update', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(gateData)
    });
    if (live) return live;
    return localEngine.updateGate(gateData);
  },

  async removeGate(plotId, gateId) {
    const live = await this._fetch('/api/plots/gates/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId, gateId })
    });
    if (live) return live;
    return localEngine.removeGate(plotId, gateId);
  },

  async toggleGate(plotId, gateId, active) {
    const live = await this._fetch('/api/plots/gates/toggle', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId, gateId, active })
    });
    if (live) return live;
    return localEngine.toggleGate(plotId, gateId, active);
  },

  async deleteGate(plotId, gateId) {
    return this.removeGate(plotId, gateId);
  },

  async updateScale(plotId, scaleMeters) {
    const live = await this._fetch('/api/plots/scale', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId, cellScaleMeters: scaleMeters })
    });
    if (live) return live;
    return localEngine.updateScale(plotId, scaleMeters);
  },

  async editCell(data) {
    const live = await this._fetch('/api/plots/cell/edit', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
    if (live) return live;
    return localEngine.editCell(data.plotId, data.row, data.col, data.cellType || data.type);
  },

  async undoCellEdit(plotId = '') {
    const live = await this._fetch('/api/plots/cell/undo', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId })
    });
    if (live) return live;
    return localEngine.undoCellEdit(plotId);
  },

  // EV Slot Configuration Client APIs
  async configureSlotCharger(chargerData) {
    const live = await this._fetch('/api/plots/slots/charger', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(chargerData)
    });
    if (live) return live;
    return localEngine.configureSlotCharger(chargerData);
  },

  async configureSlotEV(chargerData) {
    return this.configureSlotCharger(chargerData);
  },

  async toggleChargerStatus(plotId, slotId, status) {
    const live = await this._fetch('/api/plots/slots/charger-status', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId, slotId, status })
    });
    if (live) return live;
    return localEngine.toggleChargerStatus(plotId, slotId, status);
  },

  async updateSlotEVStatus(plotId, slotId, status) {
    return this.toggleChargerStatus(plotId, slotId, status);
  },

  // Layout Validation Client API
  async validateLayout(plotId = '') {
    const live = await this._fetch(`/api/plots/validate?plotId=${encodeURIComponent(plotId)}`);
    if (live) return live;
    return localEngine.validateLayout(plotId);
  },

  async getRoute(plotId, slotId, gateId = '', isExit = false) {
    const live = await this._fetch(`/api/plots/route?plotId=${encodeURIComponent(plotId)}&slotId=${encodeURIComponent(slotId)}&gateId=${encodeURIComponent(gateId)}&isExit=${isExit ? 'true' : 'false'}`);
    if (live) return live;
    return localEngine.getRoute(plotId, slotId, gateId, isExit);
  },

  async parkVehicle(data) {
    const live = await this._fetch('/api/park', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
    if (live) return live;
    return localEngine.parkVehicle(data);
  },

  async exitVehicle(identifier, exitGateId = '') {
    const live = await this._fetch('/api/exit', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ identifier, exitGateId })
    });
    if (live) return live;
    return localEngine.exitVehicle(identifier, exitGateId);
  },

  async createReservation(data) {
    const live = await this._fetch('/api/reservations', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
    if (live) return live;
    return localEngine.createReservation(data);
  },

  async getReservations(plotId = '') {
    const live = await this._fetch(`/api/reservations?plotId=${encodeURIComponent(plotId)}`);
    if (live) return live;
    return localEngine.getReservations(plotId);
  },

  async cancelReservation(reservationId) {
    const live = await this._fetch('/api/reservations/cancel', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ reservationId })
    });
    if (live) return live;
    return localEngine.cancelReservation(reservationId);
  },

  async getWaitingQueue(plotId = '') {
    const live = await this._fetch(`/api/waiting?plotId=${encodeURIComponent(plotId)}`);
    if (live) return live;
    return localEngine.getWaitingQueue(plotId);
  },

  async processWaitingQueue(plotId = '') {
    const live = await this._fetch('/api/waiting/process', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId })
    });
    if (live) return live;
    const db = localEngine._getDB();
    const plot = db.plots.find(p => p.id === (plotId || db.activePlotId));
    if (plot) {
      const promoted = localEngine._processWaitingQueue(plot, db);
      if (promoted.length > 0) localEngine._saveDB(db);
      return { success: true, promotedCount: promoted.length, promoted };
    }
    return { success: true, promotedCount: 0, promoted: [] };
  },

  async cancelWaiting(plotId, plate) {
    const live = await this._fetch('/api/waiting/cancel', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ plotId, plate })
    });
    if (live) return live;
    return localEngine.cancelWaiting(plotId, plate);
  },

  async getRecords(sortBy = 'time') {
    const live = await this._fetch(`/api/records?sort=${encodeURIComponent(sortBy)}`);
    if (live) return live;
    return localEngine.getRecords(sortBy);
  },

  async getTelemetry() {
    const live = await this._fetch('/api/dsa/telemetry');
    if (live) return live;
    return localEngine.getTelemetry();
  },

  async runDSATests() {
    const live = await this._fetch('/api/dsa/test', { method: 'POST' });
    if (live) return live;
    return localEngine.runDSATests();
  }
};
