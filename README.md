# 仿网易云播放器(NeteaseClone)

基于 Qt 6 / C++17 的本地音乐播放器,固定深色主题 + 播放器,布局参考网易云音乐。
本地音乐库、播放、歌词和歌单可以完全离线使用；搜索、扫码登录和在线歌曲需要本地在线服务及网络连接。

## 功能

- 无边框自绘窗口(原生拖动 / 最大化 / 贴边吸附)
- 首页(横幅 / 最近播放 / 我的歌单 / 推荐歌手)、音乐库(歌曲 / 歌手 / 专辑)、歌单详情、正在播放(歌词页)、搜索
- 本地音乐扫描(mp3 / flac / wav / m4a / aac),TagLib 元数据解析,内嵌封面提取与缓存
- 播放内核:Qt Multimedia,支持列表循环 / 单曲循环 / 随机、播放队列与历史、记住上次歌曲与进度
- 歌词:外挂 `.lrc` → 内嵌歌词 → 内置歌词编辑器;编码自动识别(UTF-8 / UTF-16 / GBK);点击歌词跳转
- 歌单:我喜欢的音乐 + 自建歌单(增删改、排序、右键菜单),最近播放,SQLite 持久化;所有歌曲列表支持复选框批量收藏/取消收藏、批量加入已有或新建歌单
- 在线歌曲下载:自动缓存与永久下载分离;永久下载默认保存到 `Music\NeteaseClone Downloads`,支持单曲/批量下载、取消、失败重试、进度和同名文件序号处理
- 下载管理:逐首显示任务进度、成功/失败/取消状态;已下载歌曲可离线播放,清理缓存不会删除永久下载文件
- 本地页筛选:全部 / 本地导入 / 已缓存 / 已下载;本地导入、缓存和永久下载按来源分别删除,避免误删原文件或歌单关系
- 快捷键:空格 播放/暂停,`Ctrl+←/→` 上一首/下一首,`Ctrl+↑/↓` 音量,`L` 歌词页,`Ctrl+O` 导入文件夹,`F5` 重新扫描

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

# 配置与编译
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic `
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build

# 运行
build\bin\NeteaseClone.exe
```

> 注意:源码路径含中文时,MinGW 的 `moc` 无法在中文路径下生成文件,建议把 `build` 目录放到英文路径(如 `%TEMP%\netease_build`),源码目录保持原样即可。

## 测试

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

单元测试覆盖:LRC 解析(含编码与 offset)、TagLib 标签 / 封面 / 内嵌歌词读取、歌单与收藏持久化、播放器状态机。

## 打包

```powershell
.\scripts\package.ps1 -BuildDir "$env:TEMP\netease_build" -OutputDir dist
```

上面的命令只生成 Qt 运行目录,适合本机已有在线服务的开发预览。给另一台机器使用时,使用包含在线服务的便携打包命令:

```powershell
.\scripts\package-portable.ps1 -BuildDir "$env:TEMP\netease_build" -OutputDir dist
```

该命令会生成一个带时间戳的目录和同名 `.zip`,包含:

- `NeteaseClone.exe` 及 Qt、FFmpeg、TagLib 运行库;
- 内置 Node.js 运行时;
- 网易云 API `NeteaseCloudMusicApi@4.32.0`;
- QQ 音乐包装服务及 `@sansenjian/qq-music-api@2.6.0`;
- `启动仿网易云播放器.cmd` 和目标机运行说明。

目标机只需解压 ZIP,双击启动脚本即可。程序会从自身目录自动发现两个服务,并在 `127.0.0.1:3000` / `127.0.0.1:3200` 启动；首次使用需在目标机重新扫码登录。不要复制旧电脑的 DPAPI 登录凭据。

便携包是免安装目录,不包含用户音乐、数据库、缓存、下载文件或账号凭据。若要迁移这些数据,必须先在旧电脑正常退出应用,再按 [docs/HANDOFF.md](docs/HANDOFF.md) 中的数据迁移说明复制。

## 开发辅助参数

`NeteaseClone.exe` 支持以下参数,便于无头验证与截图:

- `--folder <path>` 指定音乐文件夹(可重复)
- `--db <path>` 指定数据库文件位置
- `--song <path>` 启动时恢复指定歌曲
- `--page <0-6>` 启动后跳到指定页面(0 推荐 / 1 收藏 / 2 本地歌单 / 3 自建歌单 / 4 歌单详情 / 5 正在播放 / 6 搜索)
- `--screenshot <path>` 启动 0.9 秒后截图退出
- `--smoke` 启动 1.5 秒后退出(冒烟测试)

## 设计与主题

- 设计令牌唯一事实来源:`design/tokens.json`
- HTML 高保真原型(可在浏览器直接打开修改):`design/prototype/index.html`
- 固定深色背景,全局无 1px 分隔线,分区使用固定色块
- 底部播放器为固定深色面板(不可拖动),播放/进度/模式/音量等控件齐全
- 图标全部为自绘 SVG,替换 `design/prototype/assets/` 或 `resources/icons/` 即可
- Qt 主题由 `resources/theme.qss` 与设计令牌对应

## 架构

```
app/    入口与主窗口(无边框窗口、页面接线)
core/   与 UI 无关的核心:TagReader、LrcParser、LibraryService、PlayerService、
        PlaylistController、SearchService、SettingsService
ui/     自绘控件与页面:TitleBar、SideBar、PlayerBar、SongListView、LyricWidget、
        DiscoverPage、LibraryPage、SongListPage、PlayingPage、SearchPage
design/ 设计令牌与 HTML 原型
tests/  QtTest 单元测试
```
