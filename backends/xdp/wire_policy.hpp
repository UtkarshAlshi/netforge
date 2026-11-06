#pragma once
// Version1 policy interchange. Every integer is big endian; no native padding.
#include "policy.h"
#include <array>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <vector>
namespace netforge::xdp {
inline void validate_wire_policy(const nf_policy& p) {
    if (p.rule_count>NF_RULES || p.route_count>NF_ROUTES || p.default_allow>1) throw std::runtime_error("wire policy bounds");
    for(unsigned i=0;i<p.rule_count;++i){const auto& r=p.rules[i];
        if(r.source_prefix>32 || r.destination_prefix>32 || r.allow>1 || (r.protocol!=0 && r.protocol!=1 && r.protocol!=6 && r.protocol!=17))throw std::runtime_error("wire rule invalid");
        auto canonical=[](__u32 value,unsigned prefix){const __u32 mask=prefix?0xffffffffU<<(32-prefix):0;return (value&mask)==value;};
        if(!canonical(r.source,r.source_prefix)||!canonical(r.destination,r.destination_prefix))throw std::runtime_error("wire rule prefix");
    }
    for(unsigned i=0;i<p.route_count;++i){const auto& r=p.routes[i];
        if(r.prefix>32 || !r.ifindex || r.ifindex>0x7fffffffU || r.mtu<68)throw std::runtime_error("wire route invalid");
        const __u32 mask=r.prefix?0xffffffffU<<(32-r.prefix):0;
        if((r.network&mask)!=r.network)throw std::runtime_error("wire route prefix");
    }
}
inline std::vector<__u8> encode_policy(const nf_policy& p){
    validate_wire_policy(p);
    std::vector<__u8> out{'N','F','P','O','L',0,0,1};
    auto put=[&](unsigned value,unsigned bytes){for(unsigned i=bytes;i>0;--i)out.push_back(static_cast<__u8>(value>>(8*(i-1))));};
    put(p.rule_count,4);put(p.route_count,4);put(p.default_allow,4);
    for(auto byte:p.ingress_mac)put(byte,1);
    for(unsigned i=0;i<p.rule_count;++i){const auto& r=p.rules[i];
        put(r.ingress,4);put(r.source,4);put(r.destination,4);put(r.sport,2);put(r.dport,2);
        put(r.source_prefix,1);put(r.destination_prefix,1);put(r.protocol,1);put(r.allow,1);
    }
    for(unsigned i=0;i<p.route_count;++i){const auto& r=p.routes[i];
        put(r.network,4);put(r.ifindex,4);put(r.mtu,2);put(r.prefix,1);
        for(auto byte:r.source_mac)put(byte,1);
        for(auto byte:r.destination_mac)put(byte,1);
    }
    return out;
}
inline nf_policy decode_policy(std::span<const __u8> bytes){
    constexpr std::array<__u8,8> magic{'N','F','P','O','L',0,0,1};
    if(bytes.size()<26 || bytes.size()>1034 || !std::equal(magic.begin(),magic.end(),bytes.begin()))throw std::runtime_error("wire header/version");
    std::size_t offset=8;
    auto get=[&](unsigned length){if(offset+length>bytes.size())throw std::runtime_error("truncated wire policy");unsigned value=0;for(unsigned i=0;i<length;++i)value=(value<<8)|bytes[offset++];return value;};
    nf_policy p{};p.rule_count=get(4);p.route_count=get(4);p.default_allow=get(4);
    if(p.rule_count>NF_RULES || p.route_count>NF_ROUTES || bytes.size()!=26+20*p.rule_count+23*p.route_count)throw std::runtime_error("wire length/count");
    for(auto& byte:p.ingress_mac)byte=static_cast<__u8>(get(1));
    for(unsigned i=0;i<p.rule_count;++i){auto& r=p.rules[i];
        r.ingress=get(4);r.source=get(4);r.destination=get(4);r.sport=static_cast<__u16>(get(2));r.dport=static_cast<__u16>(get(2));
        r.source_prefix=static_cast<__u8>(get(1));r.destination_prefix=static_cast<__u8>(get(1));r.protocol=static_cast<__u8>(get(1));r.allow=static_cast<__u8>(get(1));
    }
    for(unsigned i=0;i<p.route_count;++i){auto& r=p.routes[i];
        r.network=get(4);r.ifindex=get(4);r.mtu=static_cast<__u16>(get(2));r.prefix=static_cast<__u8>(get(1));
        for(auto& byte:r.source_mac)byte=static_cast<__u8>(get(1));
        for(auto& byte:r.destination_mac)byte=static_cast<__u8>(get(1));
    }
    validate_wire_policy(p);return p;
}
}
