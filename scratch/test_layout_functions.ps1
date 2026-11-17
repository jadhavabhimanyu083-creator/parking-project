$ErrorActionPreference = "Stop"

$baseUrl = "http://localhost:8080"
$plotId = "plot_central_campus"
$passCount = 0
$failCount = 0

function Assert-Step($name, $condition, $msg) {
    if ($condition) {
        Write-Host " [PASS] $name" -ForegroundColor Green
        $global:passCount++
    } else {
        Write-Host " [FAIL] $name - $msg" -ForegroundColor Red
        $global:failCount++
    }
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   PARKINGFLOW: LAYOUT & GATE CONFIGURATION TEST SUITE    " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# -------------------------------------------------------------------------
# TEST 1: Layout Validation Endpoint
# -------------------------------------------------------------------------
try {
    $val = Invoke-RestMethod -Uri "$baseUrl/api/plots/validate?plotId=$plotId" -Method GET
    Assert-Step "Layout Validation" ($val.totalSlots -gt 0) "Validation returned totalSlots: $($val.totalSlots)"
    Write-Host "    Reachable: $($val.reachableSlots), Entrances: $($val.activeEntrances), Exits: $($val.activeExits)" -ForegroundColor Gray
} catch {
    Assert-Step "Layout Validation" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 2: Update Map Scale (Meters per Cell)
# -------------------------------------------------------------------------
try {
    $scaleBody = @{ plotId = $plotId; cellScaleMeters = 6.5 } | ConvertTo-Json
    $scaleRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/scale" -Method POST -Body $scaleBody -ContentType "application/json"
    Assert-Step "Update Scale (6.5m)" ($scaleRes.success -and ([double]$scaleRes.cellScaleMeters -eq 6.5)) "Failed to update scale: $($scaleRes.message)"
} catch {
    Assert-Step "Update Scale (6.5m)" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 3: Add Entrance Gate
# -------------------------------------------------------------------------
$testGateId = "gate_test_auto_" + (Get-Random -Minimum 1000 -Maximum 9999)
try {
    $addGateBody = @{
        plotId = $plotId
        id = $testGateId
        name = "West Gate 3"
        type = "ENTRANCE"
        row = 2
        col = 0
        isActive = $true
    } | ConvertTo-Json
    $addRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/gates/add" -Method POST -Body $addGateBody -ContentType "application/json"
    Assert-Step "Add Gate" ($addRes.success -eq $true) "Failed to add gate: $($addRes.message)"
} catch {
    Assert-Step "Add Gate" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 4: Update Gate (Rename and change type)
# -------------------------------------------------------------------------
try {
    $updGateBody = @{
        plotId = $plotId
        gateId = $testGateId
        id = $testGateId
        name = "West Gate 3 (Updated)"
        type = "BIDIRECTIONAL"
        row = 2
        col = 0
        isActive = $true
    } | ConvertTo-Json
    $updRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/gates/update" -Method POST -Body $updGateBody -ContentType "application/json"
    Assert-Step "Update Gate" ($updRes.success -eq $true) "Failed to update gate: $($updRes.message)"
} catch {
    Assert-Step "Update Gate" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 5: Toggle Gate Status (Close then Open)
# -------------------------------------------------------------------------
try {
    $togCloseBody = @{ plotId = $plotId; gateId = $testGateId; active = $false } | ConvertTo-Json
    $togCloseRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/gates/toggle" -Method POST -Body $togCloseBody -ContentType "application/json"
    
    $togOpenBody = @{ plotId = $plotId; gateId = $testGateId; active = $true } | ConvertTo-Json
    $togOpenRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/gates/toggle" -Method POST -Body $togOpenBody -ContentType "application/json"
    
    Assert-Step "Toggle Gate (Close/Open)" ($togCloseRes.success -and $togOpenRes.success) "Failed to toggle gate"
} catch {
    Assert-Step "Toggle Gate (Close/Open)" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 6: Paint Driving Path / Cell Edit (Convert C1 to AISLE)
# -------------------------------------------------------------------------
try {
    $editBody = @{
        plotId = $plotId
        row = 2
        col = 0
        cellType = "AISLE"
    } | ConvertTo-Json
    $editRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/cell/edit" -Method POST -Body $editBody -ContentType "application/json"
    Assert-Step "Paint Driving Aisle (Cell Edit)" ($editRes.success -and $editRes.cellType -eq "AISLE") "Failed to edit cell: $($editRes.message)"
} catch {
    Assert-Step "Paint Driving Aisle (Cell Edit)" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 7: Undo Cell Modification (LIFO Stack)
# -------------------------------------------------------------------------
try {
    $undoBody = @{ plotId = $plotId } | ConvertTo-Json
    $undoRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/cell/undo" -Method POST -Body $undoBody -ContentType "application/json"
    Assert-Step "Undo Cell Modification (LIFO Stack)" ($undoRes.success -and $undoRes.revertedType -eq "SLOT_CAR") "Failed to undo cell: $($undoRes.message)"
} catch {
    Assert-Step "Undo Cell Modification (LIFO Stack)" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 8: Delete Gate
# -------------------------------------------------------------------------
try {
    $delBody = @{ plotId = $plotId; gateId = $testGateId } | ConvertTo-Json
    $delRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/gates/delete" -Method POST -Body $delBody -ContentType "application/json"
    Assert-Step "Delete Gate" ($delRes.success -eq $true) "Failed to delete gate: $($delRes.message)"
} catch {
    Assert-Step "Delete Gate" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 9: EV Charger Configuration on Slot A1
# -------------------------------------------------------------------------
try {
    $evBody = @{
        plotId = $plotId
        slotId = "A1"
        hasCharger = $true
        chargerId = "EV-A1-FAST"
        connector = "CCS2"
        powerKw = 60.0
        chargingRate = 55.0
        chargerStatus = "OPERATIONAL"
    } | ConvertTo-Json
    $evRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/slots/charger" -Method POST -Body $evBody -ContentType "application/json"
    Assert-Step "Configure EV Charger (Slot A1)" ($evRes.success -eq $true) "Failed to configure EV: $($evRes.message)"
} catch {
    Assert-Step "Configure EV Charger (Slot A1)" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 10: Toggle EV Charger Status to OUT_OF_SERVICE
# -------------------------------------------------------------------------
try {
    $evStatBody = @{
        plotId = $plotId
        slotId = "A1"
        chargerStatus = "OUT_OF_SERVICE"
    } | ConvertTo-Json
    $evStatRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/slots/charger-status" -Method POST -Body $evStatBody -ContentType "application/json"
    Assert-Step "Set EV Charger OUT_OF_SERVICE" ($evStatRes.success -eq $true) "Failed to set charger status"

    # Revert A1 to standard slot to keep state clean
    $evRevertBody = @{
        plotId = $plotId
        slotId = "A1"
        hasCharger = $false
        chargerId = ""
        connector = "CCS2"
        powerKw = 0
        chargingRate = 0
        chargerStatus = "OPERATIONAL"
    } | ConvertTo-Json
    Invoke-RestMethod -Uri "$baseUrl/api/plots/slots/charger" -Method POST -Body $evRevertBody -ContentType "application/json" | Out-Null
} catch {
    Assert-Step "Set EV Charger OUT_OF_SERVICE" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 11: Route and Calibrated Distance Calculation
# -------------------------------------------------------------------------
try {
    $route = Invoke-RestMethod -Uri "$baseUrl/api/plots/route?plotId=$plotId&slotId=A4" -Method GET
    Assert-Step "Route & Calibrated Distance" ($route.found -and $route.distanceMeters -gt 0) "Route not found or 0m"
    Write-Host "    Found route to Bay A4: $($route.distanceMeters) meters, $($route.distance) cells, estimated $($route.estimatedTimeSeconds)s" -ForegroundColor Gray
} catch {
    Assert-Step "Route & Calibrated Distance" $false $_.Exception.Message
}

# -------------------------------------------------------------------------
# TEST 12: Re-calibrate scale back to 5.0m
# -------------------------------------------------------------------------
try {
    $resetScaleBody = @{ plotId = $plotId; cellScaleMeters = 5.0 } | ConvertTo-Json
    $resetRes = Invoke-RestMethod -Uri "$baseUrl/api/plots/scale" -Method POST -Body $resetScaleBody -ContentType "application/json"
    Assert-Step "Reset Scale (5.0m)" ($resetRes.success -and $resetRes.cellScaleMeters -eq 5.0) "Failed to reset scale"
} catch {
    Assert-Step "Reset Scale (5.0m)" $false $_.Exception.Message
}

Write-Host "----------------------------------------------------------" -ForegroundColor Cyan
$resColor = if ($failCount -eq 0) { "Green" } else { "Red" }
Write-Host "TEST RESULTS: Passed: $passCount | Failed: $failCount" -ForegroundColor $resColor
Write-Host "==========================================================" -ForegroundColor Cyan

if ($failCount -gt 0) {
    exit 1
} else {
    exit 0
}
