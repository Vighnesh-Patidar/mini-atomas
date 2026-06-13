#include "mith/core/scheduler.h"

#include "mith/core/trace_sink.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <queue>
#include <thread>
#include <utility>

namespace mith {

namespace detail {

// Minimal FIFO thread pool. Internal to scheduler.cpp.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_workers) {
        if (num_workers == 0) num_workers = 1;
        workers_.reserve(num_workers);
        for (std::size_t i = 0; i < num_workers; ++i) {
            workers_.emplace_back([this]() { worker_loop_(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    void submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void worker_loop_() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this]() { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mtx_;
    std::condition_variable           cv_;
    bool                              stop_ = false;
};

// Two SystemDescriptors conflict (must serialise) iff they share any
// component or resource with at least one writer.
static bool have_hazard(const SystemDescriptor& a, const SystemDescriptor& b) noexcept {
    auto intersects = [](const auto& va, const auto& vb) {
        for (const auto& x : va) {
            for (const auto& y : vb) {
                if (x == y) return true;
            }
        }
        return false;
    };
    if (intersects(a.writes_components, b.reads_components))  return true;
    if (intersects(a.reads_components,  b.writes_components)) return true;
    if (intersects(a.writes_components, b.writes_components)) return true;
    if (intersects(a.writes_resources,  b.reads_resources))   return true;
    if (intersects(a.reads_resources,   b.writes_resources))  return true;
    if (intersects(a.writes_resources,  b.writes_resources))  return true;
    return false;
}

static std::size_t resolve_pool_size(std::size_t requested) noexcept {
    if (requested > 0) return requested;
    const auto hc = std::thread::hardware_concurrency();
    if (hc <= 1) return 1;
    return static_cast<std::size_t>(hc - 1);
}

} // namespace detail


namespace detail {

[[noreturn]] void scheduler_assert_fail(const char* msg) noexcept {
    // §15 prohibits exceptions; terminate with a structured stderr line that
    // EW deployments (§13.5) and CI can capture.
    std::fprintf(stderr, "mith::SystemScheduler assertion failed: %s\n", msg);
    std::abort();
}

} // namespace detail

SystemScheduler::SystemScheduler(SchedulerMode mode,
                                  std::size_t   thread_pool_size) noexcept
    : mode_(mode)
    , thread_pool_size_(thread_pool_size) {}

SystemScheduler::~SystemScheduler() = default;

SchedulerStatus SystemScheduler::register_system(std::unique_ptr<System> system) {
    if (!system) {
        detail::scheduler_assert_fail("register_system: null system pointer");
    }

    const auto& name = system->describe().name;
    for (const auto& existing : systems_) {
        if (existing->describe().name == name) {
            return SchedulerStatus::DuplicateName;
        }
    }

    // Registering after build_graph() invalidates the resolved order.
    // The caller must rebuild before tick() — tick() aborts on !built_.
    built_ = false;

    systems_.push_back(std::move(system));
    return SchedulerStatus::Ok;
}

SchedulerStatus SystemScheduler::build_graph() {
    if (built_) return SchedulerStatus::AlreadyBuilt;

    // Lexicographic sort on name (§5.2 stable tie-breaking). Used directly
    // in Sequential, and as the deterministic edge-direction rule in
    // Parallel (for any conflicting pair, the lex-smaller name runs first).
    order_.resize(systems_.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    std::sort(order_.begin(), order_.end(),
              [this](std::size_t a, std::size_t b) {
                  return systems_[a]->describe().name < systems_[b]->describe().name;
              });

    if (mode_ == SchedulerMode::Parallel) {
        build_hazard_graph_();
        if (!pool_) {
            pool_ = std::make_unique<detail::ThreadPool>(
                detail::resolve_pool_size(thread_pool_size_));
        }
    }

    built_ = true;
    return SchedulerStatus::Ok;
}

void SystemScheduler::build_hazard_graph_() {
    const std::size_t N = systems_.size();
    dependents_.assign(N, {});
    in_degree_initial_.assign(N, 0);

    // Iterate in name order (order_) so edges always point from lex-smaller
    // name to lex-larger — gives a deterministic, conflict-respecting topo
    // order across runs.
    for (std::size_t pa = 0; pa < N; ++pa) {
        const std::size_t i = order_[pa];
        const auto& desc_i = systems_[i]->describe();
        for (std::size_t pb = pa + 1; pb < N; ++pb) {
            const std::size_t j = order_[pb];
            if (detail::have_hazard(desc_i, systems_[j]->describe())) {
                dependents_[i].push_back(j);
                ++in_degree_initial_[j];
            }
        }
    }
}

void SystemScheduler::tick(EntityRegistry& registry,
                           const SwarmContext& ctx,
                           float delta_time) {
    if (!built_) {
        detail::scheduler_assert_fail(
            "tick(): build_graph() not called since last register_system()");
    }

    if (mode_ == SchedulerMode::Sequential) {
        for (const std::size_t idx : order_) {
            systems_[idx]->tick(registry, ctx, delta_time);
        }
        emit_tick_event_(ctx, delta_time);
        return;
    }

    // Parallel mode.
    tick_parallel_(registry, ctx, delta_time);
    emit_tick_event_(ctx, delta_time);
}

void SystemScheduler::tick_parallel_(EntityRegistry& registry,
                                      const SwarmContext& ctx,
                                      float delta_time) {
    const std::size_t N = systems_.size();
    if (N == 0) return;

    // Per-tick in-degree counters. Atomics so workers can decrement
    // concurrently. Heap-allocated because std::vector<std::atomic<int>> is
    // not movable.
    auto in_degree = std::make_unique<std::atomic<int>[]>(N);
    for (std::size_t i = 0; i < N; ++i) {
        in_degree[i].store(in_degree_initial_[i], std::memory_order_relaxed);
    }

    std::atomic<std::size_t> completed{0};
    std::mutex              done_mtx;
    std::condition_variable done_cv;

    // Recursive dispatch: a system finishes → drops in-degree of each
    // dependent; any dependent that hit zero is submitted immediately.
    std::function<void(std::size_t)> dispatch;
    dispatch = [&](std::size_t i) {
        pool_->submit([&, i]() {
            systems_[i]->tick(registry, ctx, delta_time);

            for (const std::size_t dep : dependents_[i]) {
                if (in_degree[dep].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    dispatch(dep);
                }
            }

            if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 == N) {
                std::lock_guard<std::mutex> lk(done_mtx);
                done_cv.notify_one();
            }
        });
    };

    // Seed: every system with no dependencies fires immediately.
    for (std::size_t i = 0; i < N; ++i) {
        if (in_degree[i].load(std::memory_order_relaxed) == 0) {
            dispatch(i);
        }
    }

    // Wait for all systems in this tick to finish.
    std::unique_lock<std::mutex> lk(done_mtx);
    done_cv.wait(lk, [&]() {
        return completed.load(std::memory_order_acquire) == N;
    });
}

SchedulerMode SystemScheduler::mode() const noexcept       { return mode_; }
std::size_t   SystemScheduler::system_count() const noexcept { return systems_.size(); }
bool          SystemScheduler::is_built() const noexcept    { return built_; }

void SystemScheduler::set_trace_sink(TraceSink* sink) noexcept {
    sink_ = sink;
}

TraceSink* SystemScheduler::trace_sink() const noexcept {
    return sink_;
}

void SystemScheduler::emit_tick_event_(const SwarmContext& ctx,
                                        float delta_time) noexcept {
    if (!sink_) return;

    const TraceField fields[] = {
        TraceField::u64("tick",         static_cast<std::uint64_t>(ctx.tick_count)),
        TraceField::f64("delta_time_s", static_cast<double>(delta_time)),
        TraceField::u64("system_count", static_cast<std::uint64_t>(systems_.size())),
        TraceField::u64("swarm_id",     static_cast<std::uint64_t>(ctx.swarm_id)),
    };
    sink_->emit(TraceLevel::Info, "tick_completed",
                fields, sizeof(fields) / sizeof(fields[0]));
}

} // namespace mith
