# Generate sample crystal ground + real PBR maps via bolt_grok_import
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$exeImport = @(
  "build\bolt_grok_import.exe",
  "build\Release\bolt_grok_import.exe",
  "bolt_grok_import.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$exeSample = @(
  "build\bolt_make_sample_ground.exe",
  "build\Release\bolt_make_sample_ground.exe",
  "bolt_make_sample_ground.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$inbox = "assets\grok_inbox\crystal_ground_sample.png"
$outDir = "assets\materials\crystal_nebula"

if ($exeSample) {
  & $exeSample $inbox
} elseif (-not (Test-Path $inbox)) {
  Write-Host "No sample generator binary — place any PNG at $inbox first"
}

if (-not $exeImport) {
  Write-Host "Building standalone bolt_grok_import with clang if possible..."
  $env:Path = "C:\Program Files\LLVM\bin;" + $env:Path
  # Prefer VS developer environment when available
}

if (-not (Test-Path $inbox)) {
  Write-Error "Missing input image: $inbox"
}

if ($exeImport) {
  & $exeImport --in $inbox --out $outDir --name crystal_ground
} else {
  Write-Host "bolt_grok_import.exe not found. Build the project first, then re-run."
  exit 1
}

Write-Host "PBR maps ready under $outDir"
Get-ChildItem $outDir\crystal_ground* | Format-Table Name, Length
