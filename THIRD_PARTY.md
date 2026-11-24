# 来源与许可

本工程新代码以 GPL-2.0-or-later 分发。原游戏数据、密钥、EXE 和存档不属于此许可，不得公开上传。

参考 kisaku-switch-runtime，固定提交 95d71d9bcaee9dc7ab48b4bda8d3139a817e6cc7（GPL-2.0-or-later）。仅复用适用的平台构建、字体和音视频基础；AI6WIN VM 和游戏专用系统不适用。

QLIE 封包格式、内容解密及压缩算法参考 morkt/GARbro 的 ArcFormats/Qlie/ArcQLIE.cs、Encryption.cs 与 QlieMersenneTwister.cs；本地原版 EXE 反汇编用于核对当前变体。GARbro 相关代码适用以下MIT许可（MT原作者 Makoto Matsumoto / Takuji Nishimura）。

Copyright (C) 2015-2017 by morkt
Copyright (C) 1997 Makoto Matsumoto and Takuji Nishimura.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

rafi441/qlie-unpack 的固定研究快照 531694715df21a9940578f4319d246ac8aa794de 仅用于独立算法比对，未发现许可文件，不复制其源码到分发工程。

## biko3-switch-runtime FPS overlay

The compact glyph rows and cyan/black FPS styling in src/fps_overlay.cpp are adapted from https://github.com/ruje0504-2/biko3-switch-runtime at bd70f2c1278ea907740baa0c3d3e9d64e9cb1b57 (runtime/scene/fps_overlay.c and runtime/ui/debug_overlay.c). Copyright (C) 2025-2026 ruje0504-2. Licensed under 0BSD; see licenses/biko3-0bsd.txt. Frame measurement is based on actual presentation intervals and does not copy its game simulation clock.
