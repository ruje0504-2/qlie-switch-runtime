# WHJ2-Switch

《美少女万華鏡2》日文原版的原生QLIE运行时移植，目标为Nintendo Switch/Horizon NRO，使用Mac作为开发验证平台。**当前是开发候选，尚非完整可玩版本，未通过Switch实机验收。**

仓库只包含技术源码、构建脚本和许可。原版游戏资源、EXE、密钥及存档不属于本仓库；游戏数据只读，移植存档单独保存。

## 构建和运行

Mac需要C/C++17编译器、Python3、SDL2、FreeType、libpng、libjpeg与FFmpeg开发库。默认使用Homebrew pkg-config，可用PKG_CONFIG覆盖。

```sh
./build-host.sh
build/whj2-runtime /absolute/path/to/game /absolute/path/to/independent-saves
```

Mac应用入口可在构建后生成，复用同一运行时文件：

```sh
python3 tools/package_mac_app.py --game /absolute/path/to/game --saves /absolute/path/to/independent-saves
```

输出`build/WHJ2-Mac.app`。目录读取被系统拒绝时，使用标准目录选择框授权游戏目录。

Switch需要devkitPro、devkitA64、libnx和Switch portlibs：

```sh
./build-switch.sh
```

产物为`build-switch/whj2-runtime.nro`。默认从`sdmc:/switch/whj2/game`读取原版文件，在`sdmc:/switch/whj2/saves`保存状态。部署目录只需原版GameData、DLL/key.fkey和独立DLL/icon.key。Switch运行时不读取或执行EXE，也不需要Windows DLL。可从自己的本地原版副本准备icon.key：

```sh
python3 tools/extract_icon_key.py /absolute/path/to/original.exe /absolute/path/to/deployment/game/DLL/icon.key
```

icon.key必须为256字节；密钥和游戏资源不得提交到源码仓库。Switch启动日志写入sdmc:/switch/whj2/runtime.log。Mac也优先使用独立icon.key，缺少时兼容原本地日文EXE提取。

构建输出集中到固定build/build-switch目录，后续更新覆盖产物。同版本已有有效结果可以复用。

Mac和Switch运行循环统一限制为最高60 FPS，保留垂直同步；输入处理和动画计时使用实际经过时间。
正文语音加入既有后台资源预读队列，播放时复用准备好的PCM。快进默认停止语音并跳过其读取、解码，保留资源配置ForceVoicePlay的例外；历史中的手动重播独立处理。点击停止语音使用原版250ms淡出。

## 输入

目标输入为Joy-Con与触摸。L打开存档界面，R打开读档界面，A点击/确认，B取消；正文中B隐藏/显示文字框。X打开菜单页面，十字键上打开历史文字，ZL切换自动播放，ZR按设置切换快进，再按一次关闭；仅已读模式遇未读停止，全部模式包含未读，两种模式均在选项停止。Y或屏幕下滑调出原版顶部栏，再按Y/B收起。存读档界面中方向键选择槽位，L/R翻页。游戏画面不显示自制按键说明或常驻触摸按钮。−键切换左下角FPS显示，默认隐藏；采用biko3参考的浅蓝黑影像素字，显示实际每秒提交帧率，60FPS上限始终独立生效。
标题花海的左右视角跟随触摸按下与拖动；左摇杆左右调整视角，松开后保留角度。十字键独立控制菜单选项。
这些入口已接线，但仍缺实际Joy-Con和Switch触摸验收。SDL注入事件仅验证输入处理和状态流程。

## 实现范围

- QLIE资源解密/BPE、日文显示、原配置禁则换行与段首/折行缩进、剧情控制流与四结局逻辑、段落提交、空存档正常处理、原版读档/覆盖/删除确认及最近手动槽定位；有数据的槽显示原删除按钮，手柄可选、触摸命中按原30px范围。
- 全局图元排序、包含零图元控制层的变换栈、原版定点混合、矩形、部分周期/随机轨道（含顺序随机透明度及游标存读档）、上下文尺寸、位图与嵌套动画粒子。渲染状态支持压栈、退一层和清空全栈。
- 原标题与选择框资源、动画和碰撞矩形；选项确认保留两段250ms退出过程，支持点击加速，退出中的选择状态可存读；天空模型的纹理三角形、单轴嵌套三维变换、透视UV与近裁剪。无光照材质使用原始颜色；多轴精度和其他光照模式仍待完成。
- 双纹理位图遮罩、蓝通道阈值。GPU直接采样原图与遮罩；CPU回退的动态遮罩和影片帧保有队列内快照，静态图片继续共享。
- 角色基础动作与独立立绘替换槽、命名/数字动作、直接后向关键帧循环。显式与自动头像选择、逐页/持续显隐、前景层。
- 图层位置/颜色渐变、相对位移、播放速度。原消息框显示/隐藏动画、逐字正文（换行不增加逐字或自动等待）、姓名别名，以及spd速度、pc居中、换行与原OF外字标签。正文和历史按原替换表使用图集字形。静态位图支持原blur10/blur20与sepia滤镜。
- 音乐/音效、顶层动画内嵌音频及独立direct总线、音量和播放位置保存。剧情音乐切换用独立的当前/上一曲声道按脚本时间交叉淡化，两轨游标和淡化进度均进入音频存档。独立Theora影片按音频采样时钟播放。
- 设置页高/低画质开关沿用原版交替扫描行语义；低画质保留另一半上一帧画面，Switch使用GPU缓冲复制，高画质为默认。设置页包含原位置的五类音量测试按钮，Joy-Con选择/A播放及触摸均接入同一个原版测试音和当前分类音量。独立系统通关进度与原版额外菜单。音乐鉴赏已接入18曲原资源、PCM游标计时/定位、曲目切换/循环、专用音量和控制器/触摸操作；原选项及音乐页操作音已接线，音乐页首次取消按250ms淡出；图库与回想的已接入范围见下文，完整视听行为仍待验收。
- 场景交叠/cut/catch/原规则图渐变、点击/固定/正文等待和原生等待续行。图像、音轨、过渡、逐字及等待状态可以保存和恢复。

CG目录支持原33组784个差分资源，按已显示资源解锁整组；支持原按钮分页、图片/动画差分、放大和平移，以及CG/回想/音乐页面互跳。显示与已读记录保存在进度v3，旧进度v1/v2可读取，缺失的已读记录保持未读。动画CG按原关键帧时间循环；34个影片资源中24个音轨解码后为全零PCM，10个无音轨。完整原UI转场仍待接线。
剧情图像的rep替换按原脚本顺序应用，支持遮罩/粒子位图引用、实例隔离和存读档恢复；更换基础文件会清除旧替换。
回想播放时，X打开的菜单提供独立返回回想目录操作；取消菜单继续播放，返回标题保持原义。
ZL切换自动播放，ZR与原位置的触摸跳过按钮共用设置中的仅已读/含未读模式，并在选项处停止。已读页面独立持久化，flow2保存当前页标识并兼容flow1。

回想目录使用原版两页缩略图和按钮，按实际经过的剧情标签解锁；回放保留原选项并在结束后返回目录，回放期间禁用存读档。当前仅完成目录和播放/返回接线，尚未完成所有回想段落的视听验收。

选项出现后按原规则轮换五份自动备份，保存在移植存档目录的`autosave/`子目录。快速跳过和回想不写自动备份。可通过启动参数`GAME SAVE_DIR --restore-autosave N`恢复第N份（1–5）；普通120个手动槽保持独立。

保存格式仍处于开发阶段。旧版本状态的兼容分支不能替代完整演出恢复验收。

## 已知缺口

部分原版UI、部分富文本和禁则排版、精确字体与文本计时对齐、其余缓动/图元/随机/上下文、3D多轴旋转与材质光照、非位图遮罩、影片粒子、条件关键帧跳转、嵌套音轨与完整对象生命周期、马赛克/动画转场、自动播放的特殊文本计时和语音seek仍未全部完成。正文3277个语音ID已与data1.pack中的资源匹配，包含自带.ogg扩展名的标记；样本已解码为PCM。正文播放、全局语音音量、自动等待和音轨位置存读档已接线，尚未完成Switch实机语音验收。历史记录保存语音ID，支持原版按钮触摸重播和上下键逐行选择并在边缘滚动，A键重播选中条目的语音；history-v2兼容读取不含语音ID的history-v1。

未实现路径通过PENDING日志记录，不作为完整游戏行为通过。Mac验证与Switch构建都不能替代本游戏的Switch实机验收。

## 验证入口

```sh
build/story-probe /absolute/path/to/game 000100
build/presentation-probe /absolute/path/to/game --tween-render
build/presentation-probe /absolute/path/to/game --chara-base
build/presentation-probe /absolute/path/to/game --face-render
build/presentation-probe /absolute/path/to/game --dynamic-instances
build/embedded-audio-probe /absolute/path/to/game
build/cg-audio-probe /absolute/path/to/game
build/cg-loop-probe /absolute/path/to/game
build/transition-probe /absolute/path/to/game
build/wait-probe /absolute/path/to/game
build/message-probe /absolute/path/to/game
build/whj2-runtime /absolute/path/to/game /absolute/path/to/empty-test-saves --touch-smoke 120
```

资源、逻辑和像素探针仅证明各自覆盖的路径。实际硬件、完整剧情和交互体验需要独立验证。

## 来源和许可

参考kisaku-switch-runtime固定提交`95d71d9bcaee9dc7ab48b4bda8d3139a817e6cc7`。本作QLIE与参考项目AI6WIN不同，没有复用其VM；适用字体、媒体和构建基础的来源见THIRD_PARTY.md。

源码遵循GPL-2.0-or-later。原版游戏数据不在源码许可范围内。

### Controller and touch navigation

L opens the save panel; R opens the load panel. A confirms a selected slot, and B cancels a panel. X opens the settings page, Up opens text history, ZL toggles auto-play, and ZR toggles skipping according to the configured read/unread mode. Both modes stop at choices; read-only mode also stops at unread pages. During dialogue B hides or restores the text box; A first restores hidden text without advancing. Y or a downward swipe opens the original upper menu. No controller legend or permanent touch overlay is drawn.

Save/load panels use the original resources, 15 pages of eight local slots, and small scene thumbnails. History contains displayed pages and is included with each slot. Up/down selects a record, left/right scrolls long text within that record, and A replays its voice. Touch supports row selection, text dragging and the original scrollbar. The settings page supports nine sliders for text speed, text-window opacity, auto-play delay, and six volume controls, plus click-to-stop voice and auto-play voice-wait toggles. Sliders support captured touch drags and controller adjustment, with local persistence and reset confirmation. The original auto-play button controls timed progression and waits for text completion and, when selected, voice completion. Font selection includes collection faces, antialiasing, bold, shadow, outline and color controls. The display-quality setting selects the original alternating-scanline mode. Dialogue voice volume and playback are connected; current Switch behavior and exact original-font parity remain unverified. Confirmation dialogs currently use original artwork without the original transition timing. Physical controller/touch and Switch runtime acceptance remain pending.


## Vulkan renderer

Switch scenes use the GPU through the statically linked Vulkan/NVK
backend from [danfromtico/mesa-switch](https://github.com/danfromtico/mesa-switch),
Mesa 26.2.4, commit `0ff52079ca9e9e837ed27eb95a8d239f439a1864`.
The authored flower particles, sky mesh, texture sampling, ordered integer
blending and left-stick/touch viewpoint remain in the same scene graph.
The compositor compares draw order, parameters and texture versions in 16×16
tiles. Unchanged tiles keep their pixels. A changed tile can start from a
previously saved prefix of its layers. On Horizon devices that expose ordered
pixel interlock, one instanced draw covers the dirty portions of sprites,
particles, projected model triangles, text, masks and fades. The rasterizer
interpolates the source coordinates. A shared shader implements the QLIE
sampling and integer blend modes in draw order. When ordered geometry is
unavailable, one compute dispatch processes the dirty tiles.

The native composition target resides in GPU memory. At 1280×720 the renderer
can copy it directly to a BGRA swapchain image with transfer-destination
support. Other surface configurations use the fullscreen presentation shader.
The title, story, menus and overlays share the native NWindow. SDL handles
audio and events. The CPU movie decoder publishes each new video frame to the
texture cache. Immutable decoded images carry content versions and other
sources use byte comparisons. The GPU fence protects previously uploaded data.

The script engine requests a transition snapshot only for a command which
uses the previous scene. A capture draws the scene and any active transition
and reads their pixels back once. Save thumbnails keep their existing CPU
path. During the opening and at story waits a worker prepares upcoming images,
composites and audio in script order. The queue holds up to six hints and
evicts completed speculative results above a 48 MiB budget. Particle identities
and audio playback start at the original script command. Slow page changes log
the time spent capturing and whether their resources were ready.

On Horizon, the main loop, the Vulkan submit/present thread and the audio mixer
request the first, second and third CPU cores allowed for the process. CPU
raster workers use the remaining allowed slots when rendering CPU snapshots.
The asset worker shares the second allowed core with the presenter.
Each role logs its requested core and the affinity mask read back from Horizon.
The presenter holds one frame in flight. Input and scene preparation can overlap
that frame. When every tile is unchanged and the FPS counter is hidden, the
screen retains the last presentation and the input loop runs at up to 60 Hz.
With the counter visible the application continues presenting through FIFO so
the counter measures completed presentations. Command buffers and mapped memory wait for its fence before
reuse. Periodic logs identify dirty tiles, uploads, CPU preparation, fence wait
and separate GPU raster and presentation times on queues supporting timestamps.

Native FIFO presentation supplies the console's 60 Hz limit, without a second
software sleep before swapchain acquisition. The CPU/host presentation path
keeps its existing software limiter. No resolution, particle-count, animation,
filter or input reduction is used for this performance candidate.

Dependencies are isolated from the system toolchain:

```sh
python3 tools/fetch_graphics_sdk.py
sh build-switch.sh
```

The Switch build compiles the pinned Mesa NAK interlock pass with a local patch
for GM20B. For an unconditional pair of fragment interlock markers, texture
sampling before the first marker can proceed before waiting on the ordering
ticket. The original ticket, memory fences and release atomic protect the
marked section. Shaders with demotion or unproven control flow keep the
conservative scope. The NRO links this object ahead of the SDK archive and
uses a distinct NVK shader cache key.

On a first build, the NAK helper caches its verified source inputs under
`local/mesa-nak`. Set `WHJ2_MESA_SOURCE` to a Git checkout containing the
locked Mesa commit to reuse local objects. The NIR header generators use
Mako and PyYAML. `WHJ2_MESA_PYTHON` can select an existing Python environment.
The helper otherwise prepares these dependencies in a project-local environment.

`WHJ2_MESA_SDK` and `WHJ2_RUST_LIBDIR` can reuse an existing matching SDK.
The exact source, artifact and Rust versions/checksums are recorded in
`config/graphics-dependencies.json`. Run `python3 tools/build_graphics_shaders.py` to compile the scene,
compute and presentation shaders for Vulkan 1.1 and update their embedded SPIR-V.

The host build remains available without Vulkan. A Vulkan-enabled host build
can be built with `WHJ2_HOST_VULKAN=1 sh build-host.sh --runtime-only` and
selected with `WHJ2_RENDERER=vulkan`; `WHJ2_VULKAN_LIBRARY` specifies the
host library. `WHJ2_RENDERER=cpu` selects the CPU reference renderer on Switch.
These are diagnostic environment variables, not controller bindings.

Historical host verification predates the tiled renderer and covers 60 title
frames, nine blend modes, dynamic snapshots and title touch/stick logic. It does
not validate this performance candidate. Its shaders are compiled and checked
with SPIR-V validation; no new game-runtime tests or physical Switch frame-rate
measurements have been run for this revision.

### 字体选择

设置页的原“フォント選択”按钮可选择系统默认字体，以及游戏目录 `fonts/` 下的 `.ttf`、`.otf`、`.ttc` 文件。方向键选择，A 确认，B 取消；触摸可选择条目或翻页。选择保存在独立存档目录的 `font.cfg`，恢复默认设置时一并重置。自定义字体缺少的字符由平台字体补充。字体文件不随项目分发。字体设置提供原版 1/2/4/8 倍采样（默认 4 倍）、加粗、阴影、两种描边宽度及描边/阴影 RGB 颜色。十字键左右调整、A 循环切换；选择“設定”保存，B 或“キャンセル”恢复进入前的设置。字体集合会逐一列出字体族和字面名称，并保存文件名与字面索引；旧版仅保存文件名的配置按第一个字面读取。字体来源为当前平台默认字体及游戏目录提供的字体文件，不枚举 Windows 安装字体。

脚本图层的 `z`、`rotx`、`roty`、`scalez` 已接入外层三维变换与位置补间，并传递到嵌套动画和嵌套粒子；普通平面图层沿用二维路径。原生正文雨景的相关指令已接线，尚无该改动的实机画面对照。

静态位图的 `imgfilter` 支持原 `blur10`、`blur20`、`mono`、`sepia` 公式，按图层前四个图像槽缓存处理结果；`none` 恢复源图。动态影片及嵌套非位图的滤镜仍待实现，会保留未支持日志。

历史记录保留四条布局。十字键上下选择条目，A 重播该条语音；左右滚动选中条目的长正文。触摸正文可上下拖动，右侧原滚动条浏览更早记录。历史正文共用文本换行和已支持的标签解析，并按可见行缓存，不再在固定高度处永久丢弃后续文字。未完成全部原版日志演出的像素/实机验收。

存档缩略图按原配置 `SaveThumPriority` 截取图层（本作默认 999），排除默认优先级 1100 的文字框和 1110 的头像。原剧情使用的 `savefilter,0` 明确保持无缩略图滤镜；其他缩略图滤镜模式尚未实现。
