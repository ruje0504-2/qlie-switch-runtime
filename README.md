# WHJ2-Switch

《美少女万華鏡2》日文原版的原生运行时移植，目标为Horizon NRO，当前先在Mac验证。**开发中，尚非完整可玩版本，未通过Switch实机验收。**

使用用户本地合法游戏数据，源码仓库不包含游戏、密钥、EXE或存档。原版文件只读；运行存档须指定独立目录。

固定参考 kisaku-switch-runtime `95d71d9bcaee9dc7ab48b4bda8d3139a817e6cc7`。QLIE与AI6WIN不同，未复用其VM。字体/媒体基础按GPL复用，来源见THIRD_PARTY.md。

## Mac构建与运行

依赖C/C++17编译器、Python3、SDL2、FreeType、libpng、libjpeg和FFmpeg开发库；默认使用Homebrew pkg-config，可用PKG_CONFIG覆盖。

```sh
./build-host.sh
build/whj2-runtime /absolute/path/to/game /absolute/path/to/independent-saves
```

当前前端可显示日文、推进原始剧情控制流、选择分支并保存/恢复逻辑状态；已接入SDL音乐/音效多声道输出、循环与音量淡变，首段BGM在Mac默认音频设备有非零输出证据。已接入静态背景/透明角色图层和表达切换，支持静态图层、音轨位置与淡变参数存读档；Mac窗口有实际加载与清空后像素一致性证据。复合动画、转场、影片前端、语音映射、完整动画存档及触摸/手柄接线尚未完成。未呈现事件显式写入PENDING-PRESENT日志，不能用这版验证多媒体可玩性。已实现控制按键：Enter/鼠标左键推进，方向键选择，F5/F9逻辑存读档，Esc返回菜单。

资源层逐条校验、全部现有媒体解码与四个结局的逻辑路径已有主机检查，但这些不替代演出、输入与实机验收。可复现探针：

```sh
build/qlie-probe /absolute/path/to/game
build/story-probe /absolute/path/to/game 000100
build/media-probe /absolute/path/to/game
build/animation-probe /absolute/path/to/game
```

`./build-switch.sh`只产生资源/剧情诊断NRO并交叉编译媒体组件，尚未生成图形游戏NRO。需要devkitPro/devkitA64/libnx及Switch portlibs。源码遵循GPL-2.0-or-later；原始游戏数据不属于源码许可范围。
