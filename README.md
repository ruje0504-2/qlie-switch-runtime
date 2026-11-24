# QLIE Switch Runtime

[![GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue)](LICENSE)

面向 Nintendo Switch/Horizon 的原生 QLIE 运行时，也可在 Mac 上运行。当前适配《美少女万華鏡2》日文原版。开发版 NRO `bc2e4ec` 已通过 Switch 实机使用验收。当前源码含菜单、幕间和三段剧情时序修复。NSP 的 HOME 启动单独验收。

[优化手册](docs/optimization/README.md)记录 deko3d 渲染、影片硬解、资源预读、音频和界面的实现及测量范围。

## 引擎与适配

`libqlie-runtime.a` 可独立构建，包含 FilePackVer3.0 资源读取、脚本解析、图像、音频和影片。WHJ2 的入口、菜单和配置在 `games/whj2/`。FilePackVer3.1 尚未支持。

正式开发版默认使用 deko3d，帧率上限为 60 FPS。据项目维护者实机反馈，Switch 默频下基本稳定在 60 FPS。Joy-Con 和触摸可控制剧情、存读档、设置和历史。存读档、设置、CG、回想和音乐沿用历史文字的 250 ms 缩放。剧情中保留原人物标题卡和脚本标注的时序。

## 构建

Mac 需要 C/C++17、Python 3、SDL2、FreeType、libpng、libjpeg 和 FFmpeg 开发库。

```sh
./build-host.sh
build/qlie-switch-runtime /absolute/path/to/game /absolute/path/to/independent-saves
```

Switch 需要 devkitPro、devkitA64、libnx 和 Switch portlibs。

```sh
python3 tools/fetch_graphics_sdk.py
./build-switch.sh
```

产物为 `build-switch/qlie-switch-runtime.nro`。NRO 默认从 `sdmc:/switch/whj2/game` 读游戏数据，存档写入 `sdmc:/switch/whj2/saves`。使用本地原版七个封包、`DLL/key.fkey` 和 256 字节的 `DLL/icon.key`。独立密钥可用 `tools/extract_icon_key.py` 从本地原版提取。两平台的构建脚本均支持 `--engine-only`。

## 本地直装包

打包还需要 FFmpeg、hacbrewpack、hactool 和本地 Switch keyset。

```sh
./build-switch.sh --elf-only
python3 tools/package_nsp.py \
  --game /absolute/path/to/game \
  --icon /absolute/path/to/icon.jpg \
  --output /absolute/path/to/whj2.nsp
```

WHJ2 直装配置为 Title ID `01007F8E5C120000`、版本 `1.0.1`。包内游戏数据通过 RomFS 读取，直装启动要求成功挂载系统 SaveData。

## 许可

项目采用 GPL v2 或更高版本。见 [LICENSE](LICENSE) 和[第三方许可](THIRD_PARTY.md)。游戏资源、图标、密钥、存档和安装包在本地准备。
