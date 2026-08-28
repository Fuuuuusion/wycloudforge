# 仿网易云播放器 · 上下文交接文档

> 给接手此项目的新模型/新会话。读一遍即可无缝衔接,不必依赖此前的对话记录。

## 0) 最关键的三件事

- **仓库**: https://github.com/Fuuuuusion/wycloudforge 。当前在 **main**,HEAD 以最新提交为准,需与 `origin/main` 同步。
- **工作目录**: `C:\Users\Fusssssion\Documents\ChatGPT\仿网易云播放器`(**中文路径**,是很多坑的来源)。
- **构建/运行必须按 §2 流程**,否则会静默编译失败或看不到窗口。

---

## 1) 项目概况与技术栈

Windows 桌面音乐播放器,仿网易云(经典红 + 深色极光主题),本地 + 在线双模式,完全可离线。

- Qt 6.11.1(Widgets / Multimedia / Svg / Sql / Network)+ C++17 + CMake 3.30 + Ninja + MinGW 13(g++)
- TagLib 2.3.1(vcpkg `taglib:x64-mingw-dynamic`,运行时依赖 `C:\vcpkg\installed\x64-mingw-dynamic`)
- 数据库:QSQLite;封面缓存等写入 `QStandardPaths::AppDataLocation`
- 在线后端:本机 `NeteaseCloudMusicApi v4.32.0`,端口 **3000**(`本地部署/netease-api/node_modules/NeteaseCloudMusicApi`)
- Qt 目录:`C:/Qt/6.11.1/mingw_64`
- 目录:`app/`(入口/主窗口)、`core/`(model/service/parser,不依赖 UI)、`ui/`(自绘控件与页面)、`design/`(HTML 原型 + tokens)、`resources/`(qss/svg/字体)、`tests/`(单测)

---

## 2) 构建 / 运行关键事实与坑

### 构建(增量)

```powershell
Get-Process -Name NeteaseClone | Stop-Process -Force          # 不结束会链接 Permission denied
cmd /v:on /c "C:\Users\Fusssssion\AppData\Local\Temp\netease_build\run_ninja1.bat"   # 看 NINJA_EXIT=0
```

- **构建目录必须是英文**: `%TEMP%\netease_build`。不要在中文路径下建 build(MinGW/moc 会崩)。
- 工作区里的 `build/` 是早期产物,**不是当前在用的**(当前用 temp 目录那个)。
- `.bat` 里别写中文路径(GBK 代码页会弄坏,报"路径语法不正确");如需自建编译时把源码拷到 ASCII 临时路径。

### 运行(必须沙箱外,否则窗口落在隔离桌面看不到)

```powershell
Start-Process "$env:TEMP\netease_build\bin\NeteaseClone.exe" -ArgumentList "--folder `"C:\Users\Fusssssion\Music`""
```

### 命令行参数(`app/main.cpp`)

- `--folder <dir>`: 往曲库加文件夹并扫描(持久化到 settings)
- `--song <path>`: 设为"最近播放"(不自动播放)
- `--db <path>`: 数据库路径覆盖(隔离测试用)
- `--page <0-6>`: 配 `--screenshot` 切页
- `--screenshot <png>`: 约 1.8s 后 `grab()` 并退出
- `--smoke`: 1.5s 后退出

页面栈索引:`0推荐 / 1收藏 / 2本地歌单 / 3自建歌单 / 4歌单详情 / 5正在播放 / 6搜索`。

### 隔离截图验证(推荐)

```powershell
Start-Process $exe -ArgumentList "--folder `"$tmp`"","--db `"$tmp\t.db`"","--page 2","--screenshot `"$tmp\s.png`"" -Wait -WindowStyle Hidden
```

- **不要**在带 `.mgg` 的扫描目录上用 `--screenshot`(曾触发 `0xC0000002`);正常模式(不带 screenshot)扫 `.mgg` 稳定。
- 高分屏(150%)下 `grab()` 会放大,勿据此误判布局溢出。

### 数据库

- 真实库:`%APPDATA%\NeteaseClone\NeteaseClone\library.db`(**WAL 模式**,旁边有 `-wal/-shm`)。
- AppData 在**沙箱写保护根之外**;读/写它必须 `require_escalated`(否则 "unable to open database file"/Access denied)。
- 读库 Python:`C:\Users\Fusssssion\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`(系统无 python)。

### 其它

- Node.js 24 已装。API 目录探测在 `core/ApiService.cpp`。
- 曾有"node 进程在但不监听 3000"导致取不到二维码/在线失败;确认 `curl http://127.0.0.1:3000` 是否 200。

---

## 3) 试错清单(第一版有问题 → 反复修的那些)

### (A) UI 控件"灰框"(反复出现的根因)

- 现象:原生控件(PushButton/List/ScrollArea)渲染成灰框,暗色底上难看。
- 解法:`main.cpp` 用 **Fusion 样式 + 全局深色调色板 + 统一 QSS `:theme.qss`**。**全应用第一原则:任何新控件都用现有样式类/自绘控件,绝不引入原生 Windows 样式**;新增控件出现灰框就是违反了这条。

### (B) 无边框窗口按钮点不动

- 现象:标题栏最小/最大/关闭点不动、搜索框点不到、形状不对。
- 解法:`app/MainWindow.cpp::computeHitTest` 中,凡可交互区域(按钮、**搜索框**)返回 `HTCLIENT`;空白拖拽区才返回 `HTCAPTION`。搜索框曾当作 HTCAPTION 导致输入框点不到。

### (C) 胶囊上方"白色横线 + 小白圆角块"(所有页面,反复修)

- 根因:横向滚动条,来自 `QAbstractScrollArea` 后代在暗色下露出的白色横条+残留圆角。
- 解法(多轮 `0dce254`→`192317f`→`d40ccf0`→`76e7c4e`):主窗口级统一把所有 `QAbstractScrollArea` 后代横向 `AlwaysOff` + QSS 高度归零,并去掉胶囊白色顶部高光线/弱化白描边/隐藏空来源徽标。**新页面要把横向滚动条彻底禁用。**

### (D) 歌单列表只显示序号、不显示歌名

- 现象:本地/在线/收藏/搜索列表只见 1-17 序号,歌名/歌手/专辑/时长全空;底部播放栏却能正确显示当前歌。
- 根因:`ui/SongListModel` 继承 `QAbstractListModel`,`columnCount()` 恒为 1;而 `SongListView`(QTableView)的 delegate 按 `index.column()` 画 5 列。模型只给 1 列 → 只渲染序号列。
- 解法:改 `QAbstractTableModel`,`columnCount()=5`;`setPlayingId` 里 `index(0,0)/index(n,4)`(表模型 `index` 需要列参)。`a4dcb21`

### (E) 扫码登录"没反应 / 一直未登录 / 数秒后闪退"

- 现象:手机扫码确认后 PC 不认登录,左下角一直"未登录",甚至数秒后闪退。
- 解法(多轮 `37d808e`、`a4dcb21`):
  - `login/qr/check` 成功码兼容 **802 和 803**;不覆盖已写入的 uid/昵称。
  - `AccountDialog::loginNetease()` 之前只下载头像、没写 uid/昵称 → `onlineUid()` 恒 0 → 判为未登录。现从 `/login/status` 的 `profile` 补齐 uid/昵称;并用 `QPointer` 防登录回调晚到时对话框已关闭导致的闪退。
- 未完成:仅代码修复,未端到端验证(需真人扫码)。若仍"未登录",查 `/login/status` 返回结构(注意 `login/qr/check` 通常只回 cookie、不回 profile)。

### (F) 含中文路径的本地歌元数据读不出(影响面大)

- 现象:`孙燕姿 - 样子.ogg`(中文名)入库后歌手/专辑空、时长 0;改成 ASCII 名就能读。
- 根因:TagLib 在 Windows 用窄字符 `fopen` 打开含中文路径失败 → `FileRef` 为空 → 只用文件名兜底。
- 解法:`core/TagReader.cpp` 按平台传 wchar_t:Windows 用 `filePath.toStdWString()` 构造 `TagLib::FileName`。`6259a1d` **修复所有含中文路径的本地歌。**

### (G) .mgg/.mflac 加密容器让扫描崩溃

- 现象:扫 `.mgg` 崩溃;`--screenshot`+扫 `.mgg` 并发触发 `0xC0000002`。
- 解法:`kSupportedSuffixes` 加 `mgg`;`TagReader::read` 对 `.mgg/.mflac` 跳过 TagLib,按文件名兜底。仅入库显示,**不能解码播放**(解密线未做)。`0590935`

### (H) ogg 不能播放/不支持

- 根因:Windows 默认 WMF 后端不支持 ogg/vorbis;扫描器也没 ogg。
- 解法:`LibraryService` 加 `ogg`;`main.cpp` 启动时 `qputenv("QT_MEDIA_BACKEND","ffmpeg")`。FFmpeg dll(`avcodec-61` 等)在 `C:/Qt/6.11.1/mingw_64/bin`。`4aa9726`

### (I) 播放器胶囊布局

- 现象:进度条与播放键不对齐/堆两行;模式/音量位置乱。
- 解法:左右各 240px 等宽,进度条居中对齐播放键;上一首/播放/下一首居中,模式/静音/音量右侧。`f98f4cc`

### (J) 环境级坑(别浪费时间重踩)

- **PowerShell 直接调 g++/cmake 会破坏参数**(尤其 `-Wl,-subsystem,console`)→ 用 `.bat` 包装。
- **独立 g++ 编译含 Qt 头文件的小程序会静默崩溃/卡死**(`cc1plus` 0 CPU、exit 1、空日志)——别写独立 probe;用"应用自身 `--screenshot` + 临时 DB"验证即可。
- 应用运行、AppData 读写都要沙箱外(require_escalated)。

---

## 4) 用户对工作逻辑的约束

### 工作流

1. **HTML 原型先行**: `design/prototype/index.html`(纯 HTML/CSS/JS)。用户浏览器改 CSS 定稿、冻结后才同步到 Qt。
2. **每轮改完即 commit + push 到 wycloudforge**,所有改动都推。
3. **每次改完重启窗口给用户看效果**(Start-Process,窗口保持打开)。
4. **先确认再动手**:涉及方向性问题先让用户确认需求,别急着写代码。
5. 需要外部参考时先搜 GitHub 开源项目。

### 视觉/主题(硬性)

- 深色极光背景(`ui/AuroraBackground`:QTimer ~30ms + 多组 QLinearGradient 缓慢流动)。
- **全局无任何 1px 分隔线**;分区靠间距/留白/半透明层次;分区背景连续、一体。
- **底部悬浮玻璃胶囊播放器**(不可拖动、位置固定),iOS 质感 = backdrop-filter + 半透明 + 饱和度/亮度 + 白色渐变膜 + 边缘高光 + 投影 + 斜向反光;**已去掉胶囊白色顶部高光线**。
- **横向滚动条彻底禁用**(所有 QAbstractScrollArea 后代 + 全局 QSS 高度归零)。
- 主色 `#EC4141`、悬停 `#F04A4A`、正文 `#E8E8E8/#9A9AA5/#6E6E7A`、页面底深黑 `#12121A`、卡片/侧栏/播放器 `rgba(255,255,255,0.05)`、阴影加深、字体 Microsoft YaHei UI;数值以 `design/tokens.json` 为准,偏差 >2px 不通过。
- 图标自绘 SVG(`ui/SvgIcon.h::makeSvgIcon`),替换即换 `resources/icons/`。

### 产品边界(硬性)

- **不绕过任何 VIP/DRM**;VIP 只能靠登录会员账号播完整。
- 曾拒绝从 `yyms5.com` 下未授权版权歌,只导入用户合法拥有的本地文件。
- **多源架构**:`Song::source`(0 本地 / 1 网易云 / 2 QQ 预留);`MusicSource` 虚接口,`NeteaseApiClient` 实现网易云;QQ 只是占位"未接入",后续 `QqMusicSource`。
- **登录后只拉歌曲+歌曲相关内容**(每日推荐、推荐歌单、封面),**不获取评论等社交**;每平台独立登录/退出;头像可选网易云/本地上传。

### 导航结构(已定型)

- 侧边栏四导航:**推荐 / 收藏(红心)/ 本地歌单(含导入)/ 自建歌单**。自建歌单 = "自建歌单"行 + 右侧"＋"(悬浮提示"创建歌单",**无文字按钮**),列表只列自建歌单(过滤 id=1 红心歌单),可改名/换封面/写简介。
- 搜索页、正在播放页**不占主导航**(经顶部搜索框、播放栏歌词键/点封面进入)。
- 左下角账号面板(头像+昵称+设置入口),点账号弹 `AccountDialog`(网易云真登录/退出,QQ 占位);**设置键也从标题栏挪到左下角**,标题栏只留最小/最大/关闭 + 搜索框。

---

## 5) 当前进度(架构/UI/功能)

### 架构分层(稳定)

- `core`:`ApiService`(探测/自拉起 API)、`NeteaseApiClient`(HTTP+JSON,含搜索/歌曲/URL/歌词/专辑/歌手/歌单/评论/登录/QR/收藏)、`LibraryService`(多文件夹递归扫描+QFileSystemWatcher 增量+拖拽导入+`ON CONFLICT(path) DO UPDATE`)、`PlayerService`(QMediaPlayer+QAudioOutput,在线缓存优先)、`PlaylistController`(SQLite 歌单,含封面/简介)、`LyricsLoader/LrcParser`(行级同步,编码识别)、`TagReader`(TagLib+wchar_t,加密容器短路)、`SearchService`、`SettingsService`、`MusicSource.h`。
- `ui`:主窗口栈(0-6)、`AuroraBackground`、`TitleBar`、`SideBar`、`PlayerBar`(胶囊)、`AccountPanel/AccountDialog`、`LibraryPage/FavoritesPage/RecommendPage/SelfPlaylistsPage/SongListPage/OnlinePage/SearchPage/PlayingPage`、`SongListView/SongListModel`、`LyricWidget`、`LoginDialog`、`SettingsDialog`、`PlaylistEditDialog`、`LyricEditorDialog`、`CommentsDialog`(入口已从正在播放页移除)、`CoverProvider/CoverCard`、`ProgressSlider`。
- 测试:`tests/` 有 LRC/tagreader/歌单/播放器单测(ctest),需保持通过。

### 已实现功能

- 本地:多文件夹扫描、拖拽导入、缺失标记、封面(内嵌→cover.jpg/png→首字渐变色占位)、缩略图缓存;播放/暂停/上下首/拖动进度/音量/静音;列表循环/单曲循环/随机;记住最后曲目与进度。
- 在线:搜索/播放/歌词(含翻译)/专辑歌手详情/歌单广场/排行榜/每日推荐/私人FM/评论(接口保留、UI 入口移除)/扫码登录/我的歌单/红心;在线歌曲并入 `songs` 表,按 `source/online_id` 区分;**最近听过缓存到磁盘**(`song_cache` + LRU,默认 200 首/2GB,可一键清空),断网可回听;来源标记(云图标/离线角标/失效灰)+ 来源过滤(全部/本地/在线/已缓存)。
- .ogg、.mgg(仅入库)已支持;TagLib 中文路径、列表多列渲染、登录写 uid/昵称 已修。

### 最近 5 个 commit

```
1d93e61  歌单行高48→64/歌词行距1.6→2.2/歌词滚轮预览
a4dcb21  SongListModel 改多列表 + 登录补齐uid/昵称 + QPointer防闪退
6259a1d  TagLib wchar_t 读中文路径元数据
4aa9726  支持ogg + 固定FFmpeg后端
0590935  支持.mgg/.mflac(TagReader跳过解析防崩溃)
```

---

## 6) 已知待办 / 未决问题

1. **登录端到端未验证**:扫码后是否变"已登录"需用户实测;若仍不行,查 `/login/status` JSON 结构(`data.profile` 字段、`login/qr/check` 只回 cookie 不回 profile)。
2. **.mgg/.mflac 解密播放未做**:能入库显示,`PlayerService` 无法解码;须 mgg→flac/mp3 解密,来源须合法。
3. **QQ 音乐未实现**:仅账号占位;后续 `QqMusicSource`。
4. **"样子"这首 ogg 未补封面**(用户可能想要)。
5. **windeployqt 打包免安装目录**:未在本轮验证。
6. **`.mgg` 在 `--screenshot` 下扫描会崩**:截图验证避开带 `.mgg` 的目录,或正常模式验收。
7. **API 需在 3000 端口运行**:在线功能失败先确认 `http://127.0.0.1:3000` 是否 200。

---

## 7) 新模型一句话速查

> 重建:先 `Stop-Process NeteaseClone`,再 `cmd /v:on /c "%TEMP%\netease_build\run_ninja1.bat"`;运行用 require_escalated `Start-Process`;验证用临时 DB + `--page 2 --screenshot` 隔离截图;改完 `git commit` + `git push origin main`。AppData/真实库、GUI 都要沙箱外。别写独立 Qt probe(cc1plus 静默崩),别让 PowerShell 直接调 g++/cmake,别在 .bat 写中文路径。视觉=深色极光+无1px分隔线+底部不可拖毛玻璃胶囊+横向滚动条彻底禁用。版权=不绕过 VIP、不下未授权歌曲。
