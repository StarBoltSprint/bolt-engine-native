# Fetch entt header-only into third_party/entt
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\entt"
if (Test-Path (Join-Path $dest "single_include\entt\entt.hpp")) {
  Write-Host "entt already present"
  exit 0
}
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$zip = Join-Path $env:TEMP "entt-master.zip"
Write-Host "Downloading entt..."
Invoke-WebRequest -Uri "https://github.com/skypjack/entt/archive/refs/heads/master.zip" -OutFile $zip
Expand-Archive -Path $zip -DestinationPath (Join-Path $root "third_party") -Force
$extracted = Join-Path $root "third_party\entt-master"
if (Test-Path $extracted) {
  if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
  Rename-Item $extracted "entt"
}
Write-Host "entt ready at third_party/entt/single_include"
