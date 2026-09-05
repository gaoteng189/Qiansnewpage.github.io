# ============================================================
#  Message Board Console (TUI)
#  Start/stop the message board HTTP backend and set port.
# ============================================================

# Self-elevate to administrator if not already running elevated.
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host 'Requesting administrator privileges...' -ForegroundColor Yellow
    $myPath = $MyInvocation.MyCommand.Path
    Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$myPath`"" -Verb RunAs -Wait
    exit
}

$ErrorActionPreference = 'Stop'
$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$portFile = Join-Path $root '.server-port'
$boardUrl = 'https://qiansnewpage-github-io.pages.dev/message/'

# Load saved port (default 50304)
$port = 50304
if (Test-Path $portFile) {
    $saved = (Get-Content $portFile -Raw -ErrorAction SilentlyContinue).Trim()
    if ($saved -match '^\d+$') { $port = [int]$saved }
}

$script:nodeProc = $null

# ---------- process helpers ----------
function Get-Running($proc) {
    return ($proc -and -not $proc.HasExited)
}

function Stop-Services {
    if (Get-Running $script:nodeProc) {
        Stop-Process -Id $script:nodeProc.Id -Force -ErrorAction SilentlyContinue
    }
    $script:nodeProc = $null
    Write-Host 'Backend stopped.' -ForegroundColor Yellow
}

function Start-Services {
    if (Get-Running $script:nodeProc) {
        Write-Host 'Services are already running.' -ForegroundColor Yellow
        return
    }
    Write-Host "Starting backend server.js (port $port)..." -ForegroundColor Yellow
    $script:nodeProc = Start-Process -FilePath 'node' -ArgumentList 'server.js',"$port" -WorkingDirectory $root -PassThru -WindowStyle Normal
    Write-Host ''
    Write-Host "  Board URL  : $boardUrl" -ForegroundColor Green
    Write-Host '  (board is served via Cloudflare Pages -> OpenFrp -> this backend)' -ForegroundColor Green
}

function Set-PortValue {
    if (Get-Running $script:nodeProc) {
        Write-Host 'Please stop services before changing the port.' -ForegroundColor Red
        return
    }
    $portInput = Read-Host "Current port is $port. Enter new port (1-65535)"
    $newPort = 0
    if ($portInput -match '^\d+$') { $newPort = [int]$portInput }
    if ($newPort -ge 1 -and $newPort -le 65535) {
        $port = $newPort
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($portFile, "$port", $utf8NoBom)
        Write-Host "Port set to $port" -ForegroundColor Green
    } else {
        Write-Host 'Invalid port.' -ForegroundColor Red
    }
}

function Show-Menu {
    Clear-Host
    $nodeRunning = Get-Running $script:nodeProc
    $nodeState   = if ($nodeRunning) { 'Running' } else { 'Stopped' }

    Write-Host '========================================' -ForegroundColor Cyan
    Write-Host '       Message Board Console' -ForegroundColor Cyan
    Write-Host '========================================' -ForegroundColor Cyan
    Write-Host ''
    Write-Host "  Port           : $port"
    Write-Host "  Backend (node) : $nodeState"
    Write-Host "  Board URL      : $boardUrl"
    Write-Host ''
    Write-Host '  [1] Start services'
    Write-Host '  [2] Stop services'
    Write-Host '  [3] Set port'
    Write-Host '  [4] Exit'
    Write-Host ''
}

# ---------- main loop ----------
try {
    $running = $true
    while ($running) {
        Show-Menu
        $choice = (Read-Host '  Select (1-4)').Trim()
        switch ($choice) {
            '1' { Start-Services }
            '2' { Stop-Services }
            '3' { Set-PortValue }
            '4' { $running = $false }
            default { Write-Host 'Invalid choice.' -ForegroundColor Red }
        }
        Start-Sleep -Milliseconds 400
    }
} finally {
    Stop-Services
    Write-Host 'Bye.' -ForegroundColor Cyan
}

