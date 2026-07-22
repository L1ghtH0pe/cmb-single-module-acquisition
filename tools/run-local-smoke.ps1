param(
    [int]$Port = 9000,
    [int]$Frames = 10
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $ProjectRoot

$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH

Remove-Item -Recurse -Force logs, captures\raw -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force logs, captures\raw, captures\meta | Out-Null

$receiver = Start-Process `
    -FilePath '.\build\receiver.exe' `
    -ArgumentList @($Port.ToString(), $Frames.ToString()) `
    -RedirectStandardOutput 'logs\receiver-stdout.txt' `
    -RedirectStandardError 'logs\receiver-stderr.txt' `
    -PassThru

Start-Sleep -Seconds 1

$sender = Start-Process `
    -FilePath '.\build\sender.exe' `
    -ArgumentList @('127.0.0.1', $Port.ToString(), $Frames.ToString()) `
    -RedirectStandardOutput 'logs\sender-stdout.txt' `
    -RedirectStandardError 'logs\sender-stderr.txt' `
    -Wait `
    -PassThru

$receiver.WaitForExit(10000) | Out-Null
if (-not $receiver.HasExited) {
    Stop-Process -Id $receiver.Id -Force
    throw 'receiver did not exit within 10 seconds'
}

Write-Output "sender_exit=$($sender.ExitCode)"
Write-Output "receiver_exit=$($receiver.ExitCode)"
Write-Output '--- sender stdout ---'
Get-Content logs\sender-stdout.txt -ErrorAction SilentlyContinue
Write-Output '--- receiver stdout ---'
Get-Content logs\receiver-stdout.txt -ErrorAction SilentlyContinue
Write-Output '--- sender stderr ---'
Get-Content logs\sender-stderr.txt -ErrorAction SilentlyContinue
Write-Output '--- receiver stderr ---'
Get-Content logs\receiver-stderr.txt -ErrorAction SilentlyContinue

if ($sender.ExitCode -ne 0 -or $receiver.ExitCode -ne 0) {
    exit 1
}
