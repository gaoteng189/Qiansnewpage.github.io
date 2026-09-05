# ============================================================
#  Message Board Console (TUI)
#  Start/stop backend + Cloudflare Tunnel, set port,
#  and auto-update the message page's WebSocket URL.
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
$page     = Join-Path $root 'message\index.html'
$outLog   = Join-Path $root '.cloudflared.out.log'
$errLog   = Join-Path $root '.cloudflared.err.log'
$portFile = Join-Path $root '.server-port'

# Load saved port (default 50304)
$port = 50304
if (Test-Path $portFile) {
    $saved = (Get-Content $portFile -Raw -ErrorAction SilentlyContinue).Trim()
    if ($saved -match '^\d+$') { $port = [int]$saved }
}

$script:nodeProc   = $null
$script:tunnelProc = $null
$script:tunnelUrl  = $null

# ---------- WS_URL helpers ----------
function Set-WsUrl([string]$url) {
    $content = [System.IO.File]::ReadAllText($page)
    $newContent = [regex]::Replace($content, "var WS_URL = 'wss://[^']+';", "var WS_URL = '$url';")
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($page, $newContent, $utf8NoBom)
}

function Reset-WsUrl {
    Set-WsUrl 'wss://YOUR-TUNNEL.trycloudflare.com'
}

# ---------- process helpers ----------
function Get-Running($proc) {
    return ($proc -and -not $proc.HasExited)
}

function Stop-Services {
    if (Get-Running $script:tunnelProc) {
        Stop-Process -Id $script:tunnelProc.Id -Force -ErrorAction SilentlyContinue
    }
    if (Get-Running $script:nodeProc) {
        Stop-Process -Id $script:nodeProc.Id -Force -ErrorAction SilentlyContinue
    }
    $script:nodeProc   = $null
    $script:tunnelProc = $null
    $script:tunnelUrl  = $null
    Reset-WsUrl
    Write-Host 'Services stopped. Message page URL reset to placeholder.' -ForegroundColor Yellow
}

function Start-Services {
    if (Get-Running $script:nodeProc) {
        Write-Host 'Services are already running.' -ForegroundColor Yellow
        return
    }
    Write-Host "Starting backend server.js (port $port)..." -ForegroundColor Yellow
    $script:nodeProc = Start-Process -FilePath 'node' -ArgumentList 'server.js',"$port" -WorkingDirectory $root -PassThru -WindowStyle Normal
    Start-Sleep -Milliseconds 800

    Write-Host 'Starting Cloudflare Tunnel...' -ForegroundColor Yellow
    if (Test-Path $outLog) { Remove-Item $outLog -Force }
    if (Test-Path $errLog) { Remove-Item $errLog -Force }
    $script:tunnelProc = Start-Process -FilePath 'cloudflared' -ArgumentList 'tunnel','--url',"http://localhost:$port" -WorkingDirectory $root -RedirectStandardOutput $outLog -RedirectStandardError $errLog -PassThru -WindowStyle Normal

    Write-Host 'Waiting for tunnel URL...' -ForegroundColor Yellow
    $url = $null
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 90) {
        Start-Sleep -Milliseconds 500
        $log = ''
        if (Test-Path $outLog) { $log += (Get-Content $outLog -Raw -ErrorAction SilentlyContinue) }
        if (Test-Path $errLog) { $log += (Get-Content $errLog -Raw -ErrorAction SilentlyContinue) }
        if ($log -match 'https://([a-z0-9-]+\.trycloudflare\.com)') { $url = $Matches[1]; break }
        if ($script:tunnelProc.HasExited) { break }
    }

    if (-not $url) {
        Write-Host 'Failed to obtain tunnel URL. Check cloudflared.' -ForegroundColor Red
        Stop-Services
        return
    }

    $script:tunnelUrl = "https://$url"
    Set-WsUrl "wss://$url"
    Write-Host ''
    Write-Host "  Tunnel URL : $script:tunnelUrl" -ForegroundColor Green
    Write-Host "  Message page updated to: wss://$url" -ForegroundColor Green
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
    $nodeRunning   = Get-Running $script:nodeProc
    $tunnelRunning = Get-Running $script:tunnelProc
    $nodeState     = if ($nodeRunning) { 'Running' } else { 'Stopped' }
    $tunnelState   = if ($tunnelRunning) { 'Running' } else { 'Stopped' }

    Write-Host '========================================' -ForegroundColor Cyan
    Write-Host '       Message Board Console' -ForegroundColor Cyan
    Write-Host '========================================' -ForegroundColor Cyan
    Write-Host ''
    Write-Host "  Port           : $port"
    Write-Host "  Backend (node) : $nodeState"
    Write-Host "  Tunnel         : $tunnelState"
    if ($script:tunnelUrl) {
        Write-Host "  Tunnel URL     : $script:tunnelUrl"
    }
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

