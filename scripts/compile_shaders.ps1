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
  @("foliage.frag", "foliage.frag.spv"),
  @("sky.vert", "sky.vert.spv"),
  @("sky.frag", "sky.frag.spv"),
  @("blob.vert", "blob.vert.spv"),
  @("blob.frag", "blob.frag.spv"),
  @("bolt.vert", "bolt.vert.spv"),
  @("bolt.frag", "bolt.frag.spv"),
  @("particle.vert", "particle.vert.spv"),
  @("particle.frag", "particle.frag.spv"),
  @("cull.comp", "cull.comp.spv"),
  @("post.vert", "post.vert.spv"),
  @("post.frag", "post.frag.spv"),
  @("deferred_light.vert", "deferred_light.vert.spv"),
  @("deferred_light.frag", "deferred_light.frag.spv"),
  @("gbuffer_terrain.frag", "gbuffer_terrain.frag.spv"),
  @("shadow.vert", "shadow.vert.spv"),
  @("shadow_foliage.vert", "shadow_foliage.vert.spv"),
  @("shadow.frag", "shadow.frag.spv")
)
foreach ($p in $pairs) {
  $in = Join-Path $shaderDir $p[0]
  $out = Join-Path $shaderDir $p[1]
  # -I for #include "common_pbr.glsl"
  & $glslc -I $shaderDir -o $out $in
  if ($LASTEXITCODE -ne 0) { Write-Host "FAILED $($p[0])"; exit 1 }
  Write-Host "Compiled $($p[0]) -> $($p[1])"
}

# Runtime loads assets relative to the exe cwd (usually build/). CMake only
# copies on link — recompile alone left stale SPIR-V in build/assets/shaders.
$runtimeDirs = @(
  (Join-Path $root "build\assets\shaders"),
  (Join-Path $root "build\Release\assets\shaders"),
  (Join-Path $root "build\Debug\assets\shaders")
)
foreach ($rd in $runtimeDirs) {
  $parent = Split-Path $rd -Parent
  if (-not (Test-Path $parent)) { continue }
  New-Item -ItemType Directory -Force -Path $rd | Out-Null
  Copy-Item (Join-Path $shaderDir "*.spv") $rd -Force
  Write-Host "Synced SPIR-V -> $rd"
}
Write-Host "Done."
