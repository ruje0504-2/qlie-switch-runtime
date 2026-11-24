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

产物为`build-switch/whj2-runtime.nro`。默认从`sdmc:/switch/whj2/game`读取原版文件，在`sdmc:/switch/whj2/saves`保存状态。原版GameData、DLL/key.fkey和日文EXE来自本地游戏副本；运行时不会执行EXE。

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
- 双纹理位图遮罩、蓝通道阈值。动态遮罩和影片帧保有队列内快照，静态图片继续共享。
- 角色基础动作与独立立绘替换槽、命名/数字动作、直接后向关键帧循环。显式与自动头像选择、逐页/持续显隐、前景层。
- 图层位置/颜色渐变、相对位移、播放速度。原消息框显示/隐藏动画、逐字正文、姓名别名，以及spd速度标签与pc居中标签。
- 音乐/音效、顶层动画内嵌音频及独立direct总线、音量和播放位置保存。独立Theora影片按音频采样时钟播放。
- 独立系统通关进度与原版额外菜单。音乐鉴赏已接入18曲原资源、PCM游标计时/定位、曲目切换/循环、专用音量和控制器/触摸操作；底部跨模式导航及完整图库/回想仍待接线。
- 场景交叠/cut/catch、点击/固定/正文等待和原生等待续行。图像、音轨、过渡、逐字及等待状态可以保存和恢复。

CG目录支持原33组784个差分资源，按已显示资源解锁整组；支持原按钮分页、图片/动画差分、放大和平移，以及CG/回想/音乐页面互跳。显示记录保存在进度v2，旧进度v1可读取。动画CG按原关键帧时间循环；34个影片资源中24个音轨解码后为全零PCM，10个无音轨。完整原UI转场仍待接线。

回想目录使用原版两页缩略图和按钮，按实际经过的剧情标签解锁；回放保留原选项并在结束后返回目录，回放期间禁用存读档。当前仅完成目录和播放/返回接线，尚未完成所有回想段落的视听验收。

保存格式仍处于开发阶段。旧版本状态的兼容分支不能替代完整演出恢复验收。

## 已知缺口

原版剩余UI（字体、画质等设置）、部分富文本和禁则排版、精确字体与文本计时对齐、其余缓动/图元/随机/上下文、3D多轴旋转与材质光照、非位图遮罩、影片粒子、条件关键帧跳转、嵌套音轨与完整对象生命周期、规则/马赛克/动画转场、既读跳过、自动播放的特殊文本计时和语音seek仍未全部完成。对白ID到实际载荷的映射尚未闭合。

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
