# port/_shared — 共享约定与移植策略

- 目标内核：vermeer / SM8550 / `android13-5.15`（Sailboat，5.15.223），WALT 内置，governor `walt`。
- 模块以 GKI 供应商模块形态存在：源码在 `vendor/oplus/kernel/...`，只用导出符号与 `android_vh_*` 钩子。
- 5.15 树里已存在的能力（`/proc/sys/walt/*` 的 input_boost、sched_user_hint、sched_boost 等）优先复用，不重复造。
- 缺符号/缺 hook 时先列缺口清单，再决定用 in-tree graft（P5）补，不先改内核。
