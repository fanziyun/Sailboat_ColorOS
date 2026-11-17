# uinput_tap

最小化的 `/dev/uinput` 触摸注入工具：创建一块虚拟触摸屏，发一次点击，然后销毁设备。

用它验证"点击提频"这类只在 evdev 层才成立的链路——`input tap` 走 InputDispatcher，不经过 `/dev/input`，WALT 的内核 input_boost 看不到。

参数：`uinput_tap [x] [y] [ready_ms]`，坐标是 evdev 绝对值（x 0–143999、y 0–319999），`ready_ms` 是设备创建后等待 InputReader 挂载的时间。

```bash
bash tools/uinput_tap/build.sh
adb push out/uinput_tap /data/local/tmp/
adb shell su -c 'chmod 0755 /data/local/tmp/uinput_tap && /data/local/tmp/uinput_tap 72000 160000 3000'
```

`components/Sail_FrameBoost/tools/uinput_tap.c` 是另一份更完整的 FIFO 常驻版本（freestanding、自带启动汇编，需要 `build_tap.py` 流程）；本目录这份用 NDK 直接编静态可执行文件，只做单次点击，适合脚本里调用。
