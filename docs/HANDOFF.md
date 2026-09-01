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
- 2026-08-30:为诊断下载关联直接在中文工作区调用 `gcc/cmake`，复现 `cc1` 0 CPU 挂起、无诊断退出以及 CMake `-1073740791`；系统 `cmake` 也不在 PATH。根因是已知的中文源码路径与直接工具调用环境问题，不是本轮 C++ 源码错误。可行方案：数据库只读诊断使用 Codex 运行时显式 Python；正式构建继续使用 ASCII `%TEMP%\netease_build` 和已配置 MinGW PATH 的 Ninja 脚本。
- 2026-08-30:备份诊断脚本首次打印 JSON 报 `UnicodeEncodeError: gbk codec can't encode character`。根因是 PowerShell 控制台默认 GBK，备份歌曲元数据包含 GBK 不可表示字符；可行方案是显式把 Python `stdout` 设为 UTF-8。数据库查询本身没有失败。
- 2026-08-30:永久下载清单首轮回归中 `tst_playlistcontroller` 的 `downloadedLyricsSurviveReload` 1 项失败，重载后测试目录外的有效下载路径被按清单文件名错误迁到当前下载目录，导致旁挂 LRC 找不到。根因是清单读取无条件优先 `fileName`；修复为“有效绝对 `storedPath` 优先，原路径失效时才按当前下载目录 + `fileName` 迁移”，完整测试随后 `EXIT=0`。该问题不是环境配置缺失。
- 2026-08-30:UI worktree 首次配置独立 `%TEMP%\netease_ui_build` 时 CMake 报 `No CMAKE_CXX_COMPILER could be found`，显式传入编译器后旧失败缓存仍保留 `CMAKE_CXX_COMPILER-NOTFOUND`。根因是受限 PowerShell 进程未把 MinGW/Ninja 加入 `PATH`，且失败构建目录已缓存未找到状态，属于环境配置问题。已验证解决方案：在同一 PowerShell 进程把 `C:\Qt\Tools\mingw1310_64\bin` 与 `C:\Qt\Tools\Ninja` 加入 `PATH`，使用新的英文构建目录 `%TEMP%\netease_ui_controls_build`，并显式传入 `C:/Qt/Tools/mingw1310_64/bin/g++.exe`；Release 正式应用随后完整构建并链接成功。
- 2026-08-30:PlayerBar 播放按钮视觉改动后的 `tst_playerservice` 在受限进程及部分桌面重跑中，`playPauseAndPosition` / `cachedOnlineSongPlayback` 偶发进入 Playing 后 3 秒内 position 不推进，分别出现 2 项失败；同一未变测试二进制也有完整 `12 passed / 0 failed / 1 skipped` 的通过记录，失败用例单独重跑可通过。根因是当前 Windows 音频输出与 FFmpeg 后端的非确定性时序，不是本轮只涉及 `ui/PlayerBar.cpp` 与 QSS 的源码回归。已验证方案：保持 `QT_MEDIA_BACKEND=ffmpeg`，通过 `Start-Process -Wait` 在桌面用户上下文运行并对失败的音频位置用例重跑；不得因此修改 core、降低断言或把受限进程的音频位置结果误判为 UI 回归。
- 2026-08-30:播放按钮最终运行复核中，专用 UI 构建在 `--smoke`、`--screenshot` 和正常可见启动下均以 `-1073741819 (0xC0000005)` 退出；空音乐目录、独立数据库及独立 `APPDATA/LOCALAPPDATA` 仍复现，因此不是 `.mgg`、真实数据库或多实例问题。Windows Application Error 的固定偏移 `0xA43A5` 经 `objdump` 映射到 `ui::TitleBar::setMaximizedState(bool)` 首个成员解引用，调用方 `MainWindow::changeEvent` 可能在 `m_titleBar` 初始化前收到 `WindowStateChange`。正式构建无错误，本轮 PlayerBar 改动没有进入该调用路径；安全修复需要由主任务在专属 `app/MainWindow.cpp` / `ui/TitleBar.*` 边界内处理初始化顺序或空指针保护，UI 分支不得越界修改，修复前无法重新打开最终预览窗口。
- 以后每次编译或测试失败都在本节追加:命令、关键错误、根因、是否属于环境问题、已验证解决方案。现有解决方案失效且需要改变本机配置时先询问用户。
- 2026-08-31:UI 控件改造完成后的 Release 全量构建成功；首次完整 8 项 CTest 为 5/8。`tst_playerservice` 在 FFmpeg 正常探测全部 WAV 后由 CTest 报目标失败，未显示 Qt 断言；同一未变二进制随后用 `QT_MEDIA_BACKEND=ffmpeg` 单独立即重跑退出码为 0，符合此前已记录的 Windows 音频时钟偶发性，不是本轮只改 UI 绘制、事件分区和现有状态 signal 转发的回归。`tst_multisourcesupport` 显式 QtTest 报告为 8 通过、2 失败，失败仍是 `credentialRoundTripAndRemoval` / `legacyCredentialMigration` 的 DPAPI Windows 错误 2，根因是受限测试进程没有完整桌面用户配置文件；保持 DPAPI 后在完整桌面用户上下文复跑为 10 通过、0 失败，验证生产实现和本轮改动均无回归。`tst_searchpage` 为 7 通过、1 失败，既有 `fallsBackToCacheWithoutClearingHealthySource` 中网易云状态实际 `Error(3)`、预期 `Ready(2)`；本轮没有修改 SearchPage、SearchService 或搜索缓存逻辑，不为使 UI 分支全绿而越界改搜索业务。共享 UI 定向回归 `tst_songlistview` 与 `tst_playlistcontroller` 持续 100% 通过。

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
- 主库启动执行 `PRAGMA quick_check`；异常时先把 db/wal/shm 备份到 `db-backups/automatic-*`，再以 `immutable=1` 只读检查备份主库。主库健康即判定为 WAL 问题，关闭连接后只丢弃 WAL/SHM；禁止在带损坏 WAL 的连接上先执行 `REINDEX`。
- 只有主库本身也不健康时才尝试 `REINDEX`；仍失败则先从尚未丢弃 WAL 的连接用 `NOT INDEXED` 提取可读行，避免先删 WAL 导致已提交但未 checkpoint 的下载、歌单等记录永久丢失。正常退出继续执行 `TRUNCATE` checkpoint。
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

### (N) QQ 功能重启后永久下载文件存在但应用不显示

- 现场证据：当前 `library.db` 的 `integrity_check=ok`，有 213 行歌曲但 `download_path` 非空数为 0；下载目录 4 个音频均有效。`automatic-20260830-084618-285/library.db` 以 `immutable=1` 忽略损坏 WAL 后完整性为 `ok`，其中仍有网易云 `1:287398`《我不难过》、`1:516657213`《风衣》、`1:3422037209` GoldenCake 歌曲，以及微信登录 QQ 音乐下载的 `2:004NQRUH4anAYS`《了解》，四行路径和封面都完整。
- 直接根因：QQ 微信账号修复只改了 Node 包装层，并未执行删除歌曲的 SQL；但为加载新包装层多次强制结束正式应用，启动后命中损坏 WAL 修复。旧修复顺序先在带坏 WAL 的连接上 `REINDEX`，把原本健康的主库也改坏，随后从受损主库重建时丢了下载行；这就是“功能加完后”才出现的触发链。
- 放大因素：永久下载身份此前只保存在 `songs.download_path`；启动重关联只能把“歌手 - 歌名”文件匹配到当前仍存在的在线歌曲行。恢复后对应行已经不存在，文件名又没有 `source + remote_id`，所以 UI 的 `Song::isDownloaded()` 永远为假，并非本地页筛选自身出错。
- 修复：下载目录新增原子写入的 `.wycloudforge-downloads.json`，每首保存来源、字符串远端 ID、虚拟路径、歌名/歌手/专辑、封面、文件名及原路径。下载完成和封面落盘时同步更新，删除永久下载时同步移除；启动时清单可重建丢失歌曲行。旧版无清单且发现孤立音频时，只读扫描最近数据库备份主库并按完整路径恢复，成功后立即补写清单。
- **避错清单**：后端脚本更新需要重启时，优先正常关闭窗口并等待 SQLite `TRUNCATE` checkpoint，禁止直接 `Stop-Process -Force` 正式应用；只结束命令行明确属于项目的 Node 监听 PID，禁止停止所有 Node；永久下载身份必须以 `(source, remote_id)` 持久化，文件名只能展示或做旧版回退；数据库恢复前后必须核对“下载目录有效音频数 / 清单数 / 有效 `download_path` 数 / UI 已下载数”；WAL 异常必须先只读验证主库，禁止先 `REINDEX`；恢复或迁移不得删除下载清单，完成后必须重新导入下载关联。
- 回归覆盖：清单可在歌曲行被删除后恢复 QQ 字符串身份、元数据、封面和下载路径；网易云/QQ 同名歌曲不会串绑；无清单孤立文件可从 immutable 备份主库恢复；有效绝对路径优先，原路径失效时才按当前下载目录迁移；`tst_playlistcontroller` 完整通过。
- 真实迁移验收：修改前已备份到 `db-backups/manual-before-download-manifest-20260830-094024-826`；正式应用启动后当前库仍 `integrity_check=ok`，有效 `download_path=4`，下载清单 `version=1/count=4`。实机打开“本地歌单 → 已下载”确认正好显示这 4 首，播放器来源徽标为“已下载”；5 项 Qt 测试和 QQ 包装层 6 项契约测试均通过。

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
- 新增 `tst_multisourcesupport` 首次运行 2 项 DPAPI 测试失败，`CryptProtectData` 返回 Windows 错误 2；同一执行环境下用 .NET `ProtectedData` 探针也明确报告“当前线程用户上下文未加载用户配置文件”。根因是受限测试进程没有可用的 Windows 用户 DPAPI 配置文件，不是凭据实现或用户项目配置错误。可行方案：保留 DPAPI 实现，在正常桌面用户上下文中重跑凭据测试；服务签名、端口隔离等不依赖 DPAPI 的测试已通过，不能为适配受限测试环境而降级为明文或机器级加密。本轮微信账号修复回归中受限进程再次 `EXIT=1`，切换到桌面用户上下文后 `EXIT=0`，进一步确认该方案有效。
- 微信账号资料修复的首次端到端验证仍命中了旧版 `/v1/account/playlists`，随后手动启动新服务报 `EADDRINUSE 127.0.0.1:3200`。根因是修改包装层源码前由正式应用启动的 Node 进程仍在运行；Node 不会热重载 `server.js`，所以健康检查虽通过，进程内仍是旧实现。可行方案：用 `Get-NetTCPConnection` 定位 3200 监听 PID，再用 `Win32_Process.CommandLine` 确认它明确指向本项目 `本地部署/qq-api/server.js`，只停止该 PID 后重新启动；不要停止其他 Node 进程。重启后真实微信登录态验证为资料完整、2 个云歌单和 30 首推荐歌曲。
- 微信账号修复后的首次 Qt 回归测试中，`tst_lrcparser`、`tst_tagreader`、`tst_playlistcontroller`、`tst_multisourcesupport` 均无诊断退出，`tst_playerservice` 未在等待窗口内返回。根因是测试命令误用了不存在的 `C:\Qt\6.8.3\mingw_64\bin`，且不应把涉及共享运行时和音频后端的测试并行启动；本机实际版本为 `C:\Qt\6.11.1\mingw_64\bin`。可行方案：测试命令显式把 `C:\Qt\6.11.1\mingw_64\bin` 和 `C:\Qt\Tools\mingw1310_64\bin` 加入 PATH，逐项检查退出码，并为播放器测试设置 `QT_MEDIA_BACKEND=ffmpeg`；不需要重装或修改系统级 Qt 配置。修正后原四项与多源测试均 `EXIT=0`。

### QQ 多源第二阶段完成项

- 本地 `@sansenjian/qq-music-api@2.6.0` 包装服务固定依赖与 lockfile，绑定 `127.0.0.1:3200`，返回稳定 `{ok,data,error}` 契约；服务签名验证通过前 QQ 搜索不会启用。
- QQ/微信双扫码统一状态机，扫码后保留二维码直到手机确认；切换方式、关窗和退出会取消任务并丢弃迟到回调，手动 Cookie 仅作为高级故障回退。
- 网易云与 QQ 凭据均迁移到 Windows DPAPI；QQ 账号、头像缓存、推荐缓存、音频、封面与 LRC 均按来源隔离。
- 搜索按来源并发且增量展示，QQ 离线不会拖住网易云；QQ 推荐歌曲和只读云歌单、字符串歌单 ID、专辑/歌手详情、播放、下载和歌词已接入来源注册表。
- 新增自动化覆盖 DPAPI 回读/迁移、错误服务签名、服务离线隔离、QQ 字符串身份、独立 LRC 重启恢复，以及网易云/QQ 混合队列的顺序、单曲循环和随机联播。

---

## 9) 批量操作可达性修复（2026-08-30）

- 根因：`SongListView` 把“批量操作”入口放在 `m_batchBar` 内，初始化 `setBatchMode(false)` 又隐藏整个工具条；`setBatchMode()` 还是私有函数且没有快捷键或右键入口，因此底层信号虽已接线，用户无法进入批量模式。该缺陷从 `8faadfb` 初版即存在，不是后续 QQ 多源改动引入。
- 修复：批量工具条和 42px 顶部区域始终存在；普通模式只显示“批量操作”，进入后显示选择数量、全选、清空、收藏、取消收藏、添加歌单、下载、按来源删除和完成。窄窗口把低频操作收进“更多”菜单，避免按钮截断。
- 批量选择从可变列表行号改为稳定身份：在线歌曲使用 `(source, remote_id)`，本地歌曲使用数据库歌曲 ID；列表刷新、排序或异步补充封面时保留仍存在的选择并清理已消失歌曲。
- `PlaylistController::addSongsBatch/setFavoritesBatch` 使用单个 SQLite 事务，发生无效歌曲等错误时整体回滚；成功后只重载并发出一次歌单刷新信号，避免逐首事务和界面反复重建。
- 批量下载会汇报加入与跳过数量；批量删除按“移出本地曲库 / 删除永久下载 / 删除缓存 / 移除在线记录 / 失败”汇总，不再只显示笼统完成提示。
- 新增 `tst_songlistview`，覆盖入口始终可见、批量模式切换、选择数量、稳定身份跨重排保留、消失歌曲选择清理和窄窗口“更多”菜单；`tst_playlistcontroller` 增加批量事务、重复跳过、单次刷新和错误整体回滚测试。

### 本轮编译/测试记录

- `NeteaseClone`、`tst_playlistcontroller`、`tst_songlistview` 首次编译均成功，无源码编译错误。
- 首轮六项 `ctest -j1` 中，LRC、标签、歌单、播放器和新增批量 UI 五项通过，`tst_multisourcesupport` 在受限进程中无诊断退出。根因与既有记录一致：该测试包含 Windows DPAPI，受限进程没有完整桌面用户配置文件。可行方案仍是保持 DPAPI 实现，在正常桌面用户上下文单独重跑；本轮重跑 `EXIT=0`，不需要修改代码或系统配置。

---

## 10) 折中搜索方案实施记录（2026-08-30）

### 阶段 1 编译记录

- 首次正式构建在链接 `bin/NeteaseClone.exe` 时失败：`ld.exe: cannot open output file ... Permission denied`。此前 core、ui 和 `MainWindow.cpp` 均已编译成功。根因是正式应用进程仍在运行并锁定旧 EXE，链接器无法原地覆盖；这不是源码或工具链配置错误。可行方案是先向明确位于本项目构建目录的 `NeteaseClone` 进程发送正常关闭请求，等待 SQLite checkpoint 和进程退出后重新运行单线程 Ninja；禁止为解决文件锁直接强制结束应用。
- 正常关闭应用后重新链接成功；随后单线程构建正式应用和全部测试目标成功。6 项 Qt 自动化测试全部通过，其中 `tst_playerservice` 已增加统一搜索 DTO、字符串远端 ID、来源名次、generation 透传和不支持分类错误测试。
- 使用隔离数据库打开搜索页并执行 `--smoke`，正式应用退出码为 0；未触碰真实音乐库。

### 阶段 2 编译/测试记录

- 首次构建 `tst_multisourcesupport` 未进入编译：当前 PowerShell 会话找不到 `cmake` 命令。根因是 `C:\Qt\Tools\CMake_64\bin` 未加入该会话的 `PATH`，不是源码错误，也不代表用户缺少 Qt/CMake。可行方案：继续使用 `C:\Qt\Tools\CMake_64\bin\cmake.exe` 绝对路径，并显式提供 Ninja、MinGW 与 Qt 6.11.1 运行时路径；无需修改系统级环境变量。
- 改用 CMake 绝对路径后，Ninja 启动 `g++.exe` 编译 `mocs_compilation.cpp` 和 `MusicSource.cpp` 时无任何 C++ 诊断便退出。根因是 PowerShell 直接启动的构建子进程仍缺少 `C:\Qt\Tools\mingw1310_64\bin` 运行时路径，与此前记录的 MinGW `cc1plus` 静默失败一致；不是源码错误。可行方案：在独立 `cmd` 会话内显式设置 CMake、Ninja、MinGW 和 Qt 6.11.1 的 `PATH` 后再构建，不修改全局环境。
- 工具链路径修正后的首次真实编译在 `NeteaseApiClient.cpp` 失败：`QJsonArray` 没有 `constFirst()`。根因是误把其他 Qt 容器的访问接口用于 `QJsonArray`；与平台数据或环境无关。可行方案：先用 `isEmpty()` 防守，再调用 Qt 6 支持的 `first()` 读取首项。
- 在临时端口 3210 启动新版 QQ 包装服务进行真实分类、热搜和联想探测时，全部上游请求返回 502；服务日志显示对 `c.y.qq.com:443` 的连接被 `EACCES` 拒绝。根因是受限执行环境禁止该临时 Node 进程访问外网，路由和 `t=0/1/2/3/7` 参数已经实际执行；这不是源码错误，也不是用户缺少账号或网络配置。可行方案：正常关闭临时服务后，在获准联网的同一临时端口重跑真实探测；固定响应夹具继续作为离线契约回归保障。
- 获准联网后首次重启临时 QQ 服务报 `EADDRINUSE 127.0.0.1:3210`。根因是上一受限 PTY 收到 Ctrl+C 后，端口释放与新进程监听之间存在短暂竞态；复查 3210 已无监听进程，且正式应用使用的 3200 未受影响。可行方案：启动前先确认临时端口无监听，待端口释放后重试；不得为此停止 3200 上的正式 QQ 服务或其他 Node 进程。
- 再次检查发现 3210 的监听进程仅在获准上下文中可见，PID 60764，父命令明确为本轮 `set PORT=3210&& node server.js`；非强制 `Stop-Process` 对该跨上下文进程抛出 PowerShell 内部空引用且未结束监听。根因是测试进程跨执行上下文残留，不是播放器或 QQ 包装层故障。可行方案：仅对已核实的临时 PID 使用 Windows `taskkill /T` 且不加 `/F`，随后复查端口；严禁按进程名批量结束 Node，也不得触碰 3200 正式服务。
- 对已确认的 3210 临时 PID 执行不带 `/F` 的 `taskkill /T` 仍失败，Windows 明确报告该控制台子进程只能强制终止。根因是原 PTY 控制通道已经关闭，无法再投递 Ctrl+C/正常关闭信号；该临时实例没有数据库或登录任务。可行方案：仅对父命令已核实为 `PORT=3210` 的临时进程树使用 `/F`，结束后复查 3210；正式播放器和 3200 服务仍禁止强制结束。
- 联网实测 QQ 分类搜索时，单曲、歌手、专辑和歌词均返回有效结果，但旧 `client_search_cp` 的 `t=3` 在多组典型关键词下只返回歌手直达信息，歌单列表始终为空。根因是 QQ 旧搜索端点的歌单分类当前已退化，不是本地递归归一化器漏字段。可行方案：QQ 歌单分类改用当前 `music.search.SearchCgiService/DoSearchForQQMusicDesktop` 协议和 `search_type=3`，继续通过固定 `u.y.qq.com` 地址、15 秒取消超时及统一契约隔离；其他已验证分类保留现有端点，避免扩大变更范围。
- QQ 新歌单协议接入后的首次真人接口验证能返回 5 条歌单且字符串 ID 正确，但 `hasMore` 错误为 `false`。根因是实际响应的分页元数据位于 `req.data.meta`，实现误读为 `req.data.body.meta`。可行方案：按固定真实响应结构读取 `data.meta.nextpage/sum`，同时保留歌单创建者和 `listennum` 热度信号，并用夹具锁定这些字段。
- 为歌单搜索保留创建者和热度后，QQ Node 契约测试首次回归为 6/8，通过失败的 2 项均是既有账号歌单/通用歌单对象多出了空 `creator` 和 `popularity=-1`。根因是向共享 `normalizePlaylist()` 无条件增加字段，破坏了既有稳定契约形状；数据值本身无误。可行方案：通用归一化器只在上游实际提供有效字段时追加可选属性，搜索 DTO 再按需补空副标题；禁止为新场景无条件扩展已发布的共享对象形状。
- 网易云真实分类/发现接口首轮均返回 200，但随后复查歌词命中字段时 3000 端口已无监听并被系统拒绝连接。根因是此前监听者由应用管理，应用/探测结束后本地 API 子进程随生命周期退出，不是 `NeteaseApiClient` 解析失败或用户环境缺少包（项目内 `NeteaseCloudMusicApi/app.js` 已存在）。可行方案：使用项目内既有 Node 入口在独立临时端口启动服务进行真人接口复验，验收后正常关闭；不要依赖瞬时存在的应用托管进程。
- 临时网易云服务真实验收中，歌词分类返回歌曲正常，但首轮探针按 `lyrics[0].txt` 读取到空值；字段检查确认当前 API 的 `lyrics` 是带 `<b>` 高亮的字符串数组，而测试夹具误建成 `{txt: ...}` 对象数组。根因是解析器只兼容旧/假定结构，导致真实歌词搜索结果副标题为空。可行方案：同时兼容字符串和对象两种歌词项，选取首个非空片段并移除平台 HTML 高亮标签；固定夹具改为当前真实字符串结构。
- QQ 歌词分类真人响应确认每首歌曲的 `lyric` 是带 `<em>` 标签的精简命中片段，但包装层此前只复用了普通歌曲归一化结果，统一 DTO 的副标题退化为歌手名。根因是歌词专属字段在平台 JSON 转统一契约时被丢弃。可行方案：按字符串远端 ID 收集首行命中片段、去除 HTML 标签后写入歌词结果 `subtitle`；普通单曲契约保持不变，并增加固定夹具回归。
- 增加 QQ 歌词夹具后首次执行 `npm test` 报 `ENOENT ... 仿网易云播放器/package.json`。根因是测试命令误在仓库根目录运行，而 Node 包位于 `本地部署/qq-api`；不是依赖缺失或源码错误。可行方案：始终把 QQ 契约测试工作目录设置为 `本地部署/qq-api` 后执行 `npm test`，无需在仓库根目录新增 package 文件。
- 网易云 `cloudsearch type=1018` 真人探测返回 HTTP/业务码 200，但 `result` 稳定为空对象；直接依赖该接口会让“综合”分类看似成功却没有任何结果。根因是当前本地 API/上游组合的综合类型已不可用，不是 DTO 字段解析问题。可行方案：来源内部并发请求已验证的单曲、歌手、专辑、歌单和歌词分类，以固定类型顺序合并；允许子分类局部失败，仅在全部失败时判定来源失败。QQ 综合搜索采用相同局部容错策略。
- 阶段 2 首轮六项 `ctest -j1` 中，LRC、标签、歌单和批量 UI 四项通过，`tst_playerservice` 在输出正常 FFmpeg WAV 探测信息后无 Qt 断言失败，`tst_multisourcesupport` 也无诊断失败，总体 4/6。根因尚待单项退出码核实；现象与此前受限进程中的多媒体后端/DPAPI 用户上下文问题一致，不先归咎于搜索源码。可行方案：保持正确 Qt 6.11.1、MinGW 和 `QT_MEDIA_BACKEND=ffmpeg` 路径，逐项直接运行获取退出码，再在正常桌面用户上下文复跑环境敏感测试。
- 上述失败项首次单独重跑打印了 `EXIT=0`，但该结论无效：命令使用 `cmd ... & echo %ERRORLEVEL%`，`%ERRORLEVEL%` 会在测试进程运行前被预展开成旧值。根因是 Windows cmd 变量展开语义，不是测试恢复。可行方案：使用 `cmd /v:on` 并在测试后输出 `!ERRORLEVEL!`，或直接依据工具返回的进程退出码；以后禁止用同一行的 `%ERRORLEVEL%` 判断刚执行程序结果。
- 用显式 Qt 报告文件取得真实诊断：`tst_playerservice` 的 `unifiedSearchContract` 和 `unifiedSearchRejectsUnsupportedCategory` 失败。根因是阶段 2 新增 `NeteaseApiClient::search()` 覆盖后，测试对象的虚调用进入真实 `/cloudsearch` 实现，不再经过夹具仅重载的 `searchSongsPage()`；这是测试假对象过期，不是生产搜索失败。可行方案：这两项专门验证基类默认 DTO 适配器的测试显式调用 `source.MusicSource::search()`；网易云覆盖实现由 `tst_multisourcesupport` 本地 HTTP 契约覆盖。
- 同一报告确认 `tst_multisourcesupport` 仅有 DPAPI 写入/旧凭据迁移两项失败，均为 Windows 错误 2；其余服务隔离和新增网易云/QQ 分类发现测试全部通过。根因仍是受限测试进程没有完整桌面用户配置文件，与既有记录一致。可行方案：保留用户级 DPAPI，在正常桌面用户上下文重跑该测试；禁止为让受限环境通过而改为明文或机器级凭据。
- 修正播放器搜索夹具后，显式 Qt 报告运行结果为 14 通过、0 失败、1 个无音频设备跳过，但 CTest 再次只显示非零状态和 FFmpeg 输出，未包含失败用例名称。根因是 Windows GUI/QtTest 默认报告通道没有被 CTest 稳定捕获，导致诊断信息缺失。可行方案：所有 `add_test` 固定传入 `-o -,txt`，让 QtTest 文本报告进入标准输出；这不改变断言逻辑，但后续 CTest 失败必须能看到用例与行号。
- 提交前审查发现综合搜索分页若把外层 `offset` 原样传给每个约 `limit/5` 大小的子分类，第二页会跳过每类中间结果。根因是外层聚合页大小和子分类页大小不同，却混用了同一个绝对偏移。可行方案：先计算外层页码 `offset/limit`，再令子分类偏移为“外层页码 × 子分类 limit”；网易云与 QQ 使用同一规则，并用第二页来源名次断言锁定。

### 阶段 2 最终验证

- QQ 包装层 8 项 Node 契约测试全部通过，覆盖分类对象、当前/未知字段、热搜、联想、歌词片段和字符串远端 ID。
- 真人联网验证通过：QQ 单曲、歌手、专辑、歌单、歌词、热搜、联想及综合五分区均返回有效结果；综合示例返回 17 条并按固定类型顺序合并，歌单字符串 ID、创建者、热度和分页有效。网易云歌手、专辑、歌单、歌词、热搜、联想和默认词均返回有效数据。
- 正式 `NeteaseClone` 构建成功；完整 6 项 Qt 测试在正常桌面用户上下文 100% 通过。受限上下文中的 DPAPI 失败仍按既有环境边界处理，不修改加密实现。
- 使用空音乐目录和独立数据库打开正式搜索页执行 `--smoke`，最终退出码为 0；未读取或修改真实音乐库，也未关闭并行 UI 工作区的预览应用。

### 阶段 3 编译/测试记录

- 新增 `tst_searchservice` 后首次正式构建在测试源码编译阶段失败：`QCOMPARE` 报告收到 3 个参数。根因是预处理器会把 `QList<QPair<int, int>>({...})` 模板参数中的逗号当成宏参数分隔符；核心 `SearchService`、Qt 线程池任务和 UI 改动此前均已编译通过。可行方案：先把期望范围声明为局部 `QList<QPair<int, int>>` 变量，再将变量传给 `QCOMPARE`；该问题不是环境配置缺失。
- 新增搜索测试首轮运行结果为 8 通过、1 失败：纯歌手名“晴天”实际得到歌手+歌名组合分 750，而预期纯歌手完全匹配分为 700。根因是无空格组合判断只检查拼接后的“歌手+歌名”是否包含关键词，导致只落在单一字段中的关键词也被误判为跨字段组合。可行方案：拼接命中必须同时确认歌名和歌手任一字段都不能单独包含完整关键词，带空格的多词查询继续逐词确认至少命中一个歌手词和一个歌名词；该问题不是环境配置缺失。CTest 本轮仍未直接捕获 QtTest 文本，已用显式报告文件取得准确用例和行号。
- 阶段 3 首轮完整 Qt 回归为 6/7，通过项包含 DPAPI 多源测试、新增搜索测试和批量列表测试；`tst_playerservice` 的播放位置、混合来源顺序联播和缓存播放 3 项失败。显式 QtTest 报告显示播放器虽进入 `PlayingState`，时钟却不推进，并曾出现 `QAudioSink::start: QAudioFormat not supported by QAudioDevice`。这不是本轮搜索代码或播放器来源路由回归；首个诊断假设是测试 WAV 的 22.05 kHz 单声道格式与当前 Windows 音频端点兼容性不足，因此先用 44.1 kHz 双声道夹具做隔离复验，且没有修改生产播放器状态机或放宽断言。
- 将播放器测试夹具改为 44.1 kHz 双声道后，`QAudioFormat not supported` 警告消失，但隔离运行 `playPauseAndPosition` 时播放器位置仍停在 0，完整测试仍是同样 3 项失败，因此“只更换 WAV 格式”不是有效解决方案，测试改动已撤回。Windows Audio 与 AudioEndpointBuilder 服务均在运行，当前执行上下文能枚举非空端点并进入 `PlayingState`，但没有可推进的真实音频时钟；这是自动化运行上下文的音频端点问题。可行方案是后续在确有活动输出设备的交互桌面会话运行这些计时用例，或将媒体结束事件的队列逻辑拆成不依赖真实声卡的确定性单元测试；在此之前不能用放宽断言或伪造“通过”替代验收。

### 阶段 3 最终验证

- 正式应用和 7 个 Qt 测试目标构建成功；新增 `tst_searchservice` 为 9 通过、0 失败，覆盖匹配层级、歌手+歌名组合、Unicode NFKC/case folding、候选上限及总数、多处高亮、异步 generation 取消和保留在线元数据搜索。
- QQ 包装层 8 项 Node 契约测试全部通过。完整 Qt 回归中 6/7 通过，包括桌面用户上下文的 DPAPI 多源测试；唯一失败仍是上述真实音频时钟环境问题，搜索阶段没有修改 `PlayerService`，相关失败与可行后续方案已保留，未通过降低安全性或放宽断言处理。
- 使用空音乐目录和独立数据库打开正式应用搜索页并生成截图，应用自行退出且退出码为 0；搜索页、批量入口和异步本地快照初始化正常，未触碰真实音乐库，也未修改或关闭并行 UI 工作区。

### 阶段 4 编译/测试记录

- 搜索聚合、来源内热度百分位和稳定同曲版本选择完成后的首轮完整 Qt 回归为 7/8；`tst_searchservice`、`tst_searchpage`、`tst_multisourcesupport` 和 QQ Node 契约均通过，唯一失败目标仍为 `tst_playerservice`。本轮具体失败项为 `playPauseAndPosition`、`autoAdvanceInShuffleMode` 和 `cachedOnlineSongPlayback`，另有一个无音频设备用例跳过；播放器能够进入 `PlayingState`，但真实音频时钟不推进，失败项会在不同自动联播用例间波动。根因与阶段 3 已确认的自动化运行上下文音频端点问题一致，本阶段未修改 `PlayerService`。可行方案仍是在具有活动输出设备的交互桌面会话运行计时用例，或以后把队列结束事件拆成不依赖声卡的确定性测试；禁止放宽断言或伪造通过。
- 首次用独立临时目录、数据库和搜索页截图验收正式应用时，进程以 `0xC0000005`（`-1073741819`）退出，数据库和截图均未生成。Windows 事件日志给出的故障偏移为 `0x00000000000bc465`，符号定位到 `ui::TitleBar::setMaximizedState(bool)`。根因是 `MainWindow` 构造函数在创建 `m_titleBar` 前调用 `restoreGeometry()`；当保存的几何包含最大化状态时，Qt 会同步触发 `changeEvent()`，旧代码无空指针保护便调用尚不存在的标题栏。可行方案是保留窗口几何恢复，在 `changeEvent()` 中检查 `m_titleBar`，并在标题栏创建后主动同步一次最大化按钮状态；该缺陷属于既有启动时序问题，不是搜索聚合、数据库或阶段 4 算法引入。
- 提交前单独重跑 `tst_searchpage` 时，受限通道留下两个未产生报告的测试进程；改到桌面上下文后又在初始化前以 `0xC0000602` 从 `Qt6Core.dll` 快速失败。根因是测试命令误设 `QT_QPA_PLATFORM=offscreen`，而本项目 Windows 测试运行目录只部署 `qwindows.dll`，`tests/CMakeLists.txt` 也明确固定为 `QT_QPA_PLATFORM=windows`；这不是搜索页面断言或生产代码失败。可行方案是只清理路径已核实为本轮临时构建目录的残留测试进程，并按 CTest 属性在桌面用户上下文使用 Windows 平台插件重跑；Windows 下不得照搬其他平台的 `offscreen` 环境。
- 增加全局同曲歧义检测后重新构建时，核心库和新增测试源码编译成功，但链接 `bin/NeteaseClone.exe` 报 `Permission denied`。精确检查确认 PID 73044 正在运行同一临时构建目录的 EXE，命令行没有测试参数、父进程为 Explorer，因此链接器无法覆盖文件；这不是聚合代码或工具链错误。可行方案是仅对已核实路径的该实例发送不带强制参数的正常关闭请求，等待退出后再重新链接；不得按进程名批量结束播放器，也不得触碰并行 UI 工作区的构建或预览进程。

### 阶段 4 最终验证

- 提交前审查补充了全局同曲歧义检测：若某一版本在同一其他来源中存在多个可能候选，则相关版本全部保持独立，不再受来源回调先后影响或任选一个误聚合。新增正序/逆序回归后 `tst_searchservice` 为 17 通过、0 失败。
- 最终正式构建成功；QQ 包装层 8 项契约、`tst_searchpage` 3 项和桌面用户上下文的 `tst_multisourcesupport` 9 项均通过。完整 8 项 Qt 回归 100% 通过，本轮桌面音频上下文中 `tst_playerservice` 也通过。
- 使用空音乐目录、独立数据库和最终构建再次打开搜索页截图验收，应用自行退出且退出码为 0，数据库和截图均正常生成；构造期最大化状态恢复不再触发标题栏空指针。未读取或修改真实音乐库，也未修改、关闭或合并并行 UI 工作区。

### 阶段 5 编译/测试记录

- 阶段 5 综合搜索列表改用 `SongListView` 后首次重编译没有进入源码编译，PowerShell 报 `cmake` 不是可识别的命令。根因是当前会话的 `PATH` 未包含 Qt Maintenance Tool 已安装的 CMake，而既有 `build/CMakeCache.txt` 中的 Qt 6.11.1、MinGW 和 Ninja 配置仍然完整；这不是搜索源码或用户依赖缺失。可行方案是使用已确认存在的 `C:\Qt\Tools\CMake_64\bin\cmake.exe` 绝对路径驱动现有构建目录，或以后把该目录加入开发终端的 `PATH`，无需重新配置工程。
- 改用上述绝对路径后，CMake 自动重新生成 `build.ninja` 仍以退出码 1 结束，直接执行同一配置命令也没有产生 stdout/stderr；`cmake --version` 可正常返回 3.30.5，旧配置日志中的 Qt、MinGW、Ninja 和 vcpkg 探测均成功。当前证据只能确认失败发生在受限执行上下文的重新生成阶段，尚无源码或 CMake 语法诊断；申请在完整桌面上下文复跑时，审批通道自身中断并拒绝执行。可行方案是取得用户明确授权后在完整桌面开发上下文运行已确认的绝对路径命令，再依据真实生成/编译输出处理，禁止把无诊断退出伪报为源码失败或测试通过。
- 阶段 5 静态审查发现来源级重试按钮在 `m_onlineLoading` 清除前计算可见性，因此所有来源完成后失败来源仍没有重试入口；同时任一来源失败会隐藏健康来源的“加载更多”。根因是全局完成状态与来源独立状态的更新顺序错误。可行方案是先结束全局 loading 再刷新来源状态，并仅依据各来源 `hasMore` 选择分页目标；失败来源通过自己的重试入口处理，不连带禁用健康来源。
- QQ 包装层 8 项 Node 契约测试全部通过。搜索页面新增范围/分类路由、历史/热搜/联想键盘选择、缓存失败降级及健康来源保留测试；歌曲列表新增行级 `dataChanged`、无 `modelReset`、稳定选择和批量模式保持测试，待 Qt 构建恢复后执行。
- 用户明确授权完整桌面 CMake 构建后再次发起同一绝对路径配置命令，但自动审批服务在审查请求时再次断开连接并拒绝创建进程，CMake 实际没有启动，也未改动构建目录。根因是本轮 Codex 审批通道故障，不是工程、Qt 或本机 CMake 配置；安全约束禁止通过其他进程或间接命令绕过拒绝。可行方案是用户在本机开发终端直接运行记录中的绝对路径命令并提供输出，或等待审批通道恢复后由 Codex 原样重试。
- 用户在本机 PowerShell 重新生成成功后，受限环境构建在第一个 Qt AutoMoc 步骤失败：MinGW `g++ -dM -E` 生成 `moc_predefs.h` 时无输出返回 1，尚未编译任何项目源码。随后使用同一个 `g++.exe` 对 CMake 自带 `CMakeCXXCompilerABI.cpp` 做不含项目路径和宏的最小预处理仍返回 1，而 `g++ --version` 正常；这确认根因是受限执行上下文禁止或中断编译器预处理子进程，不是阶段 5 C++ 语法、Unicode 工程路径或缺少编译器。可行方案是在用户已授权的完整桌面环境执行同一构建命令；`WrapVulkanHeaders` 缺失仍只是未启用 Vulkan 时的可选 CMake 信息。
- 随后针对正式应用和三个阶段 5 测试目标申请完整桌面构建，自动审批服务仍在审查请求时断开连接，构建命令实际没有启动。该失败继续归属于 Codex 审批通道，而非工程编译；可行方案仍是用户在已成功配置的本机 PowerShell 直接执行完全相同的单线程构建命令并回传输出。
- 用户在本机执行真实构建后，AutoMoc 命令把仓库目录 `仿网易云播放器` 解码成 `浠跨綉鏄撲簯鎾斁鍣?`，随后 `moc_predefs.h` 生成失败。根因是当前 Ninja→`cmd.exe`→MinGW 链路把 UTF-8 路径字节按 GBK/系统代码页解释，传给编译器的源码、构建和 `-I` 路径已经不存在；配置成功不代表后续命令行编码正确，这也解释了预处理阶段无任何 C++ 诊断。可行方案是用未占用的 `subst` ASCII 盘符映射仓库，并从该盘符新建独立 `build-ascii`，让 CMake 记录的源目录、构建目录和所有编译参数都只含 ASCII；不移动仓库、不修改系统区域设置，也不复用已经记录乱码绝对路径的旧 `build`。
- 使用 `subst W:` 映射当前仓库后，从 `W:\build-ascii` 重新配置成功；Qt 6.11.1、MinGW 13、Ninja、vcpkg TagLib 均正常探测，正式应用、核心搜索缓存、搜索页及三个阶段 5 测试目标首次真实源码编译与链接全部通过。这验证了 ASCII 源路径与构建路径是当前机器上可复用的正式方案；`WrapVulkanHeaders` 缺失仍只是未启用 Vulkan 时的可选信息。
- 阶段 5 三项定向测试首轮为 2/3：`tst_searchservice` 与 `tst_songlistview` 通过，`tst_searchpage::providesHistoryDiscoverySuggestionsAndKeyboardSelection` 失败，回车解析返回后备文本而不是首个联想。根因是先前已提交搜索的异步本地回调在用户打开联想页后仍调用 `showCurrentResultPage()`，把页面从联想栈切回结果栈；列表选择本身没有丢失。修复为显式跟踪联想页可见状态，联想可见期间迟到结果只更新后台数据、不抢占当前页面；执行新搜索时再解除保护。增量重编译后 `tst_searchpage` 6 通过、0 失败。

### 阶段 5 最终验证

- `W:\build-ascii` 全部目标构建成功；完整 8 项 Qt 回归 100% 通过，包含播放器真实音频计时、DPAPI 多源、搜索排序/缓存、搜索页来源路由与缓存降级、歌曲列表行级更新和批量选择保持。
- QQ 包装层 8 项 Node 契约测试全部通过，分类搜索、热搜和联想契约保持稳定。使用空音乐目录、独立数据库和最终正式构建打开搜索页截图验收，应用自行退出且退出码为 0，数据库和截图均生成；未读取或修改真实音乐库，也未修改、关闭或合并并行 UI 工作区。

### 阶段 6 编译/测试记录

- 补入缓存损坏恢复、快速连续查询迟到响应、页面销毁后迟到回调、QQ 离线时网易云结果保留及 QQ 包装服务真实冷启动测试后，首次定向构建在 `tst_multisourcesupport.cpp` 编译阶段失败；随后单独构建 `tst_searchservice` 也在编译器进程无任何诊断的情况下返回 1，说明失败不局限于某个新增测试文件。使用同一完整桌面审批通道运行最小 MinGW 探针 `g++ -std=gnu++17 -dM -E -c CMakeCXXCompilerABI.cpp` 仍为退出码 1 且 stdout/stderr 全空。根因是当前 Codex 执行上下文再次阻止或中断 MinGW 预处理子进程，不是 C++ 编译诊断、QQ 依赖缺失或中文仓库路径问题（构建仍使用已验证的 `W:\build-ascii` ASCII 路径）。可行方案是在真正的 `danger-full-access` CLI/交互桌面开发终端继续运行相同 CMake 构建；不得通过修改源码、放宽测试或回退明文安全实现规避环境故障。
- 在普通完整权限 CLI 中核对 `W:` 未被其他目录占用后，将其映射到当前仓库并复用 `W:\build-ascii`；三个新增测试目标均完成真实 MinGW 编译和链接，未产生 C++ 诊断。该结果验证了上一条失败仅属于旧执行环境限制，新增断言和安全实现均无需删除、放宽或降级。

### 阶段 6 最终验证

- `W:\build-ascii` 全部目标增量构建成功；完整 8 项 Qt 回归 100% 通过（总计 36.51 秒）。`tst_searchservice` 验证损坏缓存被忽略且重新写入后恢复；`tst_searchpage` 验证查询 A 的迟到响应不能覆盖查询 B、页面销毁后迟到回调安全丢弃，以及网易云失败保留 QQ、QQ 失败保留网易云的双向来源隔离。
- `tst_multisourcesupport` 使用随机冷端口真实启动 QQ 包装服务、通过服务签名探活并调用正常停止流程，完整目标通过；QQ 包装层 Node 契约测试继续为 8 通过、0 失败。
- 使用空音乐目录和独立数据库启动最终正式应用搜索页截图验收，应用自行正常退出且退出码为 0，独立数据库与截图均成功生成；未读取真实音乐曲库，未强制结束应用，也未修改、关闭或合并并行 UI 工作区。

## UI 控件最终修复与合并验收（2026-08-31）

- 推荐歌单改为整卡封面视觉；歌曲行移除灰色悬停底层；上一首/下一首在窄窗口自动收紧外侧留白并保持 `30×30` 完整触发区域。
- 推荐页新增明确的“网易云 / QQ音乐”来源切换，选中项显示红色胶囊；账号区未登录显示“登录”，已登录显示“查看账号 · 昵称”。原有播放、来源启动、登录和账号 signal 均保持不变。
- 来源切换最终目视验收发现全局 QSS 在正式窗口中未可靠绘制选中背景，因此将四态样式直接绑定到两个来源按钮，并增加选中/闲置背景像素断言，避免再次出现“控件存在但肉眼不可见”的回归。
- Release 正式应用与 `tst_songlistview` 重新构建成功；完整 8 项 Qt CTest 全部通过（100%，0 失败，总计 37.72 秒）。
- 构建时必须在命令级 `PATH` 中包含 `C:\Qt\Tools\mingw1310_64\bin`，否则 `cc1plus` 会因找不到 MinGW 运行库而静默退出；这不是源码错误，无需修改系统级环境变量。

## UI 布局重构编译/测试记录（2026-09-01）

- 首次为 `codex/ui-layout-redesign` 配置 `W:\build-ascii` 时，受限执行进程中的 CMake 在生成 `build.ninja` 后以 `-1073740791` 无诊断退出，尚未进入当前 UI C++ 源码编译。根因与既有记录一致：受限进程会中断 CMake/MinGW 子进程，不是本轮列模型、搜索分页或图片资源的源码诊断。可行方案是在同一命令内用 `subst W: .` 建立 ASCII 映射、补齐 Qt MinGW/Ninja/CMake 的命令级 `PATH`，并在完整桌面开发上下文重跑相同配置和构建命令；不得因环境退出修改业务代码或放宽测试。
- 在完整桌面环境中，`neteclone_ui`、`tst_searchpage` 和 `tst_songlistview` 已完成真实编译链接；首次定向测试为搜索页 7/8、歌曲列表 17/22。搜索页唯一失败是发现列表的旧 `NoSelection` 断言，而新构造遗漏了显式恢复该既有属性。歌曲列表五项失败全部来自测试仍用旧列号（收藏 5、下载 6）和旧 `64px` 行高，实际实现已按确认计划迁移为收藏 6、下载 7、行高 `112px`。可行方案是恢复发现列表既有无选择视觉属性，并把测试命中点和尺寸断言迁移到八列契约，同时新增来源列首次展开/二次播放断言；这些失败不是收藏、下载信号丢失或下载状态机回归。
- 第二轮定向测试为搜索页 7/8、歌曲列表 21/23。三个失败均为新测试/旧几何断言本身：左右热搜列表位于不同父容器，直接比较各自局部 `x()` 都是 0；播放器收藏按钮已按计划移入独立 `playerActionBox`，旧断言仍要求它属于 `playerLeftBox`；未选中的 Logo 按钮背景按计划透明，测试误期望页面底色成为按钮自身像素。可行方案是把热搜位置统一映射到页面坐标、断言收藏按钮属于操作区，并以透明色校验未选中按钮；生产信号与来源二次点击测试均已通过。
- 新增自动分页和布局几何测试后，搜索页 9/9 通过，歌曲列表/布局 23/24；唯一失败为把标题栏从 1200px 缩到 700px 后搜索框仍为 518px，而计划和测试要求 430px。根因是搜索框先 `setFixedWidth(520)` 并加入带对称占位的布局，固定最小尺寸反向阻止父标题栏缩小，属于实现中发现的真实响应式问题。可行方案是让窗口按钮继续由布局固定在右侧，搜索框脱离布局按 `clamp` 和防重叠上限手工居中设置几何，从而不再抬高右栏最小宽度。
- 补齐来源资源、推荐歌单横向滚动、热搜重试和播放器紧凑模式测试后，定向 CTest 首轮为搜索页通过、歌曲列表失败；用正确的 QtTest 单参数日志格式 `-o <临时文件>,txt` 取得报告后，唯一失败是播放器从 1200px 缩到 700px 时歌词按钮仍可见。根因是宽屏布局把四区最小宽度动态抬高到 `280/76/250/240px`，这些下限反向阻止父控件缩到 `<780px`，导致紧凑模式无法进入。可行方案是四区始终保留真实可收缩下限，宽屏比例只交给 stretch 分配；修复后 `tst_songlistview` 26/26 通过。CTest 在该失败轮没有附带 QtTest 文本，直接执行时必须把 `<临时文件>,txt` 作为一个完整参数，不能在 PowerShell 中把逗号拆成两个实参。
- 尝试运行最终完整 CTest 时，审批系统以 `workspace out of credits` 拒绝请求，测试进程没有启动。根因是执行/审批额度状态，不是源码、Qt 或测试失败；可行方案是在额度恢复或用户继续授权后原样重试，不得把未启动的命令记为测试结果。本轮恢复后已在完整桌面上下文成功重跑。
- 早期隔离截图 smoke 在只补 Qt 路径、未补构建输出和 vcpkg 运行库路径时停在隐藏的 Windows Loader 错误框，GDB 栈顶表现为 `ZwRaiseHardError`。根因是正式 EXE 启动时找不到动态库，不是应用死锁或页面卡顿；可行方案是在 smoke 命令级 `PATH` 中同时加入 `W:\build-ascii\bin`、Qt 6.11.1 MinGW、vcpkg `x64-mingw-dynamic\bin` 和 MinGW 运行时目录。
- 最初尝试通过临时 `APPDATA` 隔离推荐缓存时，Windows 下 `QStandardPaths` 仍解析到真实用户应用数据目录，无法满足“不读取真实设置”的截图边界。根因是 Qt/Windows 标准路径解析不保证接受运行期临时环境变量覆盖；可行方案是仅在显式 `--db` 测试启动中提供 `--settings-dir`，把 QSettings、推荐缓存和搜索缓存全部定向到隔离目录，正常应用默认路径保持不变。
- 首套 940×600 推荐页截图出现推荐卡片与“每日推荐歌曲”重叠。根因是低高度下推荐上半区仍按完整高度参与同一纵向布局，播放器和歌曲区压缩后没有独立滚动边界；可行方案是让推荐上半区在低高度时独立纵向滚动，同时保留歌单横向滚动并至少给每日歌曲留出一行。修复后的最小窗口截图已确认无重叠，播放器进入紧凑模式。
- 提交前审查发现标题栏移入右侧容器后，非客户区命中测试已把标题栏整体映射到窗口坐标，但按钮和搜索框仍用旧的直接父级坐标偏移，可能把窗口按钮误判为拖动区。根因是嵌套布局迁移时坐标系只修正了一半；可行方案是统一以标题栏映射到主窗口后的左上角平移按钮和搜索框矩形。正式应用重新编译链接成功，随后完整回归再次通过。

### UI 布局重构最终验证

- `W:\build-ascii` 最终源码构建成功；标题栏坐标修复后的完整 8 项 Qt CTest 在完整桌面用户上下文 100% 通过，0 失败，总计 49.96 秒。覆盖播放器真实音频计时、DPAPI 多源、搜索缓存/页面、来源选择和响应式歌曲列表布局。
- QQ 包装层 Node 契约测试 8/8 通过，QQ/微信扫码状态、字符串远端 ID、推荐/歌单/搜索/热搜/联想归一化契约均未回归。
- 最终二进制使用空音乐目录、独立 SQLite、独立设置目录和固定推荐夹具生成六张隔离截图：1920×1080、1280×800、940×600 推荐页，1280×800 搜索发现页，以及 125%/150% DPI。目视确认标题栏无旧 Logo/名称、侧栏和底部播放器边界正确、歌曲保持整行 112px、最小窗口无重叠、搜索双热搜等宽无灰底、高 DPI 图标无裁切；未读取或修改真实曲库。
- 本轮只在 `codex/ui-layout-redesign` 提交并推送，不合并 `main`，不提交 `db-backups/`、`ui-repro-data/`、`ui-repro.sqlite*` 或隔离构建/截图产物，也未操作并行 `仿网易云播放器-ui-controls` worktree。

## UI 布局重构合并与本地发布（2026-09-01）

- 用户在分支验收后明确授权合并和更新正式快捷方式版本；`main` 已从 `d1c5941` 快进到已验收提交 `5f7ff31` 并推送到 `origin/main`，没有产生合并冲突，也未改动并行 `仿网易云播放器-ui-controls` worktree。
- 使用仓库现有 `scripts/package.ps1` 从 `build-ascii` 生成全新的 `dist/NeteaseClone.new`。`windeployqt` 只提示未找到可选的 `dxcompiler.dll/dxil.dll`；当前应用不使用 Direct3D 12 对应功能，该提示不影响 Qt Widgets、FFmpeg 多媒体或现有播放器功能，无需伪造或额外复制未知版本 DLL。
- 新发布目录包含 38 个文件、共 90,243,405 字节；发布 EXE 与已通过完整回归的构建 EXE SHA-256 均为 `8D99A9B6B4F8E7461DBE0A008BDAD887D8185FE38E09C6CB58A834281E7A34B0`。使用空音乐目录、独立 SQLite 和独立设置目录启动发布包，应用自行退出且退出码为 0。
- 验证通过后将新目录切换为 `dist/NeteaseClone`，旧发布版本保留在 `dist/NeteaseClone.previous-20260901-135821`。桌面 `仿网易云播放器.lnk` 未被重写，时间戳仍为 2026-08-31 16:12:03，内嵌目标与工作目录均继续指向 `dist/NeteaseClone`，因此现在会启动新的 UI 布局版本。
