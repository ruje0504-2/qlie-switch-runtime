# QLIE Switch 运行时

[![GPL-2.0-or-later](https://img.shields.io/badge/GPL--2.0--or--later-blue)](LICENSE)

面向 Nintendo Switch/Horizon 的原生 QLIE 运行时，也可在 Mac 上构建和验证。目前仅适配《美少女万华镜2》日文版，尚未完成全部实机验收。

- 通用引擎：`libqlie-runtime.a`
- 游戏适配：`games/whj2/`
- 仓库不包含游戏资源、图标、密钥、存档或安装包。

## 构建

Mac 需要 C/C++17、SDL2、FreeType、libpng、libjpeg 和 FFmpeg：

```sh
./build-host.sh --runtime-only
build/qlie-switch-runtime /path/to/game /path/to/saves
```

Switch 需要 devkitPro、devkitA64、libnx 及对应依赖，版本见 [构建依赖配置](config/graphics-dependencies.json)：

```sh
python3 tools/fetch_graphics_sdk.py
./build-switch.sh
```

输出为 `build-switch/qlie-switch-runtime.nro`。只构建通用引擎时，两种构建脚本均可使用 `--engine-only`。

## Switch 运行

将 NRO 放入 `sdmc:/switch/whj2/`，游戏数据放入其 `game/` 子目录，存档保存在 `saves/`。运行时不执行 Windows EXE；所需 `DLL/key.fkey` 和 `DLL/icon.key` 应从自己的游戏副本准备，提取工具见 [extract_icon_key.py](tools/extract_icon_key.py)。

默认使用 deko3d。支持 Joy-Con 和触摸：A 确认、B 取消，L/R 存读档，X 设置，十字键上查看历史，ZL 自动播放，ZR 切换快进。

## 当前范围

已接入剧情、图像动画、音视频、存读档及鉴赏界面。Mac 验证和 Switch 构建结果不代表完整实机验收。

当前支持 `FilePackVer3.0`。《罪と罰の少女》的 `3.1` 格式仅完成差异调查，尚未适配。

## 许可

源码采用 [GPL 第 2 版或任何后续版本](LICENSE)。第三方来源与许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。游戏资源不属于本项目源码许可范围。
