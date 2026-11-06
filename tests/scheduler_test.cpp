#include "netforge/scheduler.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace netforge;
void require(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    Service service{"lan", address("10.77.0.100"), 9000,
        {{address("10.77.0.130"), 9000}, {address("10.77.0.131"), 9000}, {address("10.77.0.132"), 9000}}};
    std::vector<std::size_t> loads{10, 2, 2};
    for (unsigned i = 0; i < 12; ++i)
        require(select_backend(service, "key", i, loads) == i % 3, "default round robin");
    service.scheduler = Scheduler::weighted_round_robin;
    service.backends[0].weight = 3; service.backends[1].weight = 2;
    std::array<unsigned, 3> counts{};
    for (unsigned i = 0; i < 600; ++i) ++counts[*select_backend(service, "key", i, loads)];
    require(counts == std::array<unsigned,3>{300,200,100}, "exact 3:2:1 weighted cycle");
    service.backends[0].draining = true; counts = {};
    for (unsigned i = 0; i < 300; ++i) ++counts[*select_backend(service, "key", i, loads)];
    require(counts == std::array<unsigned,3>{0,200,100}, "weighted drain excludes backend");
    service.backends[0].draining = false;
    service.scheduler = Scheduler::least_connections;
    require(select_backend(service, "key", 0, loads) == 1, "minimum tracked connections");
    require(select_backend(service, "key", 2, loads) == 2, "rotating tie break");
    loads[1] = 0;
    require(select_backend(service, "key", 2, loads) == 1, "load change affects next flow");
    service.scheduler = Scheduler::rendezvous;
    auto reordered = service; std::swap(reordered.backends[0], reordered.backends[2]);
    auto removed = service; removed.backends[1].draining = true;
    counts = {};
    for (unsigned i = 0; i < 10000; ++i) {
        const auto key = "flow-" + std::to_string(i);
        const auto selected = *select_backend(service, key, i, loads);
        ++counts[selected];
        require(select_backend(service, key, 99999, loads) == selected, "hash ignores cursor");
        require(reordered.backends[*select_backend(reordered, key, i, loads)].ip == service.backends[selected].ip,
                "backend reorder preserves hash selection");
        if (selected != 1)
            require(select_backend(removed, key, i, loads) == selected, "removal moves only removed backend cohort");
    }
    for (auto count : counts) require(count > 2500 && count < 4200, "fixed hash cohort reasonable spread");
    for (auto& backend : service.backends) backend.draining = true;
    for (auto scheduler : {Scheduler::round_robin, Scheduler::weighted_round_robin,
                          Scheduler::least_connections, Scheduler::rendezvous}) {
        service.scheduler = scheduler;
        require(!select_backend(service, "key", 0, loads), "all draining fails closed for every policy");
    }
    std::cout << "PASS: weighted round robin, least connections, rendezvous stability and drain exclusion\n";
}
