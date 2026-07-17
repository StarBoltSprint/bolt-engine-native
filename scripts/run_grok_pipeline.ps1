# Generate / import full Crystal Nebula material pack via bolt_grok_import
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

$outDir = "assets\materials\crystal_nebula"
$inbox = "assets\grok_inbox"

if (-not $exeImport) {
  Write-Error "bolt_grok_import.exe not found. Build the project first."
}

# Prefer Imagine sources; fall back to sample generator for ground only
$materials = @(
  @{ Name = "crystal_ground"; Src = "crystal_ground_src.png"; Fallback = "crystal_ground_sample.png" },
  @{ Name = "crystal_path";   Src = "crystal_path_src.png";   Fallback = $null },
  @{ Name = "crystal_rock";   Src = "crystal_rock_src.png";   Fallback = $null },
  @{ Name = "crystal_stalk";  Src = "crystal_stalk_src.png";  Fallback = $null }
)

foreach ($m in $materials) {
  $src = Join-Path $inbox $m.Src
  if (-not (Test-Path $src) -and $m.Fallback) {
    $fb = Join-Path $inbox $m.Fallback
    if (-not (Test-Path $fb) -and $exeSample -and $m.Name -eq "crystal_ground") {
      & $exeSample $fb
    }
    if (Test-Path $fb) { $src = $fb }
  }
  if (-not (Test-Path $src)) {
    Write-Host "SKIP $($m.Name) — drop PNG at $src (or $($m.Fallback))"
    continue
  }
  Write-Host "Import $($m.Name) from $src"
  & $exeImport --in $src --out $outDir --name $m.Name
  if ($LASTEXITCODE -ne 0) { Write-Error "Import failed for $($m.Name)" }
}

Write-Host "Crystal biome pack under $outDir"
Get-ChildItem $outDir -File | Sort-Object Name | Format-Table Name, Length
