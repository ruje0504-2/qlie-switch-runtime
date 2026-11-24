# WHJ2-Switch

《美少女万華鏡2》日文原版的原生QLIE运行时移植，目标为Nintendo Switch/Horizon NRO，使用Mac作为开发验证平台。**当前是开发候选，尚非完整可玩版本，未通过Switch实机验收。**

仓库只包含技术源码、构建脚本和许可。原版游戏资源、EXE、密钥及存档不属于本仓库；游戏数据只读，移植存档单独保存。

## 构建和运行

Mac需要C/C++17编译器、Python3、SDL2、FreeType、libpng、libjpeg与FFmpeg开发库。默认使用Homebrew pkg-config，可用PKG_CONFIG覆盖。

```sh
./build-host.sh
build/whj2-runtime /absolute/path/to/game /absolute/path/to/independent-saves
```

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

## 输入

目标输入为Joy-Con与触摸。L打开存档界面，R打开读档界面，A点击/确认，B取消；正文中B隐藏/显示文字框。X打开菜单页面，十字键上打开历史文字，按住ZR强制快进、松开停止，选项处不自动选择。Y或屏幕下滑调出原版顶部栏，再按Y/B收起。存读档界面中方向键选择槽位，L/R翻页。游戏画面不显示自制按键说明或常驻触摸按钮。−键切换左下角FPS显示，默认隐藏；采用biko3参考的浅蓝黑影像素字，显示实际每秒提交帧率，60FPS上限始终独立生效。
标题花海的左右视角跟随触摸按下与拖动；左摇杆左右调整视角，松开后保留角度。十字键独立控制菜单选项。
这些入口已接线，但仍缺实际Joy-Con和Switch触摸验收。SDL注入事件仅验证输入处理和状态流程。

## 实现范围

- QLIE资源解密/BPE、日文显示、剧情控制流与四结局逻辑、段落提交、空存档正常处理。
- 全局图元排序、包含零图元控制层的变换栈、原版定点混合、矩形、部分周期/随机轨道、上下文尺寸、位图与嵌套动画粒子。渲染状态支持压栈、退一层和清空全栈。
- 原标题与选择框资源、动画和碰撞矩形；天空模型的纹理三角形、单轴嵌套三维变换、透视UV与近裁剪。无光照材质使用原始颜色；多轴精度和其他光照模式仍待完成。
- 双纹理位图遮罩、蓝通道阈值。GPU直接采样原图与遮罩；CPU回退的动态遮罩和影片帧保有队列内快照，静态图片继续共享。
- 角色基础动作与独立立绘替换槽、命名/数字动作、直接后向关键帧循环。显式与自动头像选择、逐页/持续显隐、前景层。
- 图层位置/颜色渐变、相对位移、播放速度。原消息框显示/隐藏动画、逐字正文、姓名别名，以及spd速度标签与pc居中标签。
- 音乐/音效、顶层动画内嵌音频及独立direct总线、音量和播放位置保存。独立Theora影片按音频采样时钟播放。
- 独立系统通关进度与原版额外菜单。音乐鉴赏已接入18曲原资源、PCM游标计时/定位、曲目切换/循环、专用音量和控制器/触摸操作；底部跨模式导航及完整图库/回想仍待接线。
- 场景交叠/cut/catch/原规则图渐变、点击/固定/正文等待和原生等待续行。图像、音轨、过渡、逐字及等待状态可以保存和恢复。

CG目录支持原33组784个差分资源，按已显示资源解锁整组；支持原按钮分页、图片/动画差分、放大和平移，以及CG/回想/音乐页面互跳。显示与已读记录保存在进度v3，旧进度v1/v2可读取，缺失的已读记录保持未读。动画CG按原关键帧时间循环；34个影片资源中24个音轨解码后为全零PCM，10个无音轨。完整原UI转场仍待接线。
剧情图像的rep替换按原脚本顺序应用，支持遮罩/粒子位图引用、实例隔离和存读档恢复；更换基础文件会清除旧替换。
回想播放时，X打开的菜单提供独立返回回想目录操作；取消菜单继续播放，返回标题保持原义。
ZL或原位置的触摸跳过按钮切换普通快进：遵守原设置的仅已读/含未读模式，在未读文字与选项处停止。ZR保持强制快进。已读页面独立持久化，flow2保存当前页标识并兼容flow1。

回想目录使用原版两页缩略图和按钮，按实际经过的剧情标签解锁；回放保留原选项并在结束后返回目录，回放期间禁用存读档。当前仅完成目录和播放/返回接线，尚未完成所有回想段落的视听验收。

保存格式仍处于开发阶段。旧版本状态的兼容分支不能替代完整演出恢复验收。

## 已知缺口

原版剩余UI（字体、画质等设置）、部分富文本和禁则排版、精确字体与文本计时对齐、其余缓动/图元/随机/上下文、3D多轴旋转与材质光照、非位图遮罩、影片粒子、条件关键帧跳转、嵌套音轨与完整对象生命周期、马赛克/动画转场、自动播放的特殊文本计时和语音seek仍未全部完成。对白ID到实际载荷的映射尚未闭合。

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

L opens the save panel; R opens the load panel. A confirms a selected slot, and B cancels a panel. X opens the settings page, Up opens text history, and held ZR forces text advance until released or a choice is reached. During dialogue B hides or restores the text box; A first restores hidden text without advancing. Y or a downward swipe opens the original upper menu. No controller legend or permanent touch overlay is drawn.

Save/load panels use the original resources, 15 pages of eight local slots, and small scene thumbnails. History contains displayed pages and is included with each slot. The settings page supports nine sliders for text speed, text-window opacity, auto-play delay, and six volume controls, plus click-to-stop voice and auto-play voice-wait toggles. Sliders support captured touch drags and controller adjustment, with local persistence and reset confirmation. The original auto-play button controls timed progression and waits for text completion and, when selected, voice completion. Remaining font, display-quality, and character-voice controls are pending; this is not full settings parity. Confirmation dialogs currently use original artwork without the original transition timing. Physical controller/touch and Switch runtime acceptance remain pending.


## Vulkan renderer

Switch scenes use the GPU through the statically linked Vulkan/NVK
backend from [danfromtico/mesa-switch](https://github.com/danfromtico/mesa-switch),
Mesa 26.2.4, commit `0ff52079ca9e9e837ed27eb95a8d239f439a1864`.
The authored flower particles, sky mesh, texture sampling, ordered integer
blending and left-stick/touch viewpoint remain in the same scene graph.
One compute dispatch processes ordered draw lists for 16x16 screen tiles. Each
invocation owns one pixel, keeps its accumulated color in registers and applies
the original sampling and integer blend equations in scene order. This replaces
the old dispatch and global memory barrier after every primitive. A shared
texture arena retains uploads; each immutable queue checks a source image once
instead of comparing its complete pixels for every particle or mesh triangle.
A Vulkan fullscreen pass presents directly through the native NWindow
swapchain, including the FPS overlay.
On this path, SDL provides audio/events only; Joy-Con and touchscreen input use
libnx. SDL does not create a video window or claim the NWindow. The title,
story layers, message windows, choices, CG/replay/music screens, system panels,
glyphs, dimming, bitmap masks and scene transitions share this GPU composition
path without per-frame readback. Movies retain the existing CPU decoder and
upload its frame directly; overlays are composed on the GPU. Transition/save
snapshots still use the CPU reference renderer when requested by game logic.
Save-panel metadata and thumbnails are cached while the panel is open; glyph
bitmaps are cached separately from their GPU placement.

Native FIFO presentation supplies the console's 60 Hz limit, without a second
software sleep before swapchain acquisition. The CPU/host presentation path
keeps its existing software limiter. No resolution, particle-count, animation,
filter or input reduction is used for this performance candidate.

Dependencies are isolated from the system toolchain:

```sh
python3 tools/fetch_graphics_sdk.py
sh build-switch.sh
```

`WHJ2_MESA_SDK` and `WHJ2_RUST_LIBDIR` can reuse an existing matching SDK.
The exact source, artifact and Rust versions/checksums are recorded in
`config/graphics-dependencies.json`. The shader is `shaders/raster.comp`;
`src/raster_spv.inc` contains its Vulkan 1.1 SPIR-V generated with
`glslangValidator -V --target-env vulkan1.1`.

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
