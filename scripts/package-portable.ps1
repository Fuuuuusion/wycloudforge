[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [string]$OutputDir = "dist",
    [string]$NodeExe = ""
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $env:TEMP "netease_build"
}

$buildExe = Join-Path $BuildDir "bin\FuSinplayer.exe"
if (-not (Test-Path -LiteralPath $buildExe -PathType Leaf)) {
    throw "未找到 $buildExe ,请先构建 Release 版本"
}

function Resolve-RequiredFile([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "未找到$Description：$Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

if ($NodeExe) {
    $nodePath = Resolve-RequiredFile $NodeExe "Node.js 可执行文件"
} else {
    $nodeCommand = Get-Command node.exe -ErrorAction SilentlyContinue
    if ($nodeCommand) {
        $nodePath = (Resolve-Path -LiteralPath $nodeCommand.Source).Path
    } else {
        $nodePath = Resolve-RequiredFile "C:\Program Files\nodejs\node.exe" "Node.js 可执行文件"
    }
}

$nodeVersion = (& $nodePath --version).Trim()
if ($nodeVersion -notmatch '^v(\d+)\.') {
    throw "无法识别 Node.js 版本：$nodeVersion"
}
if ([int]$Matches[1] -lt 20) {
    throw "便携 QQ 服务要求 Node.js 20+，当前为 $nodeVersion"
}

$neteaseRoot = Join-Path $repo "本地部署\netease-api"
$neteaseSource = Resolve-RequiredFile (Join-Path $neteaseRoot "node_modules\NeteaseCloudMusicApi\app.js") "网易云 API 入口"
$neteasePackage = Resolve-RequiredFile (Join-Path $neteaseRoot "package.json") "网易云 API package.json"
$neteaseLock = Resolve-RequiredFile (Join-Path $neteaseRoot "package-lock.json") "网易云 API package-lock.json"
$qqRoot = Join-Path $repo "本地部署\qq-api"
$qqServer = Resolve-RequiredFile (Join-Path $qqRoot "server.js") "QQ 音乐包装服务"
$qqPackage = Resolve-RequiredFile (Join-Path $qqRoot "package.json") "QQ 音乐 package.json"
$qqLock = Resolve-RequiredFile (Join-Path $qqRoot "package-lock.json") "QQ 音乐 package-lock.json"
$qqDependency = Resolve-RequiredFile (Join-Path $qqRoot "node_modules\@sansenjian\qq-music-api\package.json") "QQ 音乐 SDK 依赖"

$qtBin = "C:\Qt\6.11.1\mingw_64\bin"
$mingwBin = "C:\Qt\Tools\mingw1310_64\bin"
$vcpkgBin = "C:\vcpkg\installed\x64-mingw-dynamic\bin"
$windeployqt = Resolve-RequiredFile (Join-Path $qtBin "windeployqt.exe") "windeployqt"
$libTag = Resolve-RequiredFile (Join-Path $vcpkgBin "libtag.dll") "TagLib DLL"
$libZ = Resolve-RequiredFile (Join-Path $vcpkgBin "libz.dll") "zlib DLL"

$outRoot = Join-Path $repo $OutputDir
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$packageName = "FuSinplayer-portable-$stamp"
$packageDir = Join-Path $outRoot $packageName
$zipPath = Join-Path $outRoot "$packageName.zip"
if (Test-Path -LiteralPath $packageDir) {
    throw "目标目录已存在：$packageDir"
}
if (Test-Path -LiteralPath $zipPath) {
    throw "目标压缩包已存在：$zipPath"
}
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

$oldPath = $env:PATH
$env:PATH = "$qtBin;$mingwBin;$vcpkgBin;$oldPath"
try {
    & $windeployqt --release --no-translations --no-system-d3d-compiler --dir $packageDir $buildExe
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt 失败，退出码：$LASTEXITCODE"
    }
} finally {
    $env:PATH = $oldPath
}

Copy-Item -LiteralPath $libTag -Destination (Join-Path $packageDir "libtag.dll") -Force
Copy-Item -LiteralPath $libZ -Destination (Join-Path $packageDir "libz.dll") -Force
Copy-Item -LiteralPath $buildExe -Destination (Join-Path $packageDir "FuSinplayer.exe") -Force

$runtimeNodeDir = Join-Path $packageDir "runtime\node"
$serviceRoot = Join-Path $packageDir "本地部署"
$neteaseDestination = Join-Path $serviceRoot "netease-api"
$qqDestination = Join-Path $serviceRoot "qq-api"
New-Item -ItemType Directory -Force -Path $runtimeNodeDir,$neteaseDestination,$qqDestination | Out-Null
Copy-Item -LiteralPath $nodePath -Destination (Join-Path $runtimeNodeDir "node.exe") -Force

$nodeInstallDir = Split-Path -Parent $nodePath
Get-ChildItem -LiteralPath $nodeInstallDir -File -Filter "LICENSE*" -ErrorAction SilentlyContinue |
    Copy-Item -Destination $runtimeNodeDir -Force

$neteaseNodeModules = Join-Path $neteaseRoot "node_modules"
if (-not (Test-Path -LiteralPath $neteaseNodeModules -PathType Container)) {
    throw "未找到网易云 API node_modules：$neteaseNodeModules"
}
Copy-Item -LiteralPath $neteaseNodeModules -Destination $neteaseDestination -Recurse -Force
Copy-Item -LiteralPath $neteasePackage -Destination $neteaseDestination -Force
Copy-Item -LiteralPath $neteaseLock -Destination $neteaseDestination -Force

Get-ChildItem -LiteralPath $qqRoot -File |
    Where-Object { $_.Name -eq "package.json" -or $_.Name -eq "package-lock.json" -or $_.Extension -eq ".js" } |
    Copy-Item -Destination $qqDestination -Force
Copy-Item -LiteralPath (Join-Path $qqRoot "node_modules") -Destination $qqDestination -Recurse -Force

$launcher = @'
@echo off
setlocal
cd /d "%~dp0"
start "" "%~dp0FuSinplayer.exe" %*
endlocal
'@
Set-Content -LiteralPath (Join-Path $packageDir "启动FuSinplayer.cmd") -Value $launcher -Encoding ASCII

$readme = @"
FuSinplayer 便携版

启动：双击“启动FuSinplayer.cmd”。不要只移动或重命名其中的 DLL。

本包已内置：
- FuSinplayer.exe 与 Qt/FFmpeg/TagLib 运行库
- Node.js $nodeVersion
- 网易云 API NeteaseCloudMusicApi@4.32.0
- QQ 音乐包装服务及 @sansenjian/qq-music-api@2.6.0

首次使用：
1. 将整个目录解压到有写入权限的位置。
2. 双击启动脚本。
3. 在应用中重新扫码登录网易云或 QQ/微信。
4. 在设置中选择本地音乐目录和永久下载目录。

在线服务仅监听本机：网易云 127.0.0.1:3000，QQ 音乐 127.0.0.1:3200。
目标机不需要安装 Qt、CMake、vcpkg 或 Node.js。在线功能仍需要网络。

本包不包含旧电脑的数据库、音乐文件、缓存、下载文件或登录凭据。
Windows DPAPI 登录凭据不能跨机器复制；如需迁移数据，请先正常退出旧应用并按项目 docs/HANDOFF.md 的说明迁移。

已知边界：.mgg/.mflac 可以显示但尚不能解密播放；VIP、DRM、403、地区和版权限制不会被绕过。
"@
Set-Content -LiteralPath (Join-Path $packageDir "README-便携版.txt") -Value $readme -Encoding UTF8

$manifest = [ordered]@{
    product = "FuSinplayer"
    package = $packageName
    createdAt = (Get-Date).ToUniversalTime().ToString("o")
    node = $nodeVersion
    neteaseApi = "NeteaseCloudMusicApi@4.32.0"
    qqApi = "@sansenjian/qq-music-api@2.6.0"
    services = [ordered]@{
        netease = "http://127.0.0.1:3000"
        qq = "http://127.0.0.1:3200"
    }
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $packageDir "portable-manifest.json") -Encoding UTF8

Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zipPath -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
$packageSize = (Get-ChildItem -LiteralPath $packageDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
$zipSize = (Get-Item -LiteralPath $zipPath).Length

Write-Host "便携包目录：$packageDir"
Write-Host "便携包压缩包：$zipPath"
Write-Host "目录文件总大小：$packageSize bytes"
Write-Host "ZIP 大小：$zipSize bytes"
Write-Host "ZIP SHA-256：$zipHash"
