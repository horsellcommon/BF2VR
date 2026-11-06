param([switch]$RunTests)
$ErrorActionPreference = 'Stop'
$sourceRoot = $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $cmakePath = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio" -Filter cmake.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (!$cmakePath) { throw 'Install Visual Studio 2022 C++ Build Tools with CMake and a Windows SDK.' }
$buildRoot = Join-Path $sourceRoot 'out\build'
$arguments = @('-S', $sourceRoot, '-B', $buildRoot, '-G', 'Visual Studio 17 2022', '-A', 'x64')
foreach ($dependency in @('safetyhook', 'openxr', 'zydis')) {
    $dependencyRoot = Join-Path $sourceRoot "third_party\$dependency"
    if (Test-Path -LiteralPath $dependencyRoot) {
        $arguments += "-DFETCHCONTENT_SOURCE_DIR_$($dependency.ToUpperInvariant())=$dependencyRoot"
    }
}
& $cmakePath @arguments
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmakePath --build $buildRoot --config Release --parallel 4 -- /nr:false
if ($LASTEXITCODE) { throw 'Compilation failed.' }
if ($RunTests) {
    $ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
    & $ctestPath --test-dir $buildRoot -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Regression tests failed.' }
}
Write-Host "Compiled files: $buildRoot\Release. Nothing was injected or launched."
