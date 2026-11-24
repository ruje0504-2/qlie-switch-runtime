# WHJ2-Switch

《美少女万華鏡2》日文原版的原生QLIE运行时移植，目标为Horizon NRO，先在Mac验证。**开发候选，尚非完整可玩版本，未通过Switch实机验收。**

仓库不包含游戏资源、密钥、EXE或存档。原版数据只读；移植存档独立保存。

参考 kisaku-switch-runtime 固定提交 `95d71d9bcaee9dc7ab48b4bda8d3139a817e6cc7`。QLIE与AI6WIN不同，没有复用AI6WIN的VM。字体/媒体基础按GPL复用，来源见THIRD_PARTY.md。

## Mac构建

需要C/C++17编译器、Python3、SDL2、FreeType、libpng、libjpeg和FFmpeg开发库；默认使用Homebrew pkg-config，可用PKG_CONFIG覆盖。

```sh
./build-host.sh
build/whj2-runtime /absolute/path/to/game /absolute/path/to/independent-saves
```

## Switch候选

需要devkitPro、devkitA64、libnx和Switch portlibs。

```sh
./build-switch.sh
```

生成 `build-switch/whj2-runtime.nro`。该候选直接使用libnx Joy-Con输入和SDL触摸，Switch构建不处理键盘鼠标事件。默认从 `sdmc:/switch/whj2/game` 读取原版文件，在 `sdmc:/switch/whj2/saves` 写移植存档。原版GameData、DLL/key.fkey和日文EXE必须来自本地游戏副本；不会执行EXE。

Joy-Con：A推进/确认，B或＋/－返回菜单，上下方向/摇杆选择，X保存、Y读档。单侧Joy-Con另有SL确认、SR返回、L/R保存、ZL/ZR读档。触摸支持推进、菜单项和分支选择，右上角提供保存、读档与菜单。**这些入口已经接线，仍需要真实Joy-Con和Switch触摸验收。**

## 实现范围

已实现资源解密、日文文字、原始剧情控制流和四个结局路径、段落提交、静态背景与角色、音乐/音效、静态画面及音轨位置存读档。空存档读档会提示并留在菜单。

独立Theora影片按音频采样时钟播放，Mac完整片尾自然结束已有证据。复合动画已接入跨场景图元排序、两套变换栈、周期运动轨道、上下文尺寸、矩形与位图混合（透明、屏幕、乘法、差值、最大/最小、加减）。混合使用原版定点运算规则；普通矩形、震动和闪光有原资源像素验证。位图粒子、嵌套位图时钟和顶层动画内嵌音效已接通。模型、部分运动函数与属性、嵌套音轨和完整对象生命周期尚未完成。对白语音ID映射未闭合。未实现路径显式写入PENDING日志，不作为完整演出通过。

保存格式仍为开发格式；完整动画存档和实机可玩性均未验收。

## 验证入口

```sh
build/story-probe /absolute/path/to/game 000100
build/presentation-probe /absolute/path/to/game 000100
build/presentation-probe /absolute/path/to/game --effect-render
build/presentation-probe /absolute/path/to/game --rectangle-render
build/render-queue-probe
build/animation-stack-probe
build/whj2-runtime /absolute/path/to/game /absolute/path/to/empty-test-saves --touch-smoke 120
build/whj2-runtime /absolute/path/to/game /absolute/path/to/test-saves --movie movie/ed
```

触摸冒烟使用SDL注入事件，只验证事件处理和状态流程，不能替代实际触摸屏。资源/媒体/逻辑探针不能替代演出与Switch实机验收。构建输出集中在固定build目录，更新时覆盖，不生成重复版本副本。

源码遵循GPL-2.0-or-later；原始游戏数据不属于源码许可范围。

Bitmap particles (primitive 20) now use the native deterministic random table and viewport-centered depth projection. Scene state version 6 preserves generated resource identities across saves and clears stale future caches when restoring; versions 2 through 5 scene saves remain readable. Nested/video particles, spherical particles, and particles with 3D rotation remain incomplete.

Nested bitmap animations select their child motion and interpolate the child timeline from the parent key. Child loop bounds are respected. Zero-sized logical subcanvases are supported; missing dimension context remains reported. Symbolic child timeline inputs and nested embedded audio are still incomplete.

Procedural function 5 uses the native time-quotient random-table lookup and restored per-scene identity seed. Other random functions remain incomplete.

Primitive 10 bitmap gradient masks now use independent source/mask coordinates and native blue-channel threshold arithmetic, including inverse masks and fixed-point edge behavior. Non-bitmap and 3D mask variants remain incomplete; sampling currently follows the CPU renderer’s nearest-neighbor path.

Top-level animation sound events decode embedded ABMP members on 16 direct-sound buses, separate from streamed music, voice and movies. The image dchannel base and fixed-point volume are applied; explicit zero volume stops the configured base bus, matching the script behavior. Looping, event timing, playback positions and pending mute state survive saves. Legacy eight-channel audio saves remain readable. Nested sound events and the remaining native object lifecycle are incomplete.

Image commands support timed position/scale/rotation/priority and per-channel color/opacity transitions, axial offsets, and animation playback speed. Retargeting starts from the displayed value. Active transitions survive saves. Step, linear, outquart and inquart easing are implemented; additional easing modes remain reported as incomplete.

Character base animations use a per-layer portrait in image slot 0 without modifying shared animation resources. Named and numeric motions are supported. Direct backward key jumps preserve their elapsed remainder; conditional key jumps remain incomplete. Empty portrait slots stay empty, and portrait replacements survive saves.

Sentence transitions support captured-scene overlaps, cuts and template durations. Captures and transition progress survive saves; interrupted overlaps start from the currently displayed scene. Rule masks, mosaic and animation transition modes, some transition and face/UI behaviors remain incomplete and reported.

Queued gradient masks and decoded video frames retain an immutable per-queue snapshot until drawing completes, so repeated animated assets cannot overwrite another instance’s pending frame. Static textures continue to use shared references.

Face portraits support explicit files, per-page or persistent display, show/clear controls, and foreground rendering above the text panel. Automatic selection follows project FaceType, name aliases, character IDs and the original replacement tables, with configured fallback filenames. Missing candidates remain reported.

Sentence timing supports fixed waits, interruptible click waits, message waits, automatic fade-duration waits and wait-only continuation. Native script wait and click-wait events suspend parsing without blocking rendering. Remaining wait time and in-progress page assembly survive saves; skip-mode timing and voice-seek behavior remain incomplete.

Dialogue uses the original message-frame container and text slot, with configured text area, font size and color. UTF-8 typewriter progress survives saves; confirming unfinished text reveals it before a later confirmation advances. Original rich-text controls, detailed line-breaking rules and exact native font/timing parity remain incomplete.
