# QLIE Switch Runtime

[![GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue)](LICENSE)

面向 Nintendo Switch/Horizon 和 macOS 的 QLIE 运行时。WHJ2 和 WHJ4 均已完成 Switch 实机使用测试。WHJ4 涵盖日文本体和可选汉化更新的本地打包。

[优化手册](docs/optimization/README.md)记录 deko3d 渲染、影片硬解、资源预读、音频和界面的实现及测量范围。

## 引擎与适配

`libqlie-runtime.a` 包含 FilePackVer3.0 与 WHJ4 使用的 3.1 资源读取、剧本解析、图像、音频和影片。`games/runtime/` 是共用前端，`games/profiles/` 保存各作的资源路径、剧本规则和打包配置。

Switch 默认使用 deko3d，帧率上限为 60 FPS。据项目维护者反馈，已验收的 WHJ2 版本在 Switch 默频下基本稳定在 60 FPS。两作共用 Joy-Con 和触摸、资源预读以及音视频实现。

## 构建

Mac 需要 C/C++17、Python 3、SDL2、FreeType、libpng、libjpeg 和 FFmpeg 开发库。

`QLIE_GAME` 选择 `whj2` 或 `whj4`，默认 `whj2`。

```sh
QLIE_GAME=whj2 ./build-host.sh --runtime-only
QLIE_GAME=whj4 ./build-host.sh --runtime-only
build/whj4-switch-runtime /absolute/path/to/whj4-game /absolute/path/to/whj4-saves
```

Switch 需要 devkitPro、devkitA64、libnx 和 Switch portlibs。

```sh
python3 tools/fetch_graphics_sdk.py
QLIE_GAME=whj2 ./build-switch.sh
QLIE_GAME=whj4 ./build-switch.sh
```

| 目标 | NRO（位于 `build-switch/`） | 游戏与存档根目录 | 游戏输入 |
| --- | --- | --- | --- |
| `whj2` | `qlie-switch-runtime.nro` | `sdmc:/switch/whj2` | `game/GameData`、`game/DLL/key.fkey`、`game/DLL/icon.key` |
| `whj4` | `whj4-switch-runtime.nro` | `sdmc:/switch/whj4` | `game/GameData`、`game/reskey.fkey` |

各目标在自己的 `saves/` 下写入存档。两平台均支持 `--engine-only`。

WHJ4 运行目录需准备日文原版的九个封包和从原版 EXE 导出的 RESKEY。
剧情中的独立 OGV 与 ABMP 内的影片共用解码器。原版 Theora 使用软件解码。自用直装包可沿用 WHJ2 的 H.264/Matroska 参数，将影片通过 `tools/repack_video.cpp` 回填到 FilePackVer3.1，由 NVDEC 解码并把 NV12 帧交给 deko3d。ABMP 中的 IMOAVI 仍按原 JPEG 序列读取。

```sh
python3 tools/extract_reskey.py /absolute/path/to/original.exe /absolute/path/to/whj4-game/reskey.fkey
```

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

WHJ2 直装配置为 Title ID `01007F8E5C120000`、版本 `1.0.1`。包内游戏数据通过 RomFS 读取，直装启动要求成功挂载系统 SaveData。

WHJ4 的构建和打包均选 `QLIE_GAME=whj4`。日文本体为 `01007F8E5C140000`，版本 `1.0.1`。汉化更新 `1.0.2` 使用更新 ID `01007F8E5C140800`，以该日文本体制成差分包。先安装日文本体，再安装汉化更新。HOME 中文名称为「美少女万華鏡 -罪与罚的少女-」。日文与汉化的部分脚本分段不同，跨语言读档尚未验证。

## 许可

项目采用 GPL v2 或更高版本。见 [LICENSE](LICENSE) 和[第三方许可](THIRD_PARTY.md)。游戏资源、图标、密钥、存档和安装包在本地准备。
