# VoiceStem Release 빌드 → 설치 파일(dist\VoiceStem-Setup-<버전>.exe) 생성
$ErrorActionPreference = 'Stop'

$root     = Split-Path $PSScriptRoot -Parent
$buildDir = 'C:\dev\voicestem-build'
$cmake    = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin\cmake.exe'
$iscc     = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'

if (-not (Test-Path $cmake)) { $cmake = 'cmake' }
$env:PATH = (Join-Path $env:USERPROFILE '.cargo\bin') + ';' + $env:PATH

# 버전은 CMakeLists.txt의 project(... VERSION x.y.z) 하나만 고치면 된다
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(VoiceStem VERSION ([0-9.]+)').Matches[0].Groups[1].Value

& $cmake -S $root -B $buildDir | Out-Null
& $cmake --build $buildDir --config Release --parallel --target VoiceStem_VST3 VoiceStem_Standalone
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

& $iscc "/DAppVersion=$version" (Join-Path $PSScriptRoot 'VoiceStem.iss')
if ($LASTEXITCODE -ne 0) { throw 'installer build failed' }

Get-Item (Join-Path $root "dist\VoiceStem-Setup-$version.exe")
