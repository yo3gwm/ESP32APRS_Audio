# ESP32APRS_Audio — Build & rename firmware output

$env_name = "esp32-nodisp"
$bin_src  = ".pio\build\$env_name\firmware.bin"
$ts       = Get-Date -Format "yyyyMMdd_HHmm"
$bin_dst  = "ESP32_noDisp_$ts.bin"

Copy-Item $bin_src $bin_dst -Force
Write-Host "Firmware: $(Resolve-Path $bin_dst)" -ForegroundColor Green
