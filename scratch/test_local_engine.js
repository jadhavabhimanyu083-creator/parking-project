/**
 * Automated Test Suite for localEngine (Offline file:/// mode parity test)
 */
const fs = require('fs');
const path = require('path');

// Mock localStorage and window
const store = {};
global.localStorage = {
  getItem: (k) => store[k] || null,
  setItem: (k, v) => { store[k] = String(v); },
  removeItem: (k) => { delete store[k]; },
  clear: () => { for (const k in store) delete store[k]; }
};
global.window = {
  location: { protocol: 'file:', origin: 'null' }
};

const vm = require('vm');
const apiCode = fs.readFileSync(path.join(__dirname, '..', 'public', 'js', 'api.js'), 'utf8');
vm.runInThisContext(apiCode);

async function runOfflineTests() {
  console.log("==========================================================");
  console.log("   LOCALENGINE (OFFLINE FILE:/// MODE) TEST SUITE         ");
  console.log("==========================================================");

  let pass = 0, fail = 0;
  function assert(name, cond, msg = '') {
    if (cond) {
      console.log(` [PASS] ${name}`);
      pass++;
    } else {
      console.error(` [FAIL] ${name} - ${msg}`);
      fail++;
    }
  }

  try {
    const status = await localEngine.getStatus();
    const plotId = status.activePlot.id;
    assert("Get Status", !!plotId && status.grid.length > 0);

    // 1. Validate layout
    const val = await localEngine.validateLayout(plotId);
    assert("Offline Layout Validation", val.totalSlots > 0 && val.activeEntrances > 0, `val: ${JSON.stringify(val)}`);

    // 2. Update Scale
    const scaleRes = await localEngine.updateScale(plotId, 6.5);
    assert("Offline Scale Update (6.5m)", scaleRes.success && scaleRes.cellScaleMeters === 6.5, `res: ${JSON.stringify(scaleRes)}`);

    // 3. Add Gate
    const addGateRes = await localEngine.addGate({
      plotId,
      name: "Offline Test Gate",
      type: "ENTRANCE",
      row: 2,
      col: 0,
      isActive: true
    });
    assert("Offline Add Gate", addGateRes.success, `res: ${JSON.stringify(addGateRes)}`);
    const testGateId = addGateRes.gate.id;

    // 4. Update Gate
    const updGateRes = await localEngine.updateGate({
      plotId,
      gateId: testGateId,
      id: testGateId,
      name: "Offline Test Gate (Bidirectional)",
      type: "BIDIRECTIONAL",
      row: 2,
      col: 0,
      isActive: true
    });
    assert("Offline Update Gate", updGateRes.success, `res: ${JSON.stringify(updGateRes)}`);

    // 5. Toggle Gate
    const togClose = await localEngine.toggleGate(plotId, testGateId, false);
    const togOpen = await localEngine.toggleGate(plotId, testGateId, true);
    assert("Offline Toggle Gate (Close/Open)", togClose.success && togOpen.success);

    // 6. Cell Edit (Paint driving path)
    const cellEdit = await localEngine.editCell(plotId, 2, 0, "AISLE");
    assert("Offline Paint Driving Aisle (Cell Edit)", cellEdit.success && cellEdit.cell.type === "AISLE");

    // 7. Undo Cell Edit (LIFO Stack)
    const undoRes = await localEngine.undoCellEdit(plotId);
    assert("Offline Undo Cell Edit (LIFO Stack)", undoRes.success && undoRes.cell.type === "SLOT_CAR");

    // 8. Delete Gate
    const delGate = await localEngine.removeGate(plotId, testGateId);
    assert("Offline Delete Gate", delGate.success);

    // 9. Configure EV Charger
    const evRes = await localEngine.configureSlotCharger({
      plotId,
      slotId: "A1",
      hasCharger: true,
      chargerId: "EV-A1-OFFLINE",
      connectorType: "CCS2",
      powerKw: 50.0,
      status: "OPERATIONAL",
      ratePerHour: 45.0
    });
    assert("Offline Configure EV Charger", evRes.success);

    // 10. Toggle Charger Status
    const evStatusRes = await localEngine.toggleChargerStatus(plotId, "A1", "OUT_OF_SERVICE");
    assert("Offline Set Charger OUT_OF_SERVICE", evStatusRes.success);

    // Clean up A1
    await localEngine.configureSlotCharger({
      plotId,
      slotId: "A1",
      hasCharger: false,
      chargerId: "",
      status: "OPERATIONAL"
    });

    // 11. Reset Scale
    const resetScale = await localEngine.updateScale(plotId, 5.0);
    assert("Offline Reset Scale (5.0m)", resetScale.success && resetScale.cellScaleMeters === 5.0);

    // 12. Waiting Queue Enqueue & Cancel (Module 4, 6, 7)
    const queueParkRes = await localEngine.parkVehicle({
      plotId,
      plate: "DL09EV9999",
      vehicleType: "CAR",
      isPriority: true,
      ownerName: "Offline Queue Test",
      phone: "9876543210",
      requiresEVCharging: true,
      preferredConnector: "CHAdeMO" // 0 slots installed with CHAdeMO -> guaranteed queue
    });
    assert("Offline Enqueue Vehicle", queueParkRes.isQueued === true);

    const waitListBefore = await localEngine.getWaitingQueue(plotId);
    const inQueue = (waitListBefore.waitingList || []).some(w => w.plate === "DL09EV9999");
    assert("Offline Vehicle Present in Queue", inQueue);

    const cancelRes = await localEngine.cancelWaiting(plotId, "DL09EV9999");
    assert("Offline Cancel Waiting Vehicle", cancelRes.success === true || cancelRes.status === 'success');

    const waitListAfter = await localEngine.getWaitingQueue(plotId);
    const stillInQueue = (waitListAfter.waitingList || []).some(w => w.plate === "DL09EV9999");
    assert("Offline Vehicle Removed from Queue", !stillInQueue);

    console.log("----------------------------------------------------------");
    console.log(`OFFLINE RESULTS: Passed: ${pass} | Failed: ${fail}`);
    console.log("==========================================================");

    process.exit(fail === 0 ? 0 : 1);
  } catch (err) {
    console.error("Test execution failed with error:", err);
    process.exit(1);
  }
}

runOfflineTests();
