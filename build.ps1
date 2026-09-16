<#
.SYNOPSIS
Build script for the LRU Cache Capstone Project on Windows
#>

$ErrorActionPreference = "Stop"

if (-Not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Write-Host "Building LRU Cache Server..." -ForegroundColor Cyan
gcc -Wall -Wextra -O2 -Iinclude src/server.c src/lru_cache.c -o build/server.exe -lws2_32

Write-Host "Building Test Suite..." -ForegroundColor Cyan
gcc -Wall -Wextra -O2 -Iinclude tests/test_lru.c -o build/test_lru.exe

Write-Host "Build complete! Artifacts are in the build/ directory." -ForegroundColor Green
Write-Host ""
Write-Host "To run tests : .\build\test_lru.exe"
Write-Host "To run server: .\build\server.exe"
