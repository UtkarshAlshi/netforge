#pragma once
#include "netforge/appliance.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace netforge {
struct WorkerPacket { std::string ingress; std::vector<std::uint8_t> frame; std::uint64_t now_ms; };
struct WorkerOutcome { std::size_t owner; Decision decision; };
struct WorkerMaintenance {
    std::vector<Decision> decisions;
    std::size_t flows;
    std::uint64_t expired;
};

// Bounded UDP/TCP/SNAT worker adapter. Each engine is touched by one thread.
// Owner-local engine calls, bounded AF_PACKET dispatch, maintenance and staged reload.
class WorkerPool {
public:
    WorkerPool(Config config, std::size_t count, std::size_t queue_capacity = 32);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    std::vector<std::future<WorkerOutcome>> submit_batch(std::vector<WorkerPacket> packets);
    // FIFO barrier on each owner; only owner threads touch engine state.
    std::vector<WorkerMaintenance> maintenance(std::uint64_t now_ms);
    void reload(Config config); // Quiesce admissions; stage all owners before no-throw commit.
    void shutdown(); // Close admissions, drain admitted work, join owned threads.
    const Config& worker_config(std::size_t owner) const;
private:
    struct Job {
        WorkerPacket packet; std::promise<WorkerOutcome> completion;
        bool tick = false; std::promise<WorkerMaintenance> maintained;
        std::function<void()> control; std::promise<void> controlled;
    };
    struct Worker {
        explicit Worker(Config value) : config(std::move(value)), engine(std::make_unique<Appliance>(config)) {}
        Config config; std::unique_ptr<Appliance> engine;
        std::mutex mutex; std::condition_variable ready;
        std::deque<Job> queue; bool stopping = false; std::thread thread;
    };
    std::size_t select(const WorkerPacket& packet) const;
    void run(std::size_t owner);
    Config config_; std::size_t capacity_; bool closed_ = false;
    std::mutex admission_;
    std::vector<std::unique_ptr<Worker>> workers_;
};
} // namespace netforge
