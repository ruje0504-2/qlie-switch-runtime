# 4. 基准和复核方法

[返回目录](README.md)

1. 固定源码、后端、影片和场景起点。记录正在运行的提交与构建哈希。
2. 用同一帧或同一路径交替测旧、新版本，丢弃预先约定的预热样本。分别汇报 CPU 建帧、资源等待、栅栏、GPU 光栅和呈现。
3. 精确合成路径比较完整帧哈希，转色路径记录独立像素误差。输入、存读档和音画时序使用相同前置状态。
4. 历史 Switch 测量按同场景匹配后端、GPU 时间、PMU 和直写／回补样本。当前正式构建关闭这些探针，版本通过产物中的提交标记和 SHA-256 核对。

编译产物的 DKSH、SPIR-V 和提交标记用于核对链接。Mac 离屏对照用于验证输出及主机时序。Switch 的帧率、稳定性、音画和物理输入以实际设备观察验收。FTP 回读只确认部署文件的完整性。

默认 Mac 与 Switch 构建均关闭诊断。复核 Mac 鉴赏等路径时，可用 `QLIE_HOST_DIAGNOSTICS=1 sh build-host.sh --runtime-only` 构建开发测试版。CG、音乐、回想的 Mac 对照使用稳定帧与开合中段 125 ms 的画布，核对原曲线采样和实际像素变化，并复核交叉导航与返回。

下表用于核对历史启用诊断的记录。

| 字段 | 含义 |
|---|---|
| `dirty_tiles`、`upload_bytes` | 本次重绘图块数和 CPU 上传的纹理字节数。 |
| `cpu_build_ms`、`prior_wait_ms` | CPU 建帧和等待上一次 GPU 提交的时间。 |
| `direct_tiles`、`copy_tiles` | 直接写入显示图像及回补的图块数。 |
| `motion_tiles`、`base_tiles` | 走简单运动及专用不透明底图内核的图块数。 |
| `raster_gpu_ms`、`copy_gpu_ms` | GPU 时间戳测得的着色和回补时间，负值表示该次缺有效计时。 |
| `gpu_load_pct` | 设备全局 GPU PMU 采样。 |
| `NVTEGRA active` | 解码器已产出真实硬件帧。 |
| `DEKO3D VIDEO mapped` | 运行时已将 NV12 表面映射为 deko3d 图像。 |

开发版 NRO `bc2e4ec` 已通过 Switch 实机使用验收。各项 Mac 数值和历史 Switch 采样仍按原测试范围列示。直装包的离线检查覆盖 CNMT 引用、程序分段、控制信息和 RomFS，HOME 启动与系统存档读写仍按直装版本记录。
