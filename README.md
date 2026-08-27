# 仿网易云播放器(NeteaseClone)

基于 Qt 6 / C++17 的本地音乐播放器,深色极光主题 + 悬浮胶囊播放器,布局参考网易云音乐。完全离线:本地音乐库 + 播放 + 歌词 + 歌单 + 搜索。

## 功能

- 无边框自绘窗口(原生拖动 / 最大化 / 贴边吸附)
- 首页(横幅 / 最近播放 / 我的歌单 / 推荐歌手)、音乐库(歌曲 / 歌手 / 专辑)、歌单详情、正在播放(歌词页)、搜索
- 本地音乐扫描(mp3 / flac / wav / m4a / aac),TagLib 元数据解析,内嵌封面提取与缓存
- 播放内核:Qt Multimedia,支持列表循环 / 单曲循环 / 随机、播放队列与历史、记住上次歌曲与进度
- 歌词:外挂 `.lrc` → 内嵌歌词 → 内置歌词编辑器;编码自动识别(UTF-8 / UTF-16 / GBK);点击歌词跳转
- 歌单:我喜欢的音乐 + 自建歌单(增删改、排序、右键菜单),最近播放,SQLite 持久化
- 快捷键:空格 播放/暂停,`Ctrl+←/→` 上一首/下一首,`Ctrl+↑/↓` 音量,`L` 歌词页,`Ctrl+O` 导入文件夹,`F5` 重新扫描

## 环境要求

- Windows 10/11
- Qt 6.11.1(MinGW 64 位,含 Multimedia / Svg / Sql)
- CMake 3.21+ 与 Ninja(Qt 安装器自带)
- vcpkg + `taglib:x64-mingw-dynamic`

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
.\scripts\package.ps1 -BuildDir %TEMP%\netease_build -OutputDir dist
```

产物为免安装目录 `dist\`,可直接拷贝到其他机器运行。

## 开发辅助参数

`NeteaseClone.exe` 支持以下参数,便于无头验证与截图:

- `--folder <path>` 指定音乐文件夹(可重复)
- `--db <path>` 指定数据库文件位置
- `--song <path>` 启动时恢复指定歌曲
- `--page <0-4>` 启动后跳到指定页面(0 首页 / 1 音乐库 / 2 歌单 / 3 正在播放 / 4 搜索)
- `--screenshot <path>` 启动 0.9 秒后截图退出
- `--smoke` 启动 1.5 秒后退出(冒烟测试)

## 设计与主题

- 设计令牌唯一事实来源:`design/tokens.json`
- HTML 高保真原型(可在浏览器直接打开修改):`design/prototype/index.html`
- 深色极光背景(浏览器 CSS 动画 / Qt 自绘 `AuroraBackground`),全局无 1px 分隔线,分区靠间距与半透明层次
- 底部播放器为悬浮毛玻璃胶囊(不可拖动),播放/进度/模式/音量等控件齐全
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
