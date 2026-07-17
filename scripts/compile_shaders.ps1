# Compile GLSL -> SPIR-V (requires Vulkan SDK glslc or glslangValidator)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$shaderDir = Join-Path $root "assets\shaders"

$glslc = $null
if ($env:VULKAN_SDK) {
  $c = Join-Path $env:VULKAN_SDK "Bin\glslc.exe"
  if (Test-Path $c) { $glslc = $c }
}
if (-not $glslc) {
  $c = Get-Command glslc -ErrorAction SilentlyContinue
  if ($c) { $glslc = $c.Source }
}
if (-not $glslc) {
  # Common install paths
  Get-ChildItem "C:\VulkanSDK" -ErrorAction SilentlyContinue | Sort-Object Name -Descending | ForEach-Object {
    $c = Join-Path $_.FullName "Bin\glslc.exe"
    if (Test-Path $c) { $glslc = $c }
  }
}

if (-not $glslc) {
  Write-Host "ERROR: glslc not found. Install Vulkan SDK and re-run."
  exit 1
}

Write-Host "Using $glslc"
$pairs = @(
  @("terrain.vert", "terrain.vert.spv"),
  @("terrain.frag", "terrain.frag.spv"),
  @("foliage.vert", "foliage.vert.spv"),
  @("foliage.frag", "foliage.frag.spv")
)
foreach ($p in $pairs) {
  $in = Join-Path $shaderDir $p[0]
  $out = Join-Path $shaderDir $p[1]
  & $glslc -o $out $in
  Write-Host "Compiled $($p[0]) -> $($p[1])"
}
Write-Host "Done."
