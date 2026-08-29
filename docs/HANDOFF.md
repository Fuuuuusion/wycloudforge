# 仿网易云播放器 · 上下文交接文档

> 给接手此项目的新模型/新会话。读一遍即可无缝衔接,不必依赖此前的对话记录。

## 0) 最关键的三件事

- **仓库**: https://github.com/Fuuuuusion/wycloudforge 。当前在 **main**,HEAD 以最新提交为准,需与 `origin/main` 同步。
- **工作目录**: `C:\Users\Fusssssion\Documents\ChatGPT\仿网易云播放器`(**中文路径**,是很多坑的来源)。
- **构建/运行必须按 §2 流程**,否则会静默编译失败或看不到窗口。

---

## 1) 项目概况与技术栈

Windows 桌面音乐播放器,仿网易云(经典红 + 固定深色主题),本地 + 在线双模式,完全可离线。

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

### 编译 / 测试错误记录

- 2026-08-29:直接调用 `%TEMP%\netease_build` 的 Ninja 时,`g++.exe` 在编译阶段无诊断退出。原因是当前终端没有把 `C:\Qt\Tools\mingw1310_64\bin` 加入 `PATH`;可行方案是使用 `run_ninja1.bat`,或在同一 `cmd` 会话中先加入 `C:\Qt\Tools\Ninja` 与 MinGW bin 后再运行 Ninja。无需重配 Qt。
- 2026-08-29:`LibraryService.cpp` 把含引号/斜杠的原始正则字符串移到 `Q_OBJECT` 类之前后,AutoMoc 报 `No relevant classes found`,链接时报 `undefined reference to ScanWorker::staticMetaObject/vtable`。原因是 MOC 误解析原始字符串;可行方案是改用普通转义字符串,并确认 AutoMoc 重新生成后再链接。
- 以后每次编译或测试失败都在本节追加:命令、关键错误、根因、是否属于环境问题、已验证解决方案。现有解决方案失效且需要改变本机配置时先询问用户。

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
- 当前界面已移除极光背景、渐变、动效、毛玻璃、阴影和播放器背底截图;仅保留固定深色面板与网易云红色强调色。

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
- 根因与解法(多轮 `37d808e`、`a4dcb21` 及 2026-08-29 修复):
  - 网易云二维码状态必须严格区分:`800` 过期、`801` 等待扫码、`802` 已扫码等待手机确认、**只有 `803` 才是授权成功**。旧代码误把 `802` 当成功，导致二维码在确认前消失，并拿空 cookie 查询账号。
  - 轮询增加请求防重入；所有登录接口携带毫秒时间戳，避免退出后重新登录命中旧响应。
  - `803` 后先保存 cookie，再重试 `/login/status`，只有取得有效 uid 才关闭二维码窗口；头像转为后台同步，不再阻塞登录完成。
  - `AccountDialog` 不再重复发起另一套账号同步，避免对话框关闭后的异步回调竞争。
- 仍需真人扫码完成端到端确认；重点观察 `801 → 802 → 803` 状态转换和登录后推荐内容刷新。

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

### (K) 重启后歌单/收藏/推荐看似丢失

- 真实库仍有自建歌单，但 `SideBar::rebuildPlaylistButtons()` 每次刷新只删按钮、不删旧 `stretch`，新按钮会被逐步推到可视区域外；重建时必须清空全部 layout item。
- 在线搜索页曾把在线列表行号映射到本地结果，收藏/加入歌单拿到错误 id 后被数据库校验拒绝；`SearchPage::currentSongs()` 必须按当前页返回本地或在线歌曲。
- `ApiService` 自启动成功后曾丢失 `ensureRunning()` 的回调，导致登录状态仍显示但推荐页不刷新；轮询成功/失败都必须回调调用方。
- 推荐页必须在 API 启动和登录校验前先加载 `recommend.json`，服务就绪后再在线刷新，避免启动时长期空白。
- SQLite 主连接和扫描连接均设置 `busy_timeout=5000`，避免扫描写入期间收藏/歌单操作因瞬时锁竞争静默失败。
- 验证方法：复制真实 DB 到隔离目录，写入收藏及自建歌单关联，连续两次 `--screenshot` 启动后查询 `playlist_songs` 仍存在，并目视确认收藏、自建歌单、推荐页。

### (L) 真实库扫描期间重启导致歌单空白 / 播放有进度但无声

- 后台扫描连接曾在 `QSqlQuery` / `QSqlDatabase` 句柄仍存活时调用 `removeDatabase()`，退出又只等待 3 秒；现在扫描支持中断，严格先析构查询和连接句柄，再移除连接，退出会等待安全收尾。
- 主库启动执行 `PRAGMA quick_check`；索引损坏时先把 db/wal/shm 备份到 `db-backups/automatic-*`，再 `REINDEX` 并复检，同时清理悬空 `song_cache` / `playlist_songs`。
- 数据库恢复现在使用 `FULL` checkpoint 后的毫秒级唯一备份；`REINDEX` 报错后仍以复检结果为准。若确认是 WAL 损坏，会在安全备份后关闭全部连接、丢弃 WAL/SHM 并重新验证已落盘主库。正常退出还会执行 `TRUNCATE` checkpoint。
- 如果主库本身仍无法通过检查，会从 `NOT INDEXED` 扫描中尽可能迁移歌曲、歌单、歌单关联、缓存和最近播放记录到新库；损坏源文件会随自动备份保留，无法读取的个别行会被跳过并记录警告。
- 正式应用使用 `QLockFile` 限制为单实例，避免两个播放器进程同时维护同一个 SQLite/WAL；带 `--db` 的隔离测试不受此限制。再次启动时不再弹出并滞留“已有窗口”提示，而是恢复、置顶并激活现有主窗口后退出第二进程。
- 播放前重新绑定 Windows 当前默认音频输出；优先使用 `Song::cachePath`，缓存无法解码时清掉失效记录并回退在线地址，播放失败会在播放器来源徽标显示原因。
- 歌单添加改成事务写入、提交后 membership 回查；成功显示“已添加到…”提示，失败显示具体错误。创建成功留在自建歌单总览，避免详情页与侧栏高亮错位。

### (M) 在线下载歌曲重启后显示“暂无歌词”

- 根因一：`LibraryService::reloadSongs()` 和 `PlaylistController` 组装查询结果时，曾对所有在线歌曲强制清空 `lyricPath`。
- 根因二：`LyricsLoader` 只使用 `song.filePath` 寻找旁挂 LRC，而在线歌曲这个字段是 `netease://...` 虚拟路径，因此完全忽略了永久下载 MP3 旁边已经存在的 `.lrc`。
- 根因三：正在播放页优先请求在线歌词，接口失败或返回空内容时会主动清掉歌词；异步回调也没有请求代次校验，快速切歌时旧响应可能覆盖新歌。
- 修复：`LyricsLoader` 统一按“显式歌词路径 → 永久下载旁挂 → 自动缓存旁挂 → 本地导入旁挂 → 内嵌歌词”解析，缓存键也改为真实歌词/音频路径。数据库重载、歌单和最近播放查询都会恢复有效 `lyricPath`。
- 正在播放页先立即显示本地 LRC，只有在线接口返回有效原文时才替换并补入翻译/音译；接口失败或空内容保留本地结果。每次加载带 generation 校验和 `QPointer` 生命周期保护，防止旧请求串歌或退出期间回调已销毁页面。
- 歌词编辑器现在写入真实下载/缓存/本地音频旁的 LRC，不再尝试写入 `netease://` 虚拟路径；在线歌曲没有本地音频时明确禁用保存。
- 验证：新增在线下载 LRC、显式路径优先级、缺失文件、数据库重载回归测试；四项自动化测试全部通过。用真实库副本恢复孙燕姿《风衣》，截图已确认展示作词/作曲和后续逐行歌词，不再显示“暂无歌词”。
- 本轮正式构建和四项测试未发生编译/测试错误。本机 `sqlite3` 和系统 `python` 不在 PATH；需要隔离查询 SQLite 时的可行替代是使用 Codex 工作区依赖中的显式 Python 路径及标准库 `sqlite3`。

---

## 4) 用户对工作逻辑的约束

### 工作流

1. **HTML 原型先行**: `design/prototype/index.html`(纯 HTML/CSS/JS)。用户浏览器改 CSS 定稿、冻结后才同步到 Qt。
2. **每轮改完即 commit + push 到 wycloudforge**,所有改动都推。
3. **每次改完重启窗口给用户看效果**(Start-Process,窗口保持打开)。
4. **先确认再动手**:涉及方向性问题先让用户确认需求,别急着写代码。
5. 需要外部参考时先搜 GitHub 开源项目。

### 视觉/主题(硬性)

- 固定深色背景(`ui/AuroraBackground` 仅保留静态底色),全局无任何 1px 分隔线。
- 分区使用固定深色面板与网易云红色强调色;不使用渐变、透明玻璃、阴影或动态视觉效果。
- **底部播放器**保持位置固定,使用固定深色背景;不再抓取背底、不再模糊或叠加玻璃高光。
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
- 左下角账号面板(头像+昵称+设置入口),点账号弹 `AccountDialog`(网易云与 QQ 音乐登录/退出);**设置键也从标题栏挪到左下角**,标题栏只留最小/最大/关闭 + 搜索框。

---

## 5) 当前进度(架构/UI/功能)

### 架构分层(稳定)

- `core`:`ApiService/QqApiService`(两来源独立探测与自拉起)、`NeteaseApiClient/QqMusicSource`(统一来源接口)、`MusicSourceRegistry`(按歌曲来源路由)、`CredentialStore`(Windows DPAPI)、`LibraryService`(多文件夹递归扫描+QFileSystemWatcher 增量+拖拽导入+缓存/下载/封面/LRC 管理)、`PlayerService`(QMediaPlayer+QAudioOutput,永久下载→自动缓存→在线 URL)、`DownloadService`(下载队列、进度、取消、失败与重试)、`PlaylistController`(SQLite 歌单,含封面/简介)、`LyricsLoader/LrcParser`(行级同步,编码识别)、`TagReader`、`SearchService`、`SettingsService`。
- `ui`:主窗口栈(0-6)、`AuroraBackground`(固定背景)、`TitleBar`、`SideBar`、`PlayerBar`、`AccountPanel/AccountDialog`、`LibraryPage/FavoritesPage/RecommendPage/SelfPlaylistsPage/SongListPage/OnlinePage/SearchPage/PlayingPage`、`SongListView/SongListModel`、`LyricWidget`、`LoginDialog`、`SettingsDialog`、`PlaylistEditDialog`、`LyricEditorDialog`、`CommentsDialog`(入口已从正在播放页移除)、`CoverProvider/CoverCard`、`ProgressSlider`。
- 测试:`tests/` 有 LRC/tagreader/歌单/播放器单测(ctest),需保持通过。

### 已实现功能

- 本地:多文件夹扫描、拖拽导入、缺失标记、封面(内嵌→cover.jpg/png→首字纯色占位)、缩略图缓存;播放/暂停/上下首/拖动进度/音量/静音;列表循环/单曲循环/随机;记住最后曲目与进度。
- 在线:搜索/播放/歌词(含翻译)/专辑歌手详情/歌单广场/排行榜/每日推荐/私人FM/评论(接口保留、UI 入口移除)/扫码登录/我的歌单/红心;在线歌曲并入 `songs` 表,按 `source/online_id` 区分;**最近听过缓存到磁盘**(`song_cache` + LRU,默认 200 首/2GB,可一键清空),断网可回听;来源标记(云图标/离线角标/失效灰)+ 来源过滤(全部/在线/已缓存)。
- 永久下载: `songs.download_path` 与在线身份、收藏、歌单关系独立保存;默认目录为 `Music\NeteaseClone Downloads`,可在设置中更改。下载先写临时文件再原子改名,每首任务开始时重新获取 URL,临时链接失效自动重试一次;有效文件启动时校验,外部删除后自动恢复为“未下载”。下载管理页支持逐首进度、取消、失败原因和重试,完成任务从进行中列表移除。
- 批量操作:所有歌曲列表支持选择后批量收藏/取消收藏、添加到已有或新建歌单、下载;列表行和播放胶囊均显示收藏/下载状态并禁止重复下载。
- 本地页筛选为`全部 / 本地导入 / 已缓存 / 已下载`;永久下载不计入“已缓存”。删除按来源执行:本地导入默认仅移出曲库,在线缓存仅清缓存,在线下载仅删永久文件,无本地文件则移除在线记录;批量删除按来源汇总结果。
- .ogg、.mgg(仅入库)已支持;TagLib 中文路径、列表多列渲染、登录写 uid/昵称 已修。

### 本轮下载与批量操作实现

- `Song` 增加 `downloadPath` 与有效文件判断;SQLite 启动迁移补充 `songs.download_path`,并处理旧库、损坏库恢复和失效下载清理。
- `MusicSource` 下载接口支持进度、取消任务 ID 和详细错误;网易云实现流式写临时文件后原子提交,封面/自动缓存接口保持独立。
- `SettingsService` / `LibraryService` 支持可配置下载目录、下载路径生成、下载记录设置与删除;默认目录为 `Music\NeteaseClone Downloads`。
- 永久下载目录会从本地音乐扫描中排除；启动和扫描完成时会清理旧版本误导入的 `source=0` 下载副本，保留在线歌曲、收藏和歌单关系。
- 播放缓存和永久下载完成后都会独立保存在线封面到 `covers/` 并写入 `songs.cover_path`，重启后继续显示封面。
- `DownloadPage` 维护队列和逐首状态;所有歌曲列表统一接入批量模式、复选框和来源相关删除策略。
- 播放路径固定为永久下载 → 自动缓存 → 在线 URL;清空缓存不会影响永久下载,删除永久下载不会影响收藏、歌单或缓存。
- 本地歌单仅显示本地导入歌曲或存在有效缓存/永久下载文件的在线歌曲;仅浏览但未产生本地音频的在线记录仍保留在数据库、收藏和自建歌单中,但不出现在本地页及其歌手/专辑详情。搜索页保持原有在线记录展示逻辑。
- 永久下载路径暂时不可访问时只显示为“未下载”,不再清空 `songs.download_path`;启动和数据库重载会按“歌手 - 歌名”及重名序号重新关联下载目录里的现有文件。清理旧版 `source=0` 下载副本前会把歌单、收藏、最近播放和播放统计迁移到对应在线记录。

### 启动数据库与卡顿加固

- SQLite 可选运行参数失败不再阻断可恢复数据库;恢复前会安全备份 db/wal/shm,释放所有查询句柄后再重建或替换;已验证索引损坏和 WAL 损坏副本可恢复并通过 `integrity_check`。
- 库变更 250ms 合并刷新；取消启动时对整个本地曲库逐首请求在线元数据，改为歌曲实际播放时按需补全。
- 本地扫描记录文件修改时间和大小；启动时未变化歌曲不再重复用 TagLib 读取标签/内嵌封面，也不执行无意义的逐首 SQLite 更新。新增、变化和旧库指纹迁移写入统一放在一个事务中，减少磁盘同步与库锁竞争。
- 封面写入使用独立 `songCoverChanged` 信号，不再触发全局 `libraryChanged` 和所有页面重建；界面封面变化按 450ms 合并刷新。历史在线封面详情每批最多 24 首、最多同时下载 3 张，API 就绪后延迟 3 秒启动，且不再由页面刷新递归触发补图。
- 切歌只更新歌曲列表的播放标记，不再重建本地曲库；歌手/专辑卡片仅在对应标签页打开时构建。封面通过 `QImageReader::setScaledSize` 按显示尺寸解码，避免先解压原始大图再缩小阻塞 UI。
- 真实库恢复前的损坏文件保存在 `C:\Users\Fusssssion\AppData\Roaming\NeteaseClone\NeteaseClone\db-backups\manual-before-restore-20260829-143755`;当前库已从 `automatic-20260829-110116-956` 恢复并验证完整性为 `ok`。
- 界面性能调整:所有视觉动效与毛玻璃路径已删除;播放器使用固定色背景,歌词切换直接定位,避免视觉渲染抢占主线程。

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

1. **扫码登录端到端仍需真人验证**:启动恢复现会用 `/login/status` 校验 cookie，并兼容 `data.profile` / `data.account`;但新扫码流程仍需用户实测。
2. **.mgg/.mflac 解密播放未做**:能入库显示,`PlayerService` 无法解码;须 mgg→flac/mp3 解密,来源须合法。
3. **QQ/微信扫码仍需真人端到端验收**:`QqMusicSource`、独立包装服务、并发搜索、推荐/只读云歌单、播放/下载/歌词/封面及双扫码状态机均已实现并通过契约测试；仍需用户分别用手机 QQ 和微信完成扫码、确认、重启恢复及微信未绑定账号场景。
4. **"样子"这首 ogg 未补封面**(用户可能想要)。
5. **windeployqt 打包免安装目录**:未在本轮验证。
6. **`.mgg` 在 `--screenshot` 下扫描会崩**:截图验证避开带 `.mgg` 的目录,或正常模式验收。
7. **API 需在 3000 端口运行**:在线功能失败先确认 `http://127.0.0.1:3000` 是否 200。
8. **真实数据库恢复记录**:当前库恢复后包含 204 首歌、3 个歌单、6 条最近播放和 1 条缓存记录;恢复前的 db/wal/shm 可从上面的手工备份目录取回。

---

## 7) 新模型一句话速查

> 重建:先 `Stop-Process NeteaseClone`,再 `cmd /v:on /c "%TEMP%\netease_build\run_ninja1.bat"`;运行用 require_escalated `Start-Process`;验证用临时 DB + `--page 2 --screenshot` 隔离截图;改完 `git commit` + `git push origin main`。AppData/真实库、GUI 都要沙箱外。别写独立 Qt probe(cc1plus 静默崩),别让 PowerShell 直接调 g++/cmake,别在 .bat 写中文路径。视觉=固定深色主题+无1px分隔线+固定色播放器+横向滚动条彻底禁用。版权=不绕过 VIP、不下未授权歌曲。

---

## 8) QQ 多源接入编译/测试记录（2026-08-29）

- 构建 `NeteaseClone` 失败：`ui/OnlinePage.cpp:289` 把强类型 `core::SourceId` 与整数 `1` 直接比较。根因是第一阶段将来源 ID 从裸 `int` 升级为 `enum class SourceId` 后，旧页面条件判断未同步。可行方案：改为 `core::SourceId::Netease`，后续新代码禁止使用来源魔法数字。该问题不是环境配置缺失。
- 直接从 PowerShell 调用 Ninja 构建 `tst_playlistcontroller` 失败：编译命令退出但没有任何 C++ 诊断。根因是该启动方式没有把 `C:\Qt\Tools\mingw1310_64\bin` 放入 `PATH`，MinGW `cc1plus` 在当前机器上会静默失败。可行方案：使用 `cmd` 构建脚本并显式设置 Ninja/MinGW `PATH`；不要把这类无诊断退出误判为源码错误。
- 更新后的 `tst_playlistcontroller` 首次运行有 12 项初始化失败：`cannot commit transaction - SQL statements in progress`。根因是字符串远端 ID 迁移前的 `PRAGMA wal_checkpoint(FULL)` 查询仍持有结果集，导致 SQLite 拒绝提交后续迁移事务。可行方案：备份并进入迁移事务前显式 `checkpoint.finish()`；测试初始化保留 `QVERIFY2(..., lastError)`，以后数据库打开失败可直接看到原因。该问题不是环境配置缺失。
- QQ 包装层首轮契约测试 1/3 失败：单首搜索夹具被归一成 3 首。根因是 QQ 原始 JSON 在歌曲、歌手和专辑对象里都使用通用字段 `mid`，递归解析误把歌手/专辑 MID 当作 `songmid`。可行方案：优先只接受 `songmid/song_mid/songMid`；仅当对象同时具备歌曲特征字段时才把裸 `mid` 视为歌曲身份。未知字段继续忽略。该问题不是环境配置缺失。
- 推荐页接入来源切换后构建失败：`RecommendPage.cpp` 创建并连接 `QButtonGroup` 时只有 `QPushButton` 间接提供的前置声明。根因是缺少完整类型头文件，编译器无法实例化对象或解析 `idClicked`。可行方案：显式包含 `<QButtonGroup>`；以后新增 Qt 控件时不要依赖其他头文件的间接声明。该问题不是环境配置缺失。
- 新增 `tst_multisourcesupport` 首次运行 2 项 DPAPI 测试失败，`CryptProtectData` 返回 Windows 错误 2；同一执行环境下用 .NET `ProtectedData` 探针也明确报告“当前线程用户上下文未加载用户配置文件”。根因是受限测试进程没有可用的 Windows 用户 DPAPI 配置文件，不是凭据实现或用户项目配置错误。可行方案：保留 DPAPI 实现，在正常桌面用户上下文中重跑凭据测试；服务签名、端口隔离等不依赖 DPAPI 的测试已通过，不能为适配受限测试环境而降级为明文或机器级加密。

### QQ 多源第二阶段完成项

- 本地 `@sansenjian/qq-music-api@2.6.0` 包装服务固定依赖与 lockfile，绑定 `127.0.0.1:3200`，返回稳定 `{ok,data,error}` 契约；服务签名验证通过前 QQ 搜索不会启用。
- QQ/微信双扫码统一状态机，扫码后保留二维码直到手机确认；切换方式、关窗和退出会取消任务并丢弃迟到回调，手动 Cookie 仅作为高级故障回退。
- 网易云与 QQ 凭据均迁移到 Windows DPAPI；QQ 账号、头像缓存、推荐缓存、音频、封面与 LRC 均按来源隔离。
- 搜索按来源并发且增量展示，QQ 离线不会拖住网易云；QQ 推荐歌曲和只读云歌单、字符串歌单 ID、专辑/歌手详情、播放、下载和歌词已接入来源注册表。
- 新增自动化覆盖 DPAPI 回读/迁移、错误服务签名、服务离线隔离、QQ 字符串身份、独立 LRC 重启恢复，以及网易云/QQ 混合队列的顺序、单曲循环和随机联播。
