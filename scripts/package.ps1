param(
    [string]$BuildDir = "",
    [string]$OutputDir = "dist"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $env:TEMP "netease_build"
}

$exe = Join-Path $BuildDir "bin\FuSinplayer.exe"
if (-not (Test-Path $exe)) {
    throw "未找到 $exe ,请先构建(见 README)"
}

$qtBin = "C:\Qt\6.11.1\mingw_64\bin"
$mingwBin = "C:\Qt\Tools\mingw1310_64\bin"
$vcpkgBin = "C:\vcpkg\installed\x64-mingw-dynamic\bin"

$out = Join-Path $repo $OutputDir
New-Item -ItemType Directory -Force -Path $out | Out-Null

$env:PATH = "$qtBin;$mingwBin;$vcpkgBin;$env:PATH"

# 部署 Qt 运行库(含 plugins 与平台插件)
& (Join-Path $qtBin "windeployqt.exe") --release --no-translations --no-system-d3d-compiler `
    --dir $out $exe

# 部署第三方动态库
Copy-Item (Join-Path $vcpkgBin "libtag.dll") $out -Force
Copy-Item (Join-Path $vcpkgBin "libz.dll") $out -Force
Copy-Item $exe (Join-Path $out "FuSinplayer.exe") -Force

Write-Host "打包完成:$out"
