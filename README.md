# QLIE Switch Runtime

[![GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue)](LICENSE)

面向 Nintendo Switch/Horizon 和 macOS 的 QLIE 运行时。WHJ2 和 WHJ4 均已完成 Switch 实机使用测试。WHJ4 支持日文本体和可选汉化更新。WHJ5 使用简体中文脚本和日文语音，已由项目维护者在 Switch 实机验收通过。WHJ2.5 和 WHJ3 提供日文本体和独立中文更新。

[优化手册](docs/optimization/README.md)记录 deko3d 渲染、影片硬解、资源预读、音频和界面的实现及测量范围。

## 引擎与适配

`libqlie-runtime.a` 包含 FilePackVer3.0 与 WHJ4、WHJ5 使用的 3.1 资源读取、剧本解析、图像、音频和影片。`games/runtime/` 是共用前端，`games/profiles/` 保存各作的资源路径、剧本规则和打包配置。

Switch 默认使用 deko3d，帧率上限为 60 FPS。据项目维护者反馈，已验收的 WHJ2 版本在 Switch 默频下基本稳定在 60 FPS。各作共用 Joy-Con 和触摸、资源预读以及音视频实现。

## 构建

Mac 需要 C/C++17、Python 3、SDL2、FreeType、libpng、libjpeg 和 FFmpeg 开发库。

`QLIE_GAME` 选择 `whj1`、`whj2`、`whj25`、`whj3`、`whj4` 或 `whj5`，默认 `whj2`。

```sh
QLIE_GAME=whj1 ./build-host.sh --runtime-only
QLIE_GAME=whj2 ./build-host.sh --runtime-only
QLIE_GAME=whj25 ./build-host.sh --runtime-only
QLIE_GAME=whj3 ./build-host.sh --runtime-only
QLIE_GAME=whj4 ./build-host.sh --runtime-only
QLIE_GAME=whj5 ./build-host.sh --runtime-only
build/whj4-switch-runtime /absolute/path/to/whj4-game /absolute/path/to/whj4-saves
```

Switch 需要 devkitPro、devkitA64、libnx 和 Switch portlibs。

```sh
python3 tools/fetch_graphics_sdk.py
QLIE_GAME=whj1 ./build-switch.sh
QLIE_GAME=whj2 ./build-switch.sh
QLIE_GAME=whj25 ./build-switch.sh
QLIE_GAME=whj3 ./build-switch.sh
QLIE_GAME=whj4 ./build-switch.sh
QLIE_GAME=whj5 QLIE_SWITCH_VULKAN=0 ./build-switch.sh
```

### 各作运行环境

所有目标共用场景合成、视频解码与回退、音频混音、资源预读和存读档代码。标题布局、可用资源、剧本语义和安装身份由各作的 `games/profiles/` 配置选择。

| 构建目标 | NRO | Mac 资源 / 存档环境变量 | Switch 根目录 |
| --- | --- | --- | --- |
| `whj1` | `whj1-switch-runtime.nro` | `WHJ1_GAME` / `WHJ1_SAVES` | `sdmc:/switch/whj1` |
| `whj2` | `qlie-switch-runtime.nro` | `WHJ2_GAME` / `WHJ2_SAVES` | `sdmc:/switch/whj2` |
| `whj25` | `whj25-switch-runtime.nro` | `WHJ25_GAME` / `WHJ25_SAVES` | `sdmc:/switch/whj25` |
| `whj3` | `whj3-switch-runtime.nro` | `WHJ3_GAME` / `WHJ3_SAVES` | `sdmc:/switch/whj3` |
| `whj4` | `whj4-switch-runtime.nro` | `WHJ4_GAME` / `WHJ4_SAVES` | `sdmc:/switch/whj4` |
| `whj5` | `whj5-switch-runtime.nro` | `WHJ5_GAME` / `WHJ5_SAVES` | `sdmc:/switch/whj5` |

| 游戏 | `game/` 下的资源 | 各作配置 |
| --- | --- | --- |
| WHJ1 | 3.0 格式的 `GameData/data0.pack`、`data1.pack`、`data7.pack`，`DLL/key.fkey`、`DLL/icon.key`，以及 `movie/` 内原版日文名的三个影片。可选中文 `patch.dat` | 初代 AVG 剧本、双结局与额外章、日文本体和独立中文更新 |
| WHJ2 | `GameData/`、`DLL/key.fkey`、`DLL/icon.key` | 原标题花海、按钮位置与剧本指定的幕间节拍 |
| WHJ2.5 | 九个 3.0 分卷、上述两份 DLL 目录密钥，可选 `patch.dat` | 标题按钮依次出现、单页鉴赏、历史叠行与片尾镜头拉远 |
| WHJ3 | 十个 3.0 分卷、上述两份 DLL 目录密钥，可选 `patch.dat` | 双标题背景、序章马车与风声、指定图层的影片及运行时截图替换 |
| WHJ4 | 九个 3.1 分卷、`reskey.fkey` | 日文原版和可选中文更新，保留本作 OGV/IMOAVI 媒体配置 |
| WHJ5 | 十个 3.1 分卷、`reskey.fkey`、`patch.dat` | 简体中文与日文语音、姓名输入、历史回跳、长幅动态 CG |

各目标在自己的 `saves/` 下写入存档。两平台均支持 `--engine-only`。`QLIE_SWITCH_VULKAN=0` 可省略 Vulkan 备用后端及其 SDK。

WHJ4 运行目录需准备日文原版的九个封包和从原版 EXE 导出的 RESKEY。
剧情中的独立 OGV 与 ABMP 内的影片共用解码器。原版 Theora 使用软件解码。自用直装包可沿用 WHJ2 的 H.264/Matroska 参数，将影片通过 `tools/repack_video.cpp` 回填到 FilePackVer3.1，由 NVDEC 解码并把 NV12 帧交给 deko3d。ABMP 中的 IMOAVI 仍按原 JPEG 序列读取。

```sh
python3 tools/extract_reskey.py /absolute/path/to/original.exe /absolute/path/to/whj4-game/reskey.fkey
```

WHJ5 读取 `data0.pack` 至 `data9.pack`，按编号升序叠加同名资源。本地 `patch.dat` 提供简体中文界面词条、字体及其许可。

WHJ2 的 256 字节独立密钥可用 `tools/extract_icon_key.py` 提取。

## 本地直装包

打包还需要 FFmpeg、hacbrewpack、hactool 和本地 Switch keyset。

```sh
QLIE_GAME=whj2 ./build-switch.sh --elf-only
QLIE_GAME=whj2 python3 tools/package_nsp.py \
  --game /absolute/path/to/game \
  --icon /absolute/path/to/icon.jpg \
  --output /absolute/path/to/whj2.nsp
```

WHJ1 日文本体 `1.0.0` 的 Title ID 为 `01007F8E5C110000`，中文更新 `1.0.1` 为 `01007F8E5C110800`。汉化原包 `data8.pack` 的覆盖资源通过单独的 `patch.dat` 安装。WHJ1 是本地验证候选。

WHJ2 直装配置为 Title ID `01007F8E5C120000`、版本 `1.0.1`。包内游戏数据通过 RomFS 读取，直装启动要求成功挂载系统 SaveData。

WHJ2.5 日文本体 `1.0.0` 使用新 Title ID `01007F8E5C1C0000`，配套中文更新 `1.0.2` 为 `01007F8E5C1C0800`。请先安装新本体，再安装中文更新。旧本体 `01007F8E5C12B000` 的系统存档归属旧 ID。

WHJ3 的构建和打包均选 `QLIE_GAME=whj3`，主线日文本体 `1.0.0` 为 `01007F8E5C130000`，配套中文更新 `1.0.4` 为 `01007F8E5C130800`。请配套安装这次主线构建的本体和更新。

WHJ4 的构建和打包均选 `QLIE_GAME=whj4`。日文本体为 `01007F8E5C140000`，版本 `1.0.2`。汉化更新 `1.0.3` 使用更新 ID `01007F8E5C140800`，以该日文本体制成差分包。先安装日文本体，再安装汉化更新。HOME 中文名称为「美少女万華鏡 -罪与罚的少女-」。日文与汉化的部分脚本分段不同，跨语言读档尚未验证。

WHJ5 本地直装配置使用 Title ID `01007F8E5C150000`、版本 `1.0.0`，HOME 名称为「美少女万華鏡 -理與迷宮的少女-」。切换到 `QLIE_GAME=whj5` 使用上面的打包流程。

这组版本修复了剧情切换时短暂出现的文字占位图，覆盖普通对白、全屏字幕和遮罩字卡。该修复已通过 macOS 定点回归与本地 NSP 校验。

## 许可

项目采用 GPL v2 或更高版本。见 [LICENSE](LICENSE) 和[第三方许可](THIRD_PARTY.md)。游戏资源、图标、密钥、存档和安装包在本地准备。
