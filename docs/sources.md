# 源码清单与获取方式

## OnePlusOSS 开源源码（主来源）

| 仓库 | 分支 | 内容 | 用途 |
|---|---|---|---|
| `OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850` | `oneplus/sm8850_b_16.0.0_oneplus_15`（Android 16 / 6.18） | `vendor/oplus/kernel/cpu/` 下 `sched/{sched_assist,frame_boost,sched_info,task_cpustats,task_load,task_sched,eas_opt,sched_tune}`、`uad/`（`touch_boost.c`、`ua_ioctl/`）、`sched_ext/`、`smart_freq/`、`freq_qos_arbiter/`、`geas/`、`midas/`、`game_opt/`、`freqqos_monitor/` 等 | 行为与接口主参照（与 ROM 里的 HAL 同代） |
| `OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550` | `oneplus/sm8550_v_15.0.0_oneplus11`（Android 15 / 5.15.167） | 同一批模块的 5.15 形态（`cpu/sched/*`、`cpu/uad/*`、`cpu/midas/*`、`cpu/freqqos_monitor/*` 等） | 可移植实现参考（同 SoC、同 5.15） |

说明：

- 一加 16 原版 vendor_dlkm 里的模块是 `6.18.21-android17-5-o-maybe-dirty-*`，与上述 6.18 树同族；若后续出现 ColorOS 17 对应分支（17.0），优先切到该分支。
- 拉取方式见 `scripts/fetch_upstream.sh`（sparse checkout 只取 `vendor/oplus/kernel`）。

## .ko 反编译（补充来源）

仅在源码缺失时使用，位置：

- 一加 16 `vendor_dlkm.img` 解包树：c17 项目 `out/op16_vdlkm/lib/modules/`（718 个 .ko）。
- 一加 16 `vendor_boot.img` 第一阶段模块：c17 项目 `out/op16_vdlkm_vendor_boot/`（`sched-walt.ko`、`oplus_bsp_sched_assist.ko`、`oplus_bsp_frame_boost.ko`、`oplus_bsp_schedinfo.ko`）。
- 解包/解码脚本：c17 项目 `out/decode_ramdisk.py`、`out/scan_vdlkm.py`。

反编译结论只用来补：ioctl 命令号与参数结构、proc 节点格式、内置常量与白名单、闭源分支的行为差异。拿到结论后在对应 `port/<feature>/README.md` 记录证据来源（文件 + 偏移/符号）。

## 为什么不能直接加载 6.18 的 .ko

见 c17 项目 `docs/一加16-vendor_dlkm内核模块核对-ColorOS17.md`：全部模块 vermagic `6.18.21-android17-5-o-maybe-dirty-{4k,16k}` + `modversions`，目标内核 5.15 的 vermagic/符号 CRC/API 三重不兼容。
