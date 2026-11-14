# uad（touch_boost / ua_ioctl）

目标：`vendor/oplus/kernel/cpu/uad/` 的 `touch_boost.c` 与 `ua_ioctl/`，给 frame_boost 提供用户态入口。

上游来源：

- 6.18：`.../cpu/uad/`（含 `ua_ioctl/frame_ioctl.c`）
- 5.15：`.../cpu/uad/{touch_boost.c,uag_main,...}`

状态：待移植，注意 ioctl 命令号需与 ROM 侧一致。

未完成：全部。
