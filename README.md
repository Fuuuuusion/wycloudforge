# FuSinplayer

基于 Qt 6 / C++17 的 Windows 本地与多来源音乐播放器，支持深色、浅色和跟随系统主题，布局参考网易云音乐。
本地音乐库、播放、歌词和歌单可以完全离线使用；搜索、扫码登录和在线歌曲需要本地在线服务及网络连接。

## 功能

- 无边框自绘窗口(原生拖动 / 最大化 / 贴边吸附)
- 外观主题:跟随系统 / 深色 / 浅色,设置页即时切换并持久化;QSS、自绘控件和单色图标同步更新
- 首页(横幅 / 最近播放 / 我的歌单 / 推荐歌手)、音乐库(歌曲 / 歌手 / 专辑)、歌单详情、正在播放(歌词页)、搜索
- 本地音乐扫描(mp3 / flac / wav / m4a / aac),TagLib 元数据解析,内嵌封面提取与缓存
- 播放内核:Qt Multimedia,支持列表循环 / 单曲循环 / 随机、播放队列与历史、记住上次歌曲与进度
- 歌词:外挂 `.lrc` → 内嵌歌词 → 内置歌词编辑器;编码自动识别(UTF-8 / UTF-16 / GBK);点击歌词跳转
- 歌单:我喜欢的音乐 + 自建歌单(增删改、排序、右键菜单),最近播放,SQLite 持久化;所有歌曲列表支持复选框批量收藏/取消收藏、批量加入已有或新建歌单
- 在线歌曲下载:自动缓存与永久下载分离;全新安装默认保存到 `Music\FuSinplayer Downloads`（旧安装继续沿用原目录），支持单曲/批量下载、取消、失败重试、进度和同名文件序号处理
- 下载管理:侧栏独立入口、下载中/等待/完成状态图标,左右滚动栏逐首显示字节进度、成功/失败/取消状态;已下载歌曲可离线播放,清理缓存不会删除永久下载文件
- 本地页筛选:全部 / 本地导入 / 已缓存 / 已下载;本地导入、缓存和永久下载按来源分别删除,避免误删原文件或歌单关系
- 多来源账号与搜索:网易云、QQ/微信账号独立验证;同曲优先本地文件和已登录来源,只在没有可用非游客版本时显示游客试听来源
- 快捷键:空格 播放/暂停,`Ctrl+←/→` 上一首/下一首,`Ctrl+↑/↓` 音量,`L` 歌词页,`Esc` 返回上一级,`Ctrl+O` 导入文件夹,`F5` 重新扫描

## 环境要求

- Windows 10/11
- Qt 6.11.1(MinGW 64 位,含 Multimedia / Svg / Sql)
- CMake 3.21+ 与 Ninja(Qt 安装器自带)
- vcpkg + `taglib:x64-mingw-dynamic`
- 开发/源码构建需要 Node.js 20+；便携发行包会内置 Node.js,目标机无需另行安装

## 构建

```powershell
# 安装依赖(一次即可)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install taglib:x64-mingw-dynamic

# 配置与编译。若仓库路径含中文,先把仓库映射为 ASCII 盘符。
subst W: (Get-Location).Path
cmake -S W:\ -B W:\build-ascii -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic `
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build W:\build-ascii

# 运行
W:\build-ascii\bin\FuSinplayer.exe
```

> 注意:MinGW/AutoMoc 的命令链也会读取源码路径。仓库路径含中文时，仅把构建目录放到英文路径仍可能失败；应在同一终端用 `subst` 映射仓库，并从映射盘配置和构建。

## 测试

```powershell
cmake --build W:\build-ascii
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir W:\build-ascii --output-on-failure -j1
```

Qt 回归覆盖:LRC、TagLib、曲库持久化、播放器、多来源、搜索服务/页面、共享 UI 控件和主题系统。测试职责详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 打包

```powershell
.\scripts\package.ps1 -BuildDir "$env:TEMP\netease_build" -OutputDir dist
```

上面的命令只生成 Qt 运行目录,适合本机已有在线服务的开发预览。给另一台机器使用时,使用包含在线服务的便携打包命令:

```powershell
.\scripts\package-portable.ps1 -BuildDir "$env:TEMP\netease_build" -OutputDir dist
```

该命令会生成一个带时间戳的目录和同名 `.zip`,包含:

- `FuSinplayer.exe` 及 Qt、FFmpeg、TagLib 运行库;
- 内置 Node.js 运行时;
- 网易云 API `NeteaseCloudMusicApi@4.32.0`;
- QQ 音乐包装服务及 `@sansenjian/qq-music-api@2.6.0`;
- `启动FuSinplayer.cmd` 和目标机运行说明。

目标机只需解压 ZIP,双击启动脚本即可。程序会从自身目录自动发现两个服务,并在 `127.0.0.1:3000` / `127.0.0.1:3200` 启动；首次使用需在目标机重新扫码登录。不要复制旧电脑的 DPAPI 登录凭据。

便携包是免安装目录,不包含用户音乐、数据库、缓存、下载文件或账号凭据。若要迁移这些数据,必须先在旧电脑正常退出应用,再按 [docs/HANDOFF.md](docs/HANDOFF.md) 中的数据迁移说明复制。

## 开发辅助参数

`FuSinplayer.exe` 支持以下参数,便于无头验证与截图:

- `--folder <path>` 指定音乐文件夹(可重复)
- `--db <path>` 指定数据库文件位置
- `--song <path>` 启动时恢复指定歌曲
- `--page <0-7>` 启动后跳到指定页面(0 推荐 / 1 收藏 / 2 本地歌单 / 3 自建歌单 / 4 歌单详情 / 5 正在播放 / 6 搜索 / 7 下载管理)
- `--screenshot <path>` 启动 0.9 秒后截图退出
- `--smoke` 启动 1.5 秒后退出(冒烟测试)

## 设计与主题

- 运行时颜色事实来源:`ui/ThemeManager.cpp`
- 全局样式模板:`resources/theme.qss`;使用语义令牌,不直接写主题十六进制颜色
- 设计评审令牌镜像:`design/tokens.json`
- 生产图标统一位于 `resources/icons/` 和 `resources/source-icons/`,由 `resources.qrc` 收口
- 界面不使用极光背景、毛玻璃、模糊、渐变、阴影或页面切换动效
- 底部播放器固定铺满内容区底部,保留播放、进度、模式、音量、歌词和队列控件

## 架构

```
app/      入口、单实例、命令行参数、主窗口组装与页面接线
core/     歌曲模型、SQLite、播放/下载、搜索聚合、平台来源、凭据与设置
ui/       页面、对话框、列表模型/视图、自绘控件与 ThemeManager
resources/运行时 QSS、图标和来源 Logo
本地部署/ 网易云服务与 QQ 包装服务
design/   当前语义颜色和布局参数文档
tests/    core、页面、共享 UI 和主题 QtTest 回归
scripts/  正式目录与完整便携包脚本
```

完整职责、数据流、已清理冗余和仍需渐进拆分的热点见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
