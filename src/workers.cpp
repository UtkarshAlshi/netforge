#include "netforge/workers.hpp"
#include <stdexcept>

namespace netforge {
namespace {
unsigned read16(std::span<const std::uint8_t> frame, std::size_t offset) {
    return (unsigned(frame[offset]) << 8) | frame[offset + 1];
}
Address read32(std::span<const std::uint8_t> frame, std::size_t offset) {
    return (Address(frame[offset]) << 24) | (Address(frame[offset + 1]) << 16) |
           (Address(frame[offset + 2]) << 8) | frame[offset + 3];
}
}
static std::vector<Config> worker_shards(const Config& config, std::size_t count, std::size_t queue_capacity) {
    validate(config);
    std::vector<Config> result;
    if (count < 1 || count > 2 || !queue_capacity || queue_capacity > 64 || config.ports.size() != 2 ||
        !config.snat || config.neighbor_discovery ||
        config.flow_limit < count || unsigned(config.snat->last - config.snat->first + 1) < count)
        throw std::runtime_error("workers require 1..2 workers, bounded queues and two-port static SNAT config");
    for (const auto& rule : config.rules)
        if (rule.protocol != 17 && rule.protocol != 6) throw std::runtime_error("worker adapter accepts UDP/TCP ACLs only");
    for (const auto& service : config.services) {
        if (service.ingress == config.snat->port || service.scheduler == Scheduler::least_connections)
            throw std::runtime_error("worker services require LAN ingress and do not support global least-connections");
        for (const auto& backend : service.backends) {
            const auto* route = lookup_route(config, backend.ip);
            if (!route || route->port != config.snat->port)
                throw std::runtime_error("worker service backends must route through the SNAT port");
        }
    }
    unsigned ports = config.snat->last - config.snat->first + 1;
    for (std::size_t index = 0; index < count; ++index) {
        Config shard = config;
        shard.snat->first = static_cast<std::uint16_t>(config.snat->first + ports * index / count);
        shard.snat->last = static_cast<std::uint16_t>(config.snat->first + ports * (index + 1) / count - 1);
        shard.flow_limit = config.flow_limit / count + (index < config.flow_limit % count ? 1 : 0);
        result.push_back(std::move(shard));
    }
    return result;
}
WorkerPool::WorkerPool(Config config, std::size_t count, std::size_t queue_capacity)
    : config_(std::move(config)), capacity_(queue_capacity) {
    for (auto& shard : worker_shards(config_, count, queue_capacity))
        workers_.push_back(std::make_unique<Worker>(std::move(shard)));
    try {
        for (std::size_t index = 0; index < count; ++index)
            workers_[index]->thread = std::thread([this, index] { run(index); });
    } catch (...) { shutdown(); throw; }
}
WorkerPool::~WorkerPool() { shutdown(); }
const Config& WorkerPool::worker_config(std::size_t owner) const { return workers_.at(owner)->config; }

std::size_t WorkerPool::select(const WorkerPacket& packet) const {
    if (!find_port(config_, packet.ingress) || packet.frame.size() > 9014)
        throw std::runtime_error("worker_input_invalid");
    auto ethernet = parse_ethernet(packet.frame);
    if (!std::holds_alternative<EthernetHeader>(ethernet)) return 0;
    if (std::get<EthernetHeader>(ethernet).type_or_length == 0x0806) return 0;
    if (std::get<EthernetHeader>(ethernet).type_or_length != 0x0800)
        throw std::runtime_error("worker_protocol_unsupported");
    auto parsed = parse_ipv4(std::span(packet.frame).subspan(14));
    if (!std::holds_alternative<IPv4Header>(parsed)) return 0; // Engine rejects malformed input.
    const auto& ip = std::get<IPv4Header>(parsed);
    if (ip.header_length != 20 || ip.fragment_offset || ip.more_fragments)
        throw std::runtime_error("worker_protocol_unsupported");
    if (ip.protocol == 1) {
        // Related return errors quote the NAT source port, not the outer tuple.
        if (packet.ingress == config_.snat->port && read32(packet.frame, 30) == config_.snat->ip &&
            ip.total_length >= 56 && (packet.frame[34] == 3 || packet.frame[34] == 11) &&
            packet.frame[42] == 0x45 && (packet.frame[51] == 6 || packet.frame[51] == 17) &&
            read32(packet.frame, 54) == config_.snat->ip) {
            unsigned port = read16(packet.frame, 62);
            for (std::size_t index = 0; index < workers_.size(); ++index)
                if (port >= workers_[index]->config.snat->first && port <= workers_[index]->config.snat->last) return index;
        }
        return 0; // Stateless ICMP and invalid quotes are validated by engine zero.
    }
    if (ip.protocol != 17 && ip.protocol != 6) throw std::runtime_error("worker_protocol_unsupported");
    if (ip.total_length < 28) return 0;
    unsigned destination_port = read16(packet.frame, 36);
    if (packet.ingress == config_.snat->port) {
        if (read32(packet.frame, 30) != config_.snat->ip)
            throw std::runtime_error("worker_return_unowned");
        for (std::size_t index = 0; index < workers_.size(); ++index)
            if (destination_port >= workers_[index]->config.snat->first && destination_port <= workers_[index]->config.snat->last)
                return index;
        throw std::runtime_error("worker_return_unowned");
    }
    // Stable original tuple hash; translated return tuples use disjoint PAT ranges.
    std::uint64_t hash = 1469598103934665603ULL;
    hash ^= ip.protocol; hash *= 1099511628211ULL;
    for (std::size_t offset = 26; offset < 38; ++offset) { hash ^= packet.frame[offset]; hash *= 1099511628211ULL; }
    return hash % workers_.size();
}

std::vector<std::future<WorkerOutcome>> WorkerPool::submit_batch(std::vector<WorkerPacket> packets) {
    if (packets.size() > 64) throw std::runtime_error("worker batch exceeds 64 packets");
    std::lock_guard admission(admission_);
    // Lock all queues so a submitted batch has deterministic admission bounds.
    std::vector<std::unique_lock<std::mutex>> locks;
    for (auto& worker : workers_) locks.emplace_back(worker->mutex);
    std::vector<std::future<WorkerOutcome>> results; results.reserve(packets.size());
    for (auto& packet : packets) {
        Job job{std::move(packet), {}, false, {}, {}, {}}; results.push_back(job.completion.get_future());
        std::size_t owner = 0;
        try {
            if (closed_) throw std::runtime_error("worker_pool_closed");
            owner = select(job.packet);
            if (workers_[owner]->queue.size() >= capacity_) throw std::runtime_error("worker_queue_full");
            workers_[owner]->queue.push_back(std::move(job));
        } catch (const std::exception& error) {
            job.completion.set_value({owner, Decision{error.what(), {}, {}}});
        }
    }
    locks.clear();
    for (auto& worker : workers_) worker->ready.notify_one();
    return results;
}
void WorkerPool::run(std::size_t owner) {
    auto& worker = *workers_[owner];
    for (;;) {
        std::unique_lock lock(worker.mutex);
        worker.ready.wait(lock, [&] { return worker.stopping || !worker.queue.empty(); });
        if (worker.queue.empty() && worker.stopping) return;
        Job job = std::move(worker.queue.front()); worker.queue.pop_front(); lock.unlock();
        try {
            if (job.control) {
                job.control(); job.controlled.set_value();
            } else if (job.tick) {
                auto decisions = worker.engine->maintenance(job.packet.now_ms);
                job.maintained.set_value({std::move(decisions), worker.engine->flow_count(), worker.engine->counters().expired_flows});
            } else job.completion.set_value({owner, worker.engine->process(job.packet.ingress, job.packet.frame, job.packet.now_ms)});
        } catch (...) {
            if (job.control) job.controlled.set_exception(std::current_exception());
            else if (job.tick) job.maintained.set_exception(std::current_exception());
            else job.completion.set_exception(std::current_exception());
        }
    }
}
std::vector<WorkerMaintenance> WorkerPool::maintenance(std::uint64_t now_ms) {
    std::lock_guard admission(admission_);
    if (closed_) throw std::runtime_error("worker_pool_closed");
    std::vector<std::unique_lock<std::mutex>> locks;
    for (auto& worker : workers_) locks.emplace_back(worker->mutex);
    for (auto& worker : workers_)
        if (worker->queue.size() >= capacity_) throw std::runtime_error("worker_queue_full");
    // Prepare promises before queue admission. Partial admission on allocation failure
    // is harmless maintenance, but admitted owners must still be notified.
    std::vector<Job> jobs(workers_.size());
    std::vector<std::future<WorkerMaintenance>> futures;
    futures.reserve(workers_.size());
    for (auto& job : jobs) {
        job.tick = true; job.packet.now_ms = now_ms;
        futures.push_back(job.maintained.get_future());
    }
    try {
        for (std::size_t i = 0; i < workers_.size(); ++i) workers_[i]->queue.push_back(std::move(jobs[i]));
    } catch (...) {
        locks.clear();
        for (auto& worker : workers_) worker->ready.notify_one();
        throw;
    }
    locks.clear();
    for (auto& worker : workers_) worker->ready.notify_one();
    std::vector<WorkerMaintenance> result; result.reserve(workers_.size());
    for (auto& future : futures) result.push_back(future.get());
    return result;
}
void WorkerPool::reload(Config config) {
    auto shards = worker_shards(config, workers_.size(), capacity_);
    std::lock_guard admission(admission_);
    if (closed_) throw std::runtime_error("worker_pool_closed");
    if (config.snat->port != config_.snat->port || config.snat->ip != config_.snat->ip ||
        config.snat->first != config_.snat->first || config.snat->last != config_.snat->last)
        throw std::runtime_error("restart required for worker PAT ownership changes");
    std::vector<std::unique_ptr<Appliance>> staged(workers_.size());
    // FIFO control jobs drain admitted packets before copying each owner. Holding
    // admission prevents packets/maintenance/shutdown from entering during staging.
    // Sequential staging bounds control jobs to one and safely unwinds on rejection.
    for (std::size_t i = 0; i < workers_.size(); ++i) {
        auto& worker = *workers_[i];
        Job job{};
        job.control = [&, i] {
            staged[i] = std::make_unique<Appliance>(*workers_[i]->engine);
            staged[i]->reload(shards[i]);
        };
        auto completed = job.controlled.get_future();
        {
            std::unique_lock lock(worker.mutex);
            // A full queue is still bounded; reject before enqueueing a control job.
            if (worker.queue.size() >= capacity_) throw std::runtime_error("worker_queue_full");
            worker.queue.push_back(std::move(job));
        }
        worker.ready.notify_one(); completed.get();
    }
    // All owners have completed their last engine call and no admissions can race.
    // Lock their queues and perform only noexcept swaps; no partial publication.
    std::vector<std::unique_lock<std::mutex>> locks;
    locks.reserve(workers_.size());
    for (auto& worker : workers_) locks.emplace_back(worker->mutex);
    static_assert(std::is_nothrow_swappable_v<Config>);
    for (std::size_t i = 0; i < workers_.size(); ++i) {
        workers_[i]->engine.swap(staged[i]);
        std::swap(workers_[i]->config, shards[i]);
    }
    std::swap(config_, config);
}
void WorkerPool::shutdown() {
    std::lock_guard admission(admission_);
    if (closed_) return;
    closed_ = true;
    for (auto& worker : workers_) {
        { std::lock_guard lock(worker->mutex); worker->stopping = true; }
        worker->ready.notify_one();
    }
    for (auto& worker : workers_) if (worker->thread.joinable()) worker->thread.join();
}
} // namespace netforge
