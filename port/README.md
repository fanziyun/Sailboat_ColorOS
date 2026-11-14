# port/ — 特性移植目录

每个特性一个目录，结构固定：

```
port/<feature>/
  README.md   适用 ROM/设备/版本、上游来源（tag/路径）、改动文件表、节点与接口、构建方式、验证记录、未完成项
  src/        适配后的源码，保留上游相对路径（例如 src/vendor/oplus/kernel/cpu/sched/sched_assist/）
```

规则：

- `src/` 只放改编过、准备编译的源码；未改动的上游依赖通过 `src_upstream/` 引用，不复制进来。
- 每个文件第一行写清上游来源（`// from OnePlusOSS sm8850 @ <branch>/<path>` 这类一行说明放在 README，不写进源码）。
- 源码不带叙述性注释，标记统一 `sailboat_coloros_<group>` 前缀。
- 验证数据、A/B 结果写进 README，不写进源码。
