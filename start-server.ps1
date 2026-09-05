# One-click launcher: message board backend + Cloudflare Tunnel
# Automatically updates the message page's WebSocket URL.

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
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$port = 50304

$outLog = Join-Path $root '.cloudflared.out.log'
$errLog = Join-Path $root '.cloudflared.err.log'
$page   = Join-Path $root 'message\index.html'

$script:nodeProc   = $null
$script:tunnelProc = $null

function Stop-Services {
    if ($script:nodeProc -and -not $script:nodeProc.HasExited) {
        Stop-Process -Id $script:nodeProc.Id -Force -ErrorAction SilentlyContinue
    }
    if ($script:tunnelProc -and -not $script:tunnelProc.HasExited) {
        Stop-Process -Id $script:tunnelProc.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Host '========================================' -ForegroundColor Cyan
Write-Host '  Message Board One-Click Launcher' -ForegroundColor Cyan
Write-Host '========================================' -ForegroundColor Cyan

# 1. Backend server
Write-Host "[1/4] Starting backend server (port $port)..." -ForegroundColor Yellow
$script:nodeProc = Start-Process -FilePath 'node' -ArgumentList 'server.js' -WorkingDirectory $root -PassThru -WindowStyle Normal
Start-Sleep -Milliseconds 800

# 2. Cloudflare Tunnel
Write-Host '[2/4] Starting Cloudflare Tunnel...' -ForegroundColor Yellow
if (Test-Path $outLog) { Remove-Item $outLog -Force }
if (Test-Path $errLog) { Remove-Item $errLog -Force }
$script:tunnelProc = Start-Process -FilePath 'cloudflared' -ArgumentList 'tunnel','--url',"http://localhost:$port" -WorkingDirectory $root -RedirectStandardOutput $outLog -RedirectStandardError $errLog -PassThru -WindowStyle Normal

# 3. Wait for the trycloudflare URL
Write-Host '[3/4] Waiting for tunnel URL...' -ForegroundColor Yellow
$url = $null
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 90) {
    Start-Sleep -Milliseconds 500
    $log = ''
    if (Test-Path $outLog) { $log += (Get-Content $outLog -Raw -ErrorAction SilentlyContinue) }
    if (Test-Path $errLog) { $log += (Get-Content $errLog -Raw -ErrorAction SilentlyContinue) }
    if ($log -match 'https://([a-z0-9-]+\.trycloudflare\.com)') {
        $url = $Matches[1]
        break
    }
    if ($script:tunnelProc.HasExited) { break }
}

if (-not $url) {
    Write-Host 'Failed to obtain tunnel URL. Check cloudflared logs.' -ForegroundColor Red
    Stop-Services
    Read-Host 'Press Enter to exit'
    exit 1
}

$wss = "wss://$url"
Write-Host ''
Write-Host "  Tunnel URL : https://$url" -ForegroundColor Green
Write-Host "  WebSocket  : $wss" -ForegroundColor Green

# 4. Update message page WebSocket URL
Write-Host '[4/4] Updating message page WebSocket URL...' -ForegroundColor Yellow
$content = [System.IO.File]::ReadAllText($page)
$newContent = [regex]::Replace($content, "var WS_URL = 'wss://[^']+';", "var WS_URL = '$wss';")
if ($newContent -eq $content) {
    Write-Host 'WARNING: WS_URL pattern not found, please check message/index.html manually.' -ForegroundColor Red
} else {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($page, $newContent, $utf8NoBom)
    Write-Host 'Message page updated.' -ForegroundColor Green
}

Write-Host ''
Write-Host 'All services are running. Press Ctrl+C to stop.' -ForegroundColor Cyan
Write-Host 'The message page is now connected to the new tunnel address.' -ForegroundColor Cyan

try {
    while (-not $script:tunnelProc.HasExited) {
        Start-Sleep -Seconds 1
        if ($script:nodeProc.HasExited) {
            Write-Host 'Backend server exited unexpectedly.' -ForegroundColor Red
            break
        }
    }
} finally {
    Stop-Services
    Write-Host 'Services stopped.' -ForegroundColor Yellow
}
