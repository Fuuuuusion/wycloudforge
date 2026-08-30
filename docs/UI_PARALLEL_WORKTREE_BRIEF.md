# 并行 UI 控件优化任务说明

## 交给另一个 AI 的直接指令

你是 WyCloudForge 项目的并行 UI 优化代理。你的任务是优化现有 Qt 控件的外观、布局和交互可达性，同时不得干扰另一个正在实施搜索引擎优化的任务。

开始工作前必须完整阅读本文件、docs/HANDOFF.md 和 design/tokens.json。只在本文件指定的独立 worktree 和分支工作。发现边界不清楚时先停下询问用户，不得自行扩大范围。

## 工作位置

- UI 独立 worktree：
  C:\Users\Fusssssion\Documents\ChatGPT\仿网易云播放器-ui-controls
- UI 独立分支：
  codex/ui-controls-optimization
- 搜索任务主工作区：
  C:\Users\Fusssssion\Documents\ChatGPT\仿网易云播放器
- 搜索任务分支：
  main

每次开始工作先执行并确认：

    git branch --show-current
    git status --short --branch

当前分支必须是 codex/ui-controls-optimization。若不是，立即停止，不得修改文件。

## 任务目标

只优化以下方面：

- 控件尺寸、间距、内边距、圆角、字体、颜色和图标。
- 悬停、按下、选中、禁用、加载、错误和空状态的视觉反馈。
- 控件位置、页面布局、对齐、留白及窄窗口适配。
- 按钮和操作入口的可发现性。
- 文案、工具提示和状态说明的清晰度。
- 横向溢出、滚动区域和内容截断问题。
- 不改变业务语义前提下的交互步骤简化。

## 严格禁止

- 禁止修改 core 目录中的任何文件。
- 禁止修改数据库结构、SQL、LibraryService、PlaylistController 或 SettingsService。
- 禁止修改网易云或 QQ API、登录、凭据、扫码、播放地址、歌词、下载和缓存逻辑。
- 禁止修改播放队列、播放模式、批量事务、稳定歌曲身份或来源路由。
- 禁止修改任何 signal 的参数、含义或触发条件。
- 禁止根据列表行号代替稳定歌曲身份。
- 禁止新增同步网络请求、同步数据库查询或 UI 线程中的重计算。
- 禁止为了刷新单个封面或状态而重建整个页面。
- 禁止重新加入动态背景、页面切换动画、循环或持续动画、毛玻璃、模糊、渐变、透明高光或阴影；仅允许下文“经用户批准的控件微动效例外”所定义的短时局部动效。
- 禁止大范围格式化文件。
- 禁止合并到 main，禁止向 origin/main 推送。
- 禁止执行 git reset --hard、git checkout -- 或其他会覆盖用户工作的命令。

## 当前搜索任务专属文件

以下文件由另一个搜索任务独占，UI 代理不得修改，即使修改看起来只是样式：

- core/SearchService.h
- core/SearchService.cpp
- core/MusicSource.h
- core/MusicSource.cpp
- core/MusicSourceRegistry.h
- core/MusicSourceRegistry.cpp
- core/NeteaseApiClient.h
- core/NeteaseApiClient.cpp
- core/QqMusicSource.h
- core/QqMusicSource.cpp
- ui/SearchPage.h
- ui/SearchPage.cpp
- ui/TitleBar.h
- ui/TitleBar.cpp
- app/MainWindow.h
- app/MainWindow.cpp
- core/CMakeLists.txt
- ui/CMakeLists.txt
- app/CMakeLists.txt
- tests/CMakeLists.txt
- 搜索任务新增的 SearchTypes、SearchCoordinator、搜索测试或搜索缓存文件

如果希望调整搜索页或标题栏搜索框，只能先在 design/prototype 中制作提案，不能修改对应 Qt 文件。待搜索任务完成后由主任务统一移植。

## UI 代理拥有的区域

可以自由修改但仍须遵守业务边界：

- design/prototype/
- resources/theme.qss
- resources/icons/
- resources/resources.qrc（仅用于登记 UI 图标资源）
- ui/ProgressSlider.*
- ui/CoverCard.*
- ui/SideBar.*
- ui/AccountPanel.*
- ui/LibraryPage.*
- ui/FavoritesPage.*
- ui/SelfPlaylistsPage.*
- ui/SongListPage.*
- ui/DownloadPage.*
- ui/PlayingPage.*（仅布局与展示）
- ui/LyricWidget.*（仅布局、字体和展示）
- ui/RecommendPage.*（仅布局与展示）
- ui/OnlinePage.*（仅布局与展示）
- ui/SettingsDialog.*（仅布局与展示）
- ui/PlaylistEditDialog.*（仅布局与展示）
- ui/LyricEditorDialog.*（仅布局与展示）
- ui/CommentsDialog.*（仅布局与展示；不得重新添加已移除的社交入口）

## 共享控件的特殊边界

### PlayerBar

允许：

- 调整按钮位置、尺寸、间距、图标和提示。
- 调整歌曲信息、封面、进度和音量区域布局。
- 改善窄窗口显示和文本截断。

禁止：

- 修改播放、暂停、上下首、播放模式、音量和跳转信号的语义。
- 修改歌曲队列、播放状态机或来源选择。
- 修改永久下载、缓存或在线地址优先级。

### SongListView 和 SongListModel

这两个文件包含批量选择和稳定歌曲身份逻辑，属于高风险共享区域。默认不得修改。

若用户明确要求优化歌曲列表控件，允许修改前必须满足：

1. 只改行高、列宽、按钮几何、颜色、字体、图标、工具提示和批量栏布局。
2. 不改 SongListModel 的数据来源、角色、行映射和歌曲身份。
3. 不改批量选择集合、稳定身份、事务信号和操作结果。
4. 不改 playRequested、heartRequested、addToPlaylistRequested、downloadRequested、deleteRequested 等信号语义。
5. 单独提交，并运行 tst_songlistview 与 tst_playlistcontroller。

如果无法保证上述条件，先只在 design/prototype 中制作方案。

### AccountDialog、QqLoginDialog 和 LoginDialog

账号页普通布局可以优化，但扫码对话框包含敏感异步状态机：

- AccountDialog 和 AccountPanel 只允许调整展示。
- QqLoginDialog 和 LoginDialog 默认不得修改。
- 二维码必须在扫码后、手机确认前继续显示。
- 不得更改请求代次、取消、凭据验证或窗口关闭逻辑。

### RecommendPage 和 OnlinePage

允许调整卡片、标签、来源切换控件及空状态的布局和样式。

不得修改：

- 来源激活和服务启动流程。
- 推荐、歌单、封面和详情请求。
- generation 校验、缓存加载和失败回退。

### PlayingPage 和 LyricWidget

允许调整歌词区域、字号、行距、控制区位置和当前歌词高亮。

不得修改：

- 歌词来源优先级。
- LRC 解析、时间同步和点击跳转语义。
- 在线歌词请求代次和旧回调保护。

## 视觉硬约束

必须延续已经确认的固定主题：

- 主强调色：#EC4141。
- 悬停强调色：#F04A4A。
- 正文：#E8E8E8。
- 次级文字：#9A9AA5。
- 弱化文字：#6E6E7A。
- 页面背景：#12121A 或现有相邻深色。
- 使用 Microsoft YaHei UI。
- 不使用任何 1px 分隔线。
- 不使用渐变、毛玻璃、背景模糊、动态背景、页面切换动画、循环或持续动画、透明高光或阴影；仅允许下文定义的短时控件微动效。
- 底部播放器保持固定位置和固定深色背景。
- 所有横向滚动条继续隐藏。
- 图标使用项目自绘 SVG 体系。
- 具体尺寸优先遵守 design/tokens.json，除非用户明确批准调整。

### 经用户批准的控件微动效例外（2026-08-30）

用户明确批准仅在以下边界内添加控件微动效；本例外在 UI 分支中优先于 docs/HANDOFF.md 里“移除所有视觉动效”的旧表述：

- 只允许悬停、按下等控件内部状态的短时、非循环微动效，单次时长不超过 160ms。
- 动效只能改变控件内部的绘制属性或局部几何，例如颜色、调节圆点位置；不得改变控件占位、页面布局或命中区域。
- 只能局部重绘当前控件，不得重建页面、列表或数据模型。
- 不得改变 signal 参数、含义、触发条件、业务状态、设置值、网络请求、数据库访问、播放或下载逻辑。
- 用户明确批准 PlayerBar 播放/暂停控件使用 Uiverse `wet-rabbit-81` 的原版 500ms 旋转、缩放、回弹与透明度动画；该例外只允许由现有 `setPlaying(bool)` 的真实播放状态变化触发，不得由按钮点击在本地预先切换状态，也不适用于其他控件。
- 不得使用动态背景、页面切换动画、自动播放动画、循环或持续动画、渐变、毛玻璃、模糊、透明高光或阴影。
- 每次新增一种超出上述范围的动效，或需要突破其他既有约束，必须先停止并向用户说明影响，取得明确批准后才能实施。

### 新控件变更确认门禁（2026-08-30）

用户提供新的控件、网页参考或视觉方案后，必须先完成只读评估，不得立即修改原型、QSS、资源或 Qt 代码。评估至少包含：

- 技术可行性，以及是否会改变 signal、业务语义、数据状态或线程模型。
- 预期视觉与交互效果，包括闲置、悬停、按下、焦点、禁用及窄窗口状态。
- 为适配现有 Qt Widgets、固定深色主题、尺寸令牌和自绘 SVG 体系所需的改动。
- 预计修改的文件、文件所有权、改动规模、性能与可访问性影响、构建和测试范围。
- 与现有视觉约束或业务边界的冲突、后果及可行替代方案。

只有在用户对该控件的评估结果和实施范围作出明确确认后，才能开始修改。实施中若出现新的范围、效果或约束突破，必须再次停止并取得用户确认；此前的确认不得视为对后续其他控件的授权。

## 推荐工作顺序

1. 先列出准备优化的控件及对应文件。
2. 检查这些文件是否属于搜索任务专属或高风险共享区域。
3. 对明显的布局方向先修改 design/prototype，向用户展示。
4. 用户确认方向后再同步到 Qt。
5. 每次只处理一个控件或一组紧密相关控件。
6. 修改后检查窄窗口、正常窗口、禁用、空状态和长文本。
7. 运行与修改范围相关的测试和正式构建。
8. 每个控件完成后独立 commit，并 push 到 origin/codex/ui-controls-optimization。

## Git 纪律

- 只提交本 UI worktree 中属于自己的改动。
- 每次提交前检查 git diff 和 git status。
- 提交信息使用 ui:、style: 或 fix(ui): 前缀。
- 每次提交后执行：

    git push origin codex/ui-controls-optimization

- 不得直接推送 main。
- 不得擅自合并 main。
- 需要同步主分支时，先通知用户或当前搜索任务，由主任务选择安全的同步点。
- 发现其他任务的改动出现在当前 worktree 时立即停止。

## 编译和测试纪律

- 仅修改原型或文档时不要求编译，但必须检查文件完整性。
- 修改 QSS、资源或 Qt 代码后必须构建正式应用。
- 修改某个控件时运行对应测试；涉及共享列表时至少运行 tst_songlistview 和 tst_playlistcontroller。
- 最终合并前运行全部 Qt 自动化测试。
- 每次编译或测试失败都追加记录到 docs/HANDOFF.md，包含错误、根因和已验证的解决方案。
- 环境配置缺失时先询问用户，不得用删除功能或降低安全性规避测试。

## 提交前自检

每次提交必须确认：

- 没有修改 core、MainWindow、SearchPage、TitleBar 和搜索相关文件。
- 没有改变 signal 参数或业务行为。
- 没有引入未经用户批准或超出“控件微动效例外”边界的动画，也没有引入玻璃、渐变、阴影或横向滚动条。
- 没有使用行号代替稳定歌曲身份。
- 没有全局重建页面解决局部刷新问题。
- 没有夹带其他任务或用户的改动。
- 当前分支仍是 codex/ui-controls-optimization。

## 与搜索任务的交接

UI 分支只负责产生独立、可审查的小提交。搜索任务完成一个稳定阶段后，由主任务检查 UI 分支提交并决定何时合并。

如果 UI 需求必须修改 SearchPage、TitleBar、MainWindow、SongListView 的业务部分或任何 core 文件，UI 代理必须：

1. 停止修改。
2. 写出需要修改的文件、目标和理由。
3. 把建议交给用户和当前搜索任务。
4. 等待主任务统一实施或明确解除文件边界。

不得为了完成视觉目标自行突破边界。
