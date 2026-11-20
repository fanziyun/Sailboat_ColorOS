# 最小 UX 实现：vermeer / 2026-11-20

## 当前交付与范围

Profile：`sailboat_coloros_ux_min`。目标为 android13-5.15 的 ABK 内建实现，固定 sm8550 提交 `0241347436d48df011e2a6f6fe2d8740c26a379c`。

核心已经实现：显式线程 UX 标记、任务入出队、本 rq 合法候选选择、唤醒抢占、tick/运行额度、OTS 逐次初始化、就绪门禁与真实状态计数。不是只创建节点或回显。

不包含：完整唤醒选核、音频、pipeline、Binder/锁继承、RT、调频、FrameBoost。没有重写 EEVDF，也没有照搬6.18的半套切换逻辑。

**本地编译与离线测试不等于已经在手机上跑通。当前手机尚未刷入本实现，旧内核的 enabled 不能据此打开。**

远程构建必须明确引用包含本次实现的源码提交；本地离线产物不会自动替换GitHub工作流的输入。重跑仍引用旧模块提交的工作流，不等于已经构建新版。

## 控制协议

| 操作/状态 | 行为 |
|---|---|
| 启动默认 | OFF；完成必要资源、hook和proc初始化后发布ready |
| 未ready/OFF写UX | 拒绝；不积累等待开启补扫的标签 |
| root写 enabled=1 | ready且初始资源完整时开启FEATURE_COMMON |
| ACTIVE重复写1 | 幂等 |
| ACTIVE写0 | `-EBUSY`，保持开启，不伪装已经排空 |
| 其他enabled位 | `-EINVAL` |
| 回滚 | 重启/恢复已知可启动的旧boot，不支持本次启动内热卸载 |
| 设置分配失败任务的UX | 拒绝或保持无UX，不能解引用空OTS |
| audio/tpd/sched_prop占位接口 | 读写返回`-EOPNOTSUPP`，不再伪报后端成功 |
| lowend_plat_opt写入 | 最小profile不支持，返回`-EOPNOTSUPP`，不开放额外额度倍增策略 |

总开关仅root控制。线程设置的系统入口允许root/system；应用入口保留自身访问范围并受`disable_setting`门禁控制，默认不放行普通应用写入。SELinux策略仍需在Enforcing设备验证，不通过放开SELinux解决。

首版有意拒绝热关闭，避免旧实现“先关开关，再改priority，节点却仍linked”的记账问题。需要热切换时应另行实现停写、在途回调协调、按旧权重排空与重建，不能只删掉`-EBUSY`。

## 调度与生命周期

- 配对注册task enqueue/dequeue，复用上游`queue_ux_thread()`；entity hook不是其替代品。
- pick只检查当前rq的节点；先检查CPU/rq、fair、queued、OTS回指和额度，再在正确rq锁下检查实体层级。暂时被层级限制的节点跳过而不是永久清除。
- 扫描有上限，找不到合法UX候选就回落普通调度，避免损坏节点造成无限循环。
- simple repick仍由handler调用`set_next_entity()`；group路径仍由核心完成切换。保留EEVDF的实体/平均虚拟时间/deadline记账。
- 每次OTS分配显式全初始化，现存线程/idle/fork共用；通过`cmpxchg`只安装一份已初始化对象，竞争失败释放新分配对象。tasklist扫描漏过的在途fork可由sched_fork、enqueue或显式设置的补分配恢复，不把一次扫描当作永久完备保证。free前核对两个节点均已摘除。
- 普通生命周期hook先注册free再注册dup；dup注册失败会撤销free并等待在途回调结束，未暴露分配hook时才能销毁cache。后续不可撤销的RVH注册失败保持OFF，不释放已注册hook所依赖资源。
- 文本与ioctl的“读旧状态→合并/清位→更新双树”统一持有同一任务的pi/rq锁；不再只靠文本节点自己的mutex。保持pi→rq→UX锁顺序，锁内调用setter不重复加rq锁。
- 两个UX树的移除都清节点，运行额度、权重、任务引用按上游路径成对维护。ONCE清除不依赖节点是否仍linked，额度先耗尽再睡眠也会清掉标签；达到额度边界即不再作为UX候选。
- rq锁为静态per-CPU资源。初始化前对task OEM[2]及rq OEM区做占用检查；已有占用时拒绝启动，不覆盖未知所有者。此检查不能证明未来加载的vendor模块永不占用，目标验证仍需核对所有权。
- 裁掉退出boost、自动标签/继承、新任务util和RT选核等非核心hook，避免最小版OFF时继续改变策略。

## ABI 注意

保留5.15的类型/操作编码和native/compat ioctl形态；没有把所有入口静默改成6.18语义。

- 文本`p <tid> 132`设置ANIMATOR；随后`p <tid> 0`仍可能保留ANIMATOR。
- 去掉一个已设置类型应使用对应显式清位；ioctl的RESET/优先级重置与文本入口不同，不能互相代用。
- 文本查询`r <tid>`成功后返回写入长度，不再无条件返回`-EFAULT`。
- 线程TID与应用UID不同；设置必须针对真实线程。
- 对UX优先级/非法位及IM类别编码做边界校验，避免越界权重表/bit操作。未实现的文本RESET操作明确拒绝。

## 观测与真正的闭环验收

新节点：`/proc/oplus_scheduler/sched_assist/ux_status`。

它显示profile、ready/active、`hot_disable=0`，以及enqueue/dequeue、selected、runtime、expired、拒绝/层级跳过/分配失败等计数，另列各CPU的UX成员数和权重。初始化成功与失败均打印一次`sailboat_coloros_ux_min`日志，明确仍为runtime disabled。

目标5.15.225归档配置及固定ACK hook头的适配形态检查也已通过，记录在`out/ux_target_shape_20261120.8FhyRo/`；这里只核对配置和hook原型，不是完整目标树编译。

`selected`是hook选中次数，**不是已经发生的上下文切换次数**；计数增长不能替代目标TID的`sched_switch`证据，也不证明性能更好。

目标验收应完整串起：

1. 核对新profile和ready，确认同树同配置的内建镜像，而不是旧节点。
2. 显式开启后，给测试线程设置UX并读回。
3. 观察实际入树、pick选择及该TID的`sched_switch`，而不只看write成功。
4. 观察runtime/额度耗尽；睡眠时出队，唤醒时重新入队。
5. 显式清除全部实际类型、退出任务，核对对应节点/权重/任务引用回收。
6. 再测试迁移、换类、组层级和初始化失败；先稳定性，最后性能A/B。

单向启用版本的A/B需用独立启动组或不同内核启动组，不能在同一轮启动内强制关闭。状态计数本身有开销，离线正确性检查不作性能结论。

## 构建与回归

公共适配器：`scripts/adapt_sched_assist.py`。ABK的`setup.sh`和`scripts/wsl/build_sched_assist.sh`共用。输入逐文件与固定Git blob比较，仅规范化CRLF，不忽略其他空白/内容差异；临时副本事务式适配，二次运行字节不变，缺锚点/不支持的hook或配置则停止。

本地WSL SDK：`/home/fanziyun/sailboat_hans_ddk_20261119/sdk/opt/ddk/`，r450784e、5.15.202、CFI和FULL LTO。与目标5.15.225/Thin LTO不同，不能替代目标编译或运行验证。

执行过的检查：

- 9项Python回归：真实上游适配、幂等、缺锚点事务性、SHA/配置/hook形态拒绝、篡改拒绝、CRLF与内容差异区分、ABK两阶段及重复注入（含before_build阶段配置漂移拒绝）。
- host C + ASan/UBSan：启用状态矩阵、64种候选状态、输入边界，以及从实际适配源码提取的OTS分配/初始化/安装/注册函数，覆盖脏对象复用、OOM、安装竞争和生命周期hook部分注册失败清理。
- 第二组真实C路径提取：额度耗尽→ONCE睡眠清除、普通睡眠的权重/引用回收、设置时OOM/查无TID/OFF拒绝，以及两个pthread共4,000次标签合并。锁、树操作和内核环境使用host stub，此测试验证调用时序与RMW锁范围，不证明真实rq并发或红黑树实现。
- shell/Python语法、`git diff --check`检查通过。
- 模块形态的完整离线编译、LTO、严格modpost与最终链接通过；103个强导入加module_layout共104项CRC逐项匹配该SDK的Module.symvers。没有使用缺符号警告放行、没有改vermagic、没有关闭CFI。

最终模块编译检查产物：`out/build/ux_min_20261120_07/sail_sched_assist.ko`，SHA256为`86e0bb9e40fdde24a693010806ac4f800c28f11ecf4c8e5778a6fdc1403c138a`。配置、适配清单、导入符号与CRC表随该目录保存；适配器、port源码和全部生成文件的哈希均已与当前工作区核对一致。最终host回归日志为`out/ux_validation_20261120.y2DfUu/host-tests.log`。

早期`ux_min_20261120_03`虽然SDK工具返回成功，但版本表为空，**不计为通过**；构建脚本现会逐项检查导入与CRC。严格构建使用既有修复版modpost `/home/fanziyun/mpfix/scripts/mod/modpost`（`SAILBOAT_MODPOST`），保留全部`-m/-E/-e/-i/-o`参数，未修改共享SDK或符号表。CRC核对仅针对SDK 5.15.202，不证明目标5.15.225版本匹配。

**这个ko仅用于编译检查，MODULE入口明确拒绝加载；不打包安装，不与当前内建sched_assist共存。** 交付目标是ABK源码注入；KSU loader新增已有sched_assist目录时跳过同类ko的防护，不改变其其他boost策略。

离线内建对象检查也已通过：`out/build/ux_min_builtin_20261120_03/built-in.a`，SHA256为`408c563acae1e4bad3c1ff0331ad09387cc4265402b45c0de06eedfcb8bf22b9`。10个对象均以不带MODULE的内建模式编译，包含修复后的真实初始化路径；这不是靠MODULE拒载分支才编过。复现可将`SAILBOAT_BUILTIN_CHECK=1`传给同一构建脚本；该对象仍不是可刷入boot镜像。

## 设备原样拒载测试（2026-11-20）

在`5.15.225-Sailboat-ColorOS-20261118`、Enforcing设备上执行了一次保留全部保护的普通`insmod`。设备文件SHA256与上述最终ko一致。二进制反汇编确认`init_module`仅返回`-95`（`-EOPNOTSUPP`），没有进入调度hook注册。

实际输出：`Operation not supported on transport endpoint`，`insmod_rc=1`。此结果与入口拒载保护一致，**不是模块加载成功，也不是UX运行验证**。没有删除MODULE门禁，没有强制加载或改版本校验，没有打开总开关。

测试前后均未出现外部`sail_sched_assist`，原内建SA保持`enabled=0`、独立WALT模块仍在，SELinux仍为Enforcing。对测试前后dmesg差集的模块诊断/BUG/Oops/CFI/atomic-scheduling/panic筛查无新增匹配；这不等于长期稳定性或所有内核路径已验证。临时设备ko及专用测试目录已删除。

日志：`out/ux_insmod_20261120.ewMq4D/insmod.log`，其余摘要、状态和前后dmesg同目录。第一次本地路径格式错误的推送在摘要校验阶段被拦截，未执行insmod；修正路径后才进行了上述单次加载尝试。

## 尚未证明

本轮没有证明目标5.15.225完整ABK最终树已编译、没有设备启用/调度trace、没有panic复现收口、没有24小时稳定性或性能收益数据。host shim测试不能证明rq并发或实际红黑树实现正确，后续需KUnit/受控目标验证补齐。

未经上述目标验证，不应把“离线构建成功”写成“vermeer已经运行UX加速”。
