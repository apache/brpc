// 授权给Apache Software Foundation (ASF)
// 或更多贡献者许可协议。请参阅通知文件
// 与这项工作一起分发以获取更多信息
// 关于版权所有权。ASF许可此文件
// 在Apache许可证版本2.0 (
// “许可证”); 除非符合要求，否则您不得使用此文件
// 有执照。您可以在以下位置获得许可证的副本
//
//   http://www.apache.org/licenses/ 许可证-2.0
//
// 除非适用法律要求或书面同意，
// 根据许可证分发的软件在
// “按现状” 为基础，不附带任何保证或条件
// 种类，无论是明示的还是暗示的。请参阅的许可证
// 管理权限和限制的特定语言
// 根据许可证。

// bthread-一个M:N线程库，使应用程序更加并发。

// 日期: 星期二7月10日17:40:58 CST 2012

#ifndef BTHREAD_TASK_GROUP_H
#define BTHREAD_TASK_GROUP_H

#include "butil/time.h"                             // Cpuwide时间ns
#include "bthread/task_control.h"
#include "bthread/task_meta.h"                     // bthread_t, TaskMeta
#include "bthread/work_stealing_queue.h"           // 工作窃取队列
#include "bthread/remote_task_queue.h"             // 远程任务尾巴
#include "butil/resource_pool.h"                    // 资源id
#include "bthread/parking_lot.h"

namespace bthread {

// 用于退出bthread。
class ExitException : public std::exception {
public:
    explicit ExitException(void* value) : _value(value) {}
    ~ExitException() throw() {}
    const char* what() const throw() override {
        return "ExitException";
    }
    void* value() const {
        return _value;
    }
private:
    void* _value;
};

// 线程本地任务组。
// 请注意，大多数涉及上下文切换的方法都是静态的，否则
// 指针 “this” 在唤醒后可能会改变。以下中的 ** pg参数
// 函数在返回之前更新。
class TaskGroup {
public:
    // 在TaskGroup * pg中创建具有属性 'attr' 的任务 'fn(arg)'，并将
    // 将标识符转换为 “tid”。切换到新任务并计划旧任务
    // 跑。
    // 成功时返回0，否则返回errno。
    static int start_foreground(TaskGroup** pg,
                                bthread_t* __restrict tid,
                                const bthread_attr_t* __restrict attr,
                                void * (*fn)(void*),
                                void* __restrict arg);

    // 在此任务组中创建属性为 “attr' 的任务 'fn(arg)'，将
    // 将标识符转换为 “tid”。安排新线程运行。
    //   从worker调用: start_background<false>
    //   从非worker调用: start_background<true>
    // 成功时返回0，否则返回errno。
    template <bool REMOTE>
    int start_background(bthread_t* __restrict tid,
                         const bthread_attr_t* __restrict attr,
                         void * (*fn)(void*),
                         void* __restrict arg);

    // 暂停调用方并在TaskGroup * pg中运行下一个bthread。
    static void sched(TaskGroup** pg);
    static void ending_sched(TaskGroup** pg);

    // 暂停调用方并在TaskGroup * pg中运行bthread 'next_tid'。
    // 此函数的目的是避免将 “next_tid” 推送到 _rq和
    // 然后被sched(pg) 弹出，这是没有必要的。
    static void sched_to(TaskGroup** pg, TaskMeta* next_meta, bool cur_ending);
    static void sched_to(TaskGroup** pg, bthread_t next_tid);
    static void exchange(TaskGroup** pg, TaskMeta* next_meta);

    // 回调将在下一个运行bthread的开始运行。
    // 不能被当前bthread直接调用，因为它经常需要
    // 已暂停的目标。
    typedef void (*RemainedFn)(void*);
    void set_remained(RemainedFn cb, void* arg) {
        _last_context_remained = cb;
        _last_context_remained_arg = arg;
    }
    
    // 将调用方挂起至少 | timeout_us | 微秒。
    // 如果 | timeout_us | 为0，则此函数不执行任何操作。
    // 如果 | group | 为NULL或当前线程为非bthread，则调用usleep(3)
    // 相反。此函数不创建线程本地TaskGroup。
    // 成功时返回0，否则返回-1，并设置errno。
    static int usleep(TaskGroup** pg, uint64_t timeout_us);

    // 挂起调用方并运行另一个bthread。当呼叫者将恢复
    // 未定义。
    static void yield(TaskGroup** pg);

    // 挂起调用方，直到bthread “tid” 终止。
    static int join(bthread_t tid, void** return_value);

    // 如果bthread 'tid' 仍然存在，则返回true。请注意，它是
    // 只是此时此刻的结果可能很快就会改变。
    // 除非必须，否则不要使用此功能。永远不要写这样的代码:
    //    if (exists(tid)) {
    //        等待线程的事件。// Racy可能会无限期阻塞。
    //    }
    static bool exists(bthread_t tid);

    // 将与 'tid' 关联的属性放入 '* attr'。
    // 成功时返回0，否则返回-1，并设置errno。
    static int get_attr(bthread_t tid, bthread_attr_t* attr);

    // 获取/设置TaskMeta.stop的tid。
    static void set_stopped(bthread_t tid);
    static bool is_stopped(bthread_t tid);

    // 运行run_main_task() 的bthread；
    bthread_t main_tid() const { return _main_tid; }
    TaskStatistics main_stat() const;
    // 应该从专用pthread调用的主要任务的例程。
    void run_main_task();

    // current_task是macOS 10.0中的函数
#ifdef current_task
#undef current_task
#endif
    // 此组中当前任务的Meta/标识符。
    TaskMeta* current_task() const { return _cur_meta; }
    bthread_t current_tid() const { return _cur_meta->tid; }
    // 当前任务的正常运行时间 (以纳秒为单位)。
    int64_t current_uptime_ns() const
    { return butil::cpuwide_time_ns() - _cur_meta->cpuwide_start_ns; }

    // True iff当前任务是运行run_main_task() 的任务
    bool is_current_main_task() const { return current_tid() == _main_tid; }
    // 如果当前任务处于pthread模式，则为True。
    bool is_current_pthread_task() const
    { return _cur_meta->stack == _main_stack; }

    // 此任务组花费的活动时间 (以纳秒为单位)。
    int64_t cumulated_cputime_ns() const { return _cumulated_cputime_ns; }

    // 将bthread推入runqueue
    void ready_to_run(TaskMeta* meta, bool nosignal = false);
    // 刷新任务已推送到rq，但已发出信号。
    void flush_nosignal_tasks();

    // 将bthread从另一个非工作线程推入runqueue。
    void ready_to_run_remote(TaskMeta* meta, bool nosignal = false);
    void flush_nosignal_tasks_remote_locked(butil::Mutex& locked_mutex);
    void flush_nosignal_tasks_remote();

    // 自动确定呼叫者是远程还是本地，并调用
    // 相应的函数。
    void ready_to_run_general(TaskMeta* meta, bool nosignal = false);
    void flush_nosignal_tasks_general();

    // 此TaskGroup所属的TaskControl。
    TaskControl* control() const { return _control; }

    // 调用此，而不是删除。
    void destroy_self();

    // 唤醒线程中的阻塞ops。
    // 成功时返回0，否则返回errno。
    static int interrupt(bthread_t tid, TaskControl* c, bthread_tag_t tag);

    // 获取与任务关联的元。
    static TaskMeta* address_meta(bthread_t tid);

    // 将任务推入 _rq，如果 _rq已满，请在一段时间后重试。这个
    // 过程无限期地进行下去。
    void push_rq(bthread_t tid);

    // 返回本地运行队列的大小。
    size_t rq_size() const {
        return _rq.volatile_size();
    }

    bthread_tag_t tag() const { return _tag; }

    pid_t tid() const { return _tid; }

    int64_t current_task_cpu_clock_ns() {
        if (_last_cpu_clock_ns == 0) {
            return 0;
        }
        int64_t total_ns = _cur_meta->stat.cpu_usage_ns;
        total_ns += butil::cputhread_time_ns() - _last_cpu_clock_ns;
        return total_ns;
    }

private:
friend class TaskControl;

    // 您应使用TaskControl::create_group创建新实例。
    explicit TaskGroup(TaskControl* c);

    int init(size_t runqueue_capacity);

    // 你应该调用destroy_selfm() 而不是析构函数，因为删除
    // 团体被推迟以避免种族。
    ~TaskGroup();

#ifdef BUTIL_USE_ASAN
    static void asan_task_runner(intptr_t);
#endif // 丁基磨损
    static void task_runner(intptr_t skip_remained);

    // Set_restaed () 的回调
    static void _release_last_context(void*);
    static void _add_sleep_event(void*);
    struct ReadyToRunArgs {
        bthread_tag_t tag;
        TaskMeta* meta;
        bool nosignal;
    };
    static void ready_to_run_in_worker(void*);
    static void ready_to_run_in_worker_ignoresignal(void*);
    static void priority_to_run(void*);

    // 等待任务运行。
    // 成功时返回true，false被视为永久错误，
    // 循环调用此函数应结束。
    bool wait_task(bthread_t* tid);

    bool steal_task(bthread_t* tid) {
        if (_remote_rq.pop(tid)) {
            return true;
        }
#ifndef BTHREAD_DONT_SAVE_PARKING_STATE
        _last_pl_state = _pl->get_state();
#endif
        return _control->steal_task(tid, &_steal_seed, _steal_offset);
    }

    void set_tag(bthread_tag_t tag) { _tag = tag; }

    void set_pl(ParkingLot* pl) { _pl = pl; }

    TaskMeta* _cur_meta;
    
    // 此组所属的控件
    TaskControl* _control;
    int _num_nosignal;
    int _nsignaled;
    // 充电调度时间
    int64_t _last_run_ns;
    int64_t _cumulated_cputime_ns;
    // 最后一个线程cpu时钟
    int64_t _last_cpu_clock_ns;

    size_t _nswitch;
    RemainedFn _last_context_remained;
    void* _last_context_remained_arg;

    ParkingLot* _pl;
#ifndef BTHREAD_DONT_SAVE_PARKING_STATE
    ParkingLot::State _last_pl_state;
#endif
    size_t _steal_seed;
    size_t _steal_offset;
    ContextualStack* _main_stack;
    bthread_t _main_tid;
    WorkStealingQueue<bthread_t> _rq;
    RemoteTaskQueue _remote_rq;
    int _remote_num_nosignal;
    int _remote_nsignaled;

    int _sched_recursive_guard;
    // 此任务组的标记
    bthread_tag_t _tag;

    // 工作线程id。
    pid_t _tid;
};

}  // 命名空间bthread

#include "task_group_inl.h"

#endif  // Bthread任务组h
