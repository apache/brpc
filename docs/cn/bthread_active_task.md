# bthread Active Task（实验性/UNSTABLE）

本文介绍当前 brpc 中新增的 **Active Task** 基础设施，以及在服务端请求处理中配合 `butex` 实现：

- 请求处理 bthread 挂起等待（例如等待 io_uring completion）
- 在 bthread worker 的 active-task hook 中收割 completion
- 在 hook 内把 waiter 恢复到当前 worker 的 **local runqueue**（不走 `_remote_rq`）

本文描述的是 **当前实现** 的使用方式与边界，接口位于 `bthread/unstable.h`，属于 UNSTABLE API。

## 适用场景

典型场景是“每个 bthread worker 一个本地 reactor”（例如每 worker 一个 io_uring ring）：

1. worker 初始化时创建本地 reactor/ring。
2. 提交异步 IO 后，在私有 `butex` 上通过 `bthread_butex_wait_local` 挂起。
4. worker 的 active-task hook 收割 completion。
5. hook 内调用 `bthread_butex_wake_within(ctx, req->butex)` 唤醒 waiter。
6. waiter bthread 在同一个 worker 上恢复执行（不会被 steal）。

## 当前提供的接口（UNSTABLE）

头文件：`src/bthread/unstable.h`

- `bthread_register_active_task_type(...)`
- `bthread_butex_wake_within(...)`
- `bthread_butex_wait_local(...)`

相关类型：

- `bthread_active_task_ctx_t`
- `bthread_active_task_type_t`

### 关键限制（当前实现）

- Active-task callback 只允许做**短小、非阻塞**的维护逻辑（如收割 completion + 唤醒 waiter）。
- **不支持**在 active-task hook 中创建新的 bthread。
- `harvest` 返回值语义：`0` 表示正常；`1` 表示本轮 worker loop 跳过一次 `ParkingLot::wait`（立即重试）。
- `bthread_butex_wake_within(ctx, butex)` 只允许在 active-task `harvest` 回调中调用。
- `bthread_butex_wait_local(...)` 内部会对本次 wait 启用 **隐式 wait-scope 本地化 pin**：
  - 从进入 wait 到返回（成功/超时/中断）这一段，恢复会被路由回 home worker
  - 恢复前不会被 steal
  - 返回后 task 恢复默认调度行为（后续 `yield` 仍可能迁移）
- `bthread_butex_wake_within` 只适用于“每请求私有 butex（单 waiter）”模型：
  - 0 waiter -> 返回 `0`
  - 1 waiter（同 `TaskControl`、同 tag 的 bthread waiter）-> 返回 `1`
  - 当前 worker 的 pinned local runqueue 已满 -> 返回 `-1` 且 `errno=EAGAIN`（应在下一轮 `harvest` 重试）
  - 否则返回 `-1` 且 `errno=EINVAL`（多 waiter / pthread waiter / 跨 tag / 跨 `TaskControl`）

## 快速上手（推荐接入顺序）

如果你只是要把“本地 IO completion -> 唤醒等待中的 bthread”跑通，按下面顺序做：

1. 进程启动早期注册 active-task（在任何 bthread/brpc 初始化前）。
2. 在 `worker_init` 里初始化每 worker 本地 reactor/ring。
3. 请求对象里放一个私有 `butex`（每请求一个）。
4. 请求 bthread 提交异步 IO 后调用 `bthread_butex_wait_local(...)`。
5. 在 active-task `harvest` 里收割 completion，找到 `ReqCtx*`，然后调用 `bthread_butex_wake_within(ctx, req->done_butex)`。
6. waiter 从 `bthread_butex_wait_local(...)` 返回后继续处理结果。

建议先在单 worker 环境验证，再切到多 worker。

## 一个最小使用流程（服务端请求处理）

下面用“异步 IO + butex 挂起/唤醒”的模式说明。

### 1. 定义每 worker 的本地状态（例如 ring）

`worker_init/worker_destroy` 用于每个 worker 上的初始化/销毁。

```cpp
struct WorkerIoState {
    // 示例：io_uring ring / 本地 reactor / 统计等
    // io_uring ring;
};

static thread_local WorkerIoState* tls_worker_io = NULL;

static int IoWorkerInit(void** worker_local,
                        const bthread_active_task_ctx_t* ctx,
                        void* user_data) {
    (void)ctx;
    (void)user_data;
    WorkerIoState* s = new WorkerIoState;
    // 初始化 ring/reactor ...
    tls_worker_io = s;
    *worker_local = s;
    return 0;
}

static void IoWorkerDestroy(void* worker_local,
                            const bthread_active_task_ctx_t* ctx,
                            void* user_data) {
    (void)ctx;
    (void)user_data;
    WorkerIoState* s = static_cast<WorkerIoState*>(worker_local);
    if (tls_worker_io == s) {
        tls_worker_io = NULL;
    }
    // 销毁 ring/reactor ...
    delete s;
}
```

### 2. 启动前注册 active-task 类型（必须在 bthread/brpc 初始化前）

必须在任何 bthread/Server 启动前完成注册（例如 `main()` 早期）。

```cpp
static int IoHarvest(
    void* worker_local, const bthread_active_task_ctx_t* ctx);

void RegisterIoActiveTask() {
    bthread_active_task_type_t t;
    memset(&t, 0, sizeof(t));
    t.struct_size = sizeof(t);
    t.name = "io_active_task";
    t.worker_init = IoWorkerInit;
    t.worker_destroy = IoWorkerDestroy;
    t.harvest = IoHarvest;
    const int rc = bthread_register_active_task_type(&t);
    CHECK_EQ(0, rc);
}
```

## 3. 请求对象里携带私有 butex（关键）

当前框架**不会自动传递 butex** 到 active-task hook。

正确做法是：把 `butex` 放进请求对象里，并通过异步 completion 的 `user_data` 找回请求对象。

```cpp
struct ReqCtx {
    void* done_butex;                  // 每请求私有 butex
    std::atomic<int> done;             // 0 -> 未完成, 1 -> 完成
    int result;
    // 其他字段：fd/buffer/offset/cancel 等
};
```

请求等待本地 IO completion 时（推荐用法）：

```cpp
ReqCtx req;
req.done_butex = bthread::butex_create();
static_cast<butil::atomic<int>*>(req.done_butex)->store(0, butil::memory_order_relaxed);
req.done.store(0, std::memory_order_relaxed);

// 提交异步 IO，把 &req 作为 completion 的 user_data（例如 io_uring cqe->user_data）
SubmitAsyncIo(&req);

// 挂起当前 bthread，等待 active-task hook 唤醒。
// bthread_butex_wait_local 内部会对本次 wait 启用 wait-scope 本地化 pin，
// 保证恢复在同一个 worker 上，且恢复前不会被 steal。
int rc = bthread_butex_wait_local(req.done_butex, 0, NULL);
if (rc != 0 && errno != EWOULDBLOCK) {
    // 处理错误/中断/超时（如果设置了超时）
}

// 被唤醒后继续执行（返回点在同一个 worker 上）
UseResult(req.result);
bthread::butex_destroy(req.done_butex);
```

## 4. 在 active-task `harvest` 回调中收割 completion 并唤醒 waiter

核心点：

- 在 hook 中通过 completion 找回 `ReqCtx*`
- 写结果
- 调 `bthread_butex_wake_within(ctx, req->done_butex)`，显式本地唤醒

```cpp
static bool HarvestCompletions(
    void* worker_local, const bthread_active_task_ctx_t* ctx) {
    WorkerIoState* s = static_cast<WorkerIoState*>(worker_local);
    (void)s;

    bool made_progress = false;

    // 伪代码：循环收割 completion
    for (;;) {
        ReqCtx* req = TryPopOneCompletion();  // 例如从 io_uring CQ 取 cqe->user_data
        if (req == NULL) {
            break;
        }

        // “做点什么事”：写 completion 结果
        req->result = 123;  // 示例
        req->done.store(1, std::memory_order_release);

        errno = 0;
        const int wake_rc = bthread_butex_wake_within(ctx, req->done_butex);
        if (wake_rc == 1) {
            made_progress = true;
        } else if (wake_rc == 0) {
            // 没有 waiter（可能已超时/取消），按需处理
        } else {
            // EINVAL/EPERM 代表用法或上下文不满足要求，应该报警
        }
    }
    return made_progress;
}

static int IoHarvest(
    void* worker_local, const bthread_active_task_ctx_t* ctx) {
    const bool made_progress = HarvestCompletions(worker_local, ctx);
    return made_progress ? 1 : 0;  // 1 == skip current park once
}
```

### `bthread_butex_wake_within(...)` 返回值与错误码（实用）

- 返回 `1`：成功唤醒了 1 个 waiter（这是常见主路径）
- 返回 `0`：当前 butex 上没有 waiter（例如 timeout/取消竞争后已无人等待）
- 返回 `-1`：
  - `EPERM`：不在 active-task `harvest` 回调里调用
  - `EAGAIN`：当前 worker 的 pinned local runqueue 满，当前轮无法安全入队，应下一轮重试
  - `EINVAL`：butex 不满足 within 语义（多 waiter / pthread waiter / 跨 tag / 跨 `TaskControl`），或 wrong-worker invariant 被触发

建议：

- `wake_rc == 0` 当作**合法分支**处理（不是异常）
- `wake_rc < 0 && errno == EAGAIN` 视为**背压信号**，本轮放弃，下一轮 `harvest` 重试
- 其他 `wake_rc < 0` 视为**用法/所有权错误**并记录错误日志或计数

## 调用时机与可调间隔（busy/idle）

Active-task `harvest` 回调会在 worker 调度循环的多个内部时机被调用（实现细节），你可以把业务逻辑统一写在一个 `HarvestCompletions()` 中。

当前实现有两个关键 gflag（都可调）：

- gflag: `bthread_active_task_poll_every_nswitch`
- 默认值：`1`
- 含义：每 N 次 bthread 切换，在 worker 主循环中额外执行一次 `harvest`（busy worker 场景）

- gflag: `bthread_active_task_idle_wait_ns`
- 默认值：`1000000`（1ms）
- 含义：当 worker 没有 runnable task 时，进入 `ParkingLot::wait()` 的空闲等待间隔
  - `<0`：无限等待（仅靠 signal/stop 唤醒）
  - `=0`：不进入 park（空转）
  - `>0`：按该间隔超时醒来，再继续循环并执行 `harvest`

推荐写法（与你的场景一致）：

- 把业务逻辑统一写在 `HarvestCompletions()`
- 用 `bthread_active_task_poll_every_nswitch=1` 降低 busy worker 的 completion 延迟
- 用 `bthread_active_task_idle_wait_ns` 控制 idle 场景的轮询间隔（CPU/延迟折中）

说明：

- 长时间不 `yield`/阻塞的 bthread 会阻塞 worker 主循环，这种情况下无论 `poll_every_nswitch=1` 还是更大值都无解（协作式调度边界）
- `poll_every_nswitch=1` 优化的是“busy 且有切换”的 worker 场景

## `bthread_butex_wait_local(...)` 的本地化保证范围（wait-scope）

`bthread_butex_wait_local(...)` 会在 runtime 内部对“这一次 wait”启用隐式本地化 pin。

在该 wait 生命周期内，当前 task 的恢复路径会被 runtime 路由回 `home worker`，并进入非 steal 队列：

- active-task `bthread_butex_wake_within(...)`
- timeout（TimerThread）
- interruption（`bthread_interrupt` / `bthread_stop`）
- `bthread_butex_wake_within(...)` 是外部允许的唯一正常唤醒路径（strict 模式）
- timeout / interruption 是 runtime 内部路径

strict 模式下，普通 `butex_wake*` 命中 pinned waiter 会返回 `-1/EINVAL`（不做隐式回退）。

因此在这次 `bthread_butex_wait_local(...)` 调用的生命周期内：

- 不会迁移到其他 worker
- 不会通过普通 `_rq/_remote_rq` 暴露给 steal

说明：

- `bthread_butex_wake_within(ctx, butex)` 仍会做 `home TaskGroup == 当前 hook TaskGroup` 的 invariant 校验；
  正常生产路径不应触发错误。
- `bthread_butex_wait_local(...)` 返回后，task 会恢复默认调度行为（后续 `yield`/调度仍可能被 steal）。

## 非保证范围（避免误解）

下面这些**不在** `bthread_butex_wait_local(...)` 的 wait-scope 本地化保证范围内：

- `bthread_butex_wait_local(...)` 返回之后的后续调度（例如后面的 `yield`）
- 整个请求处理阶段都固定在同一 worker（本文档当前方案不保证）
- completion “一定会被正确 worker 的 `harvest` 收到”这件事本身

最后一条尤其重要：runtime 会在 `bthread_butex_wake_within(...)` 做 wrong-worker invariant 校验，但这只是第二道防线。业务侧（例如 per-worker io_uring）仍要保证 completion ownership 正确。

## 调试与测试建议

- 先用单 worker 环境验证链路（最容易确认“wait -> harvest -> wake -> resume”）
- 再开启多 worker 验证吞吐和尾延迟
- 在测试里显式记录：
  - waiter 挂起前的 worker 线程
  - hook 执行线程
  - waiter 恢复后的线程
- 如果出现 `EINVAL`：
  - 检查是否误用了多 waiter butex
  - 检查是否在 pthread waiter 上使用了 within wake
  - 检查 tag / `TaskControl` 是否匹配
  - 检查是否对 `bthread_butex_wait_local` 的 waiter 误用了普通 `butex_wake*`
  - 检查 completion 是否被错误 worker 的 `harvest` 收割（ownership/routing 问题）

可观测计数（累计 bvar）：

- `bthread_butex_strict_reject_count`：普通 `butex_wake*` 命中 pinned waiter 被 strict 拒绝次数
- `bthread_butex_within_no_waiter_count`：`bthread_butex_wake_within` 返回 `0`（无 waiter）次数
- `bthread_butex_within_invalid_count`：`bthread_butex_wake_within` 返回 `-1/EINVAL` 次数

## 注意事项（务必遵守）

- `bthread_register_active_task_type()` 必须在 bthread/brpc 初始化前调用
- active-task hook 内不要阻塞，不要调用会导致调度切换的 bthread API
- 每请求使用私有 butex（不要让多个 waiter 复用同一个 butex）
- `bthread_butex_wait_local(...)` 的本地化保证范围是“本次 wait 生命周期”，不是整个请求处理阶段
- 请求对象生命周期必须覆盖：
  - 提交异步 IO
  - completion 收割
  - waiter 恢复读取结果
- 在 completion 可能与超时/取消竞争的场景，按业务需要处理 `wake_rc == 0`（表示此时 butex 上没有 waiter）
- 建议把 butex 值本身作为完成状态位（先写结果/状态，再 wake），避免丢唤醒
