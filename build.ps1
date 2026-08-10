param([switch]$Test, [string]$Config = "Release")

$ErrorActionPreference = "Stop"
cmake -S . -B build "-DCMAKE_BUILD_TYPE=$Config"
cmake --build build --config $Config
if ($Test) { ctest --test-dir build -C $Config --output-on-failure }
Write-Host "-> build/treetop.exe"
