$ErrorActionPreference = "Stop"
$baseUrl = "http://localhost:8080"
$plotId = "plot_central_campus"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   TEST SUITE: LIVE WAITING QUEUE REMOVE FUNCTIONALITY    " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# Step 1: Enqueue an EV vehicle when all EV bays are occupied or full
# Note: In default campus plot, operational EV slots are A4 and A5.
# Let's send an EV vehicle with CCS2 to ensure it either parks or queues.
# To reliably trigger queue: let's see how many EV slots are free or mark chargers out of service or fill them.
# Even simpler: park an EV vehicle that requires CHAdeMO (which has 0 slots) or fill EV chargers!
$evPlate1 = "DL99EV" + (Get-Random -Minimum 1000 -Maximum 9999)
$parkBody = @{
    plotId = $plotId
    plate = $evPlate1
    vehicleType = "CAR"
    isPriority = $true
    ownerName = "VIP Waiting Test"
    phone = "9876543210"
    requiresEVCharging = $true
    preferredConnector = "CHAdeMO"
} | ConvertTo-Json

Write-Host "Enqueuing vehicle $evPlate1 (CHAdeMO connector - guaranteed queue)..." -ForegroundColor Yellow
$parkRes = Invoke-RestMethod -Uri "$baseUrl/api/park" -Method POST -Body $parkBody -ContentType "application/json"
Write-Host "Park Response: $($parkRes.message) (isQueued=$($parkRes.isQueued))" -ForegroundColor Gray

if (-not $parkRes.isQueued) {
    Write-Host "[FAIL] Vehicle was not enqueued" -ForegroundColor Red
    exit 1
}

# Step 2: Fetch the live waiting queue
$waitRes = Invoke-RestMethod -Uri "$baseUrl/api/waiting?plotId=$plotId" -Method GET
$foundInQueue = $false
foreach ($w in $waitRes.waitingList) {
    if ($w.plate -eq $evPlate1) {
        $foundInQueue = $true
        Write-Host " [PASS] Found vehicle $($w.plate) in waiting queue (Priority: $($w.isPriority))" -ForegroundColor Green
        break
    }
}

if (-not $foundInQueue) {
    Write-Host "[FAIL] Vehicle $evPlate1 was not found in waiting queue!" -ForegroundColor Red
    exit 1
}

# Step 3: Call POST /api/waiting/cancel to remove the vehicle!
Write-Host "Sending cancel request for vehicle $evPlate1..." -ForegroundColor Yellow
$cancelBody = @{
    plotId = $plotId
    plate = $evPlate1
} | ConvertTo-Json

$cancelRes = Invoke-RestMethod -Uri "$baseUrl/api/waiting/cancel" -Method POST -Body $cancelBody -ContentType "application/json"
Write-Host "Cancel Response: $($cancelRes.message) (success=$($cancelRes.success))" -ForegroundColor Gray

if (-not $cancelRes.success) {
    Write-Host "[FAIL] Cancel request returned success=false" -ForegroundColor Red
    exit 1
}
Write-Host " [PASS] Successfully called /api/waiting/cancel" -ForegroundColor Green

# Step 4: Verify vehicle is no longer in the waiting queue!
$waitAfter = Invoke-RestMethod -Uri "$baseUrl/api/waiting?plotId=$plotId" -Method GET
$stillPresent = $false
foreach ($w in $waitAfter.waitingList) {
    if ($w.plate -eq $evPlate1) {
        $stillPresent = $true
        break
    }
}

if ($stillPresent) {
    Write-Host "[FAIL] Vehicle $evPlate1 is still in the waiting queue!" -ForegroundColor Red
    exit 1
} else {
    Write-Host " [PASS] Verified vehicle $evPlate1 was successfully removed from the live waiting queue!" -ForegroundColor Green
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "ALL WAITING QUEUE REMOVAL TESTS PASSED (100% SUCCESS)!" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Cyan
