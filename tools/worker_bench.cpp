// Bounded offline worker-dispatch experiment; no sockets or NIC throughput claim.
#include "netforge/workers.hpp"
#include "../backends/xdp/fixture.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace netforge;
int main(int argc, char** argv) { try {
    if (argc != 4) throw std::runtime_error("usage: worker_bench PROJECT WORKERS ITERATIONS");
    auto number=[](const char* value) { std::string s(value); std::size_t end=0; auto n=std::stoul(s,&end); if(end!=s.size())throw std::runtime_error("invalid number");return n; };
    auto count=number(argv[2]), iterations=number(argv[3]);
    if(count<1 || count>2 || iterations<1 || iterations>2000)throw std::runtime_error("experiment bounds exceeded");
    auto config=load_config(std::string(argv[1])+"/examples/xdp-veth.conf");
    config.snat=Snat{"wan",address("10.89.0.1"),40000,40031};config.flow_limit=32;
    WorkerPool pool(config,count);
    std::vector<WorkerPacket> packets;
    for(unsigned i=0;i<16;++i){
        std::vector<std::uint8_t> frame(64);nf_fixture_packet(frame.data());
        nf_put16(frame.data()+34,static_cast<__u16>(10000+i));
        // IPv4 UDP permits an absent checksum; NAT must produce a valid one.
        nf_put16(frame.data()+40,0);
        packets.push_back({"lan",std::move(frame),0});
    }
    std::vector<std::unique_ptr<Appliance>> serial;
    for(std::size_t i=0;i<count;++i)serial.push_back(std::make_unique<Appliance>(pool.worker_config(i)));
    auto warm=pool.submit_batch(packets);std::vector<WorkerOutcome> expected;std::set<std::size_t> owners;std::set<unsigned> ports;
    for(unsigned i=0;i<16;++i){
        auto outcome=warm[i].get();auto oracle=serial[outcome.owner]->process("lan",packets[i].frame,0);
        if(outcome.decision.reason!="new_udp_mapping" || outcome.decision.egress!="wan" || outcome.decision.frame!=oracle.frame)throw std::runtime_error("serial warmup parity failed");
        owners.insert(outcome.owner);ports.insert(nf_u16(outcome.decision.frame.data()+34));expected.push_back(std::move(outcome));
    }
    if(owners.size()!=count || ports.size()!=16)throw std::runtime_error("owner/PAT coverage failed");
    const auto start=std::chrono::steady_clock::now();
    for(unsigned step=0;step<iterations;++step){
        auto futures=pool.submit_batch(packets);
        for(unsigned i=0;i<16;++i){auto result=futures[i].get();
            if(result.owner!=expected[i].owner || result.decision.reason!="tracked_udp" || result.decision.egress!="wan" || result.decision.frame!=expected[i].decision.frame)throw std::runtime_error("timed packet parity failed");
        }
    }
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    pool.shutdown();
    std::cout<<"{\"workers\":"<<count<<",\"packets\":"<<iterations*16<<",\"seconds\":"<<seconds<<",\"verified\":true,\"owners\":"<<owners.size()<<"}\n";
    return 0;
} catch(const std::exception& e){std::cerr<<"worker_bench: "<<e.what()<<'\n';return 1;} }
