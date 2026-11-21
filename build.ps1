$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    windres displayoff.rc -O coff -o build/icon.o
    if ($LASTEXITCODE -ne 0) { throw 'Icon compilation failed.' }

    gcc -std=c17 -Wall -Wextra -Werror -Os -s -municode -mwindows -static -o build/displayoff.exe displayoff.c power.c build/icon.o -lpowrprof -luuid
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
} finally {
    Pop-Location
}
