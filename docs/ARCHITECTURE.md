# 工程结构与维护边界

本文描述当前 `main` 的真实代码结构。它用于判断新功能应该放在哪里，也明确哪些大文件只是职责过重、不能直接当作“冗余”删除。

## 目录职责

| 目录 | 当前职责 | 允许依赖 |
|---|---|---|
| `app/` | 进程入口、单实例、命令行参数、主窗口组装和跨页面接线 | `core/`、`ui/` |
| `core/` | 歌曲模型、SQLite、播放/下载状态机、搜索聚合、平台来源、凭据和设置 | Qt Core/Sql/Network/Multimedia，不依赖 `ui/` |
| `ui/` | Qt Widgets 页面、对话框、列表模型/视图、自绘控件和主题 | `core/` |
| `resources/` | QSS、运行时图标和来源 Logo；所有生产资源由 `resources.qrc` 收口 | 不含业务代码 |
| `本地部署/` | 网易云服务与 QQ 包装服务；进程和端口彼此隔离 | Node.js 包装契约 |
| `tests/` | QtTest 回归与固定夹具 | 按目标链接 `neteclone_core` 或 `neteclone_ui` |
| `scripts/` | 正式目录和完整便携 ZIP 的发布脚本 | 已构建的 Release 目标 |
| `design/` | 当前语义颜色和布局参数的文档化令牌 | 不参与运行时加载 |
| `docs/` | 现行交接、架构说明和已批准方案记录 | 不参与编译 |

## 运行时主链路

```text
main.cpp
  -> ThemeManager 初始化主题和全局 QSS
  -> MainWindow 组装页面与服务
       -> LibraryService / PlaylistController 管理本地持久化
       -> MusicSourceRegistry 路由网易云、QQ 和本地来源
       -> SearchService + SearchAggregator 处理并发搜索和同曲合并
       -> PlayerService / DownloadService 管理播放队列和下载任务
       -> ui 页面只消费统一 Song、来源状态和服务信号
```

平台原始 JSON 必须先在 `NeteaseApiClient` 或 `QqMusicSource` 中转换为统一对象，不能直接泄漏到 UI。歌曲身份必须使用来源与字符串远端 ID，不能退回列表行号或单个平台整数 ID。

## 主题链路

- `ThemeManager` 是运行时颜色事实来源，支持跟随系统、深色和浅色。
- `resources/theme.qss` 使用 `@semanticToken`，加载时由 `ThemeManager` 渲染。
- 动态内联样式必须通过 `setThemedStyleSheet()` 注册，禁止直接写死主题颜色。
- 自绘控件使用 `themeColor(ThemeColor::...)`。
- 单色 SVG/PNG 使用 `SvgIcon.h` 的动态图标引擎，保证切换主题后现有按钮同步换色。
- `design/tokens.json` 是供设计和评审使用的镜像；修改颜色时必须同步它和 `ThemeManager.cpp`。

## 测试分类

| 目标 | 主要范围 |
|---|---|
| `tst_lrcparser` | LRC 解析 |
| `tst_tagreader` | 音频标签、封面和内嵌歌词 |
| `tst_librarypersistence` | SQLite、收藏/歌单、缓存/下载关系与恢复 |
| `tst_playerservice` | 播放状态、队列、联播和来源回退 |
| `tst_multisourcesupport` | 多来源、凭据和本地包装服务契约 |
| `tst_searchservice` | 搜索、缓存、排序和同曲聚合 |
| `tst_searchpage` | 搜索页面异步状态和交互 |
| `tst_uiwidgets` | 共享歌曲列表、播放器、推荐、下载和布局控件 |
| `tst_thememanager` | 三种主题、持久化、QSS 令牌和动态图标 |

## 已确认并移除的冗余

- 无入口且已被现行页面替代的 `DiscoverPage`、`OnlinePage`。
- 无调用的 `CommentsDialog`。
- `MusicSource` 中没有消费者的榜单、私人 FM、评论和点赞旧接口，以及两个平台的空闲实现。
- 旧 `playlistCoverCachePath(qint64)` 兼容入口。
- 未引用的 Logo、检查图标和旧下载状态资源。
- 生产 QRC 对旧浏览器原型资源的依赖。
- 与当前无动效、无模糊界面相冲突的旧 HTML/CSS/JS 极光原型。

## 仍然偏大的热点

以下文件不是死代码，本轮不做一次性拆分：

- `core/LibraryService.cpp`：SQLite 迁移、扫描、缓存、封面和下载修复集中在一个服务。
- `app/MainWindow.cpp`：窗口组装、页面路由和跨服务信号接线集中。
- `ui/SearchPage.cpp`：发现页、热搜、联想、分页和结果展示集中。
- `ui/SongListView.cpp`：委托绘制、批量操作、来源选择和上下文菜单集中。
- `ui/PlayerBar.cpp`：布局、自绘按钮和播放状态展示集中。

后续拆分顺序应是先补行为测试，再按职责提取小对象：数据库迁移/文件关联、页面路由、搜索发现模型、列表委托/批量控制器、播放器子控件。不要为了降低行数同时改动数据库、播放状态机和 UI 接线。

## 非源码与本地数据

- `dist/`、`build/`、`build-ascii/` 是生成物，不属于源代码架构。
- `db-backups/`、`ui-repro-data/`、`ui-repro.sqlite*` 是本地诊断数据，不提交、不清理。
- `docs/*_PLAN.md` 是历史决策记录，即使其中保留旧测试名或旧截图说明，也不作为当前运行时事实来源。
- 并行 UI worktree 有独立分支和所有权；主工作树不得代替它修改或合并未交付内容。
