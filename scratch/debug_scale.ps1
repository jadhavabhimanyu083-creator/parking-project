$b = @{ plotId = "plot_central_campus"; cellScaleMeters = 6.5 } | ConvertTo-Json -Compress
Write-Host "Sending: $b"
$r = Invoke-RestMethod -Uri "http://localhost:8080/api/plots/scale" -Method POST -Body $b -ContentType "application/json"
Write-Host "Result type: $($r.cellScaleMeters.GetType().FullName)"
Write-Host "Result value: $($r.cellScaleMeters)"
Write-Host "Raw JSON: $($r | ConvertTo-Json -Compress)"
