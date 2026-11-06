#include "../backends/dpdk/policy_binding.hpp"
#include <iostream>
using namespace netforge;
template<class F> void rejects(F call){try{call();}catch(const std::exception&){return;}throw std::runtime_error("invalid DPDK binding accepted");}
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("supply project directory");
 auto config=load_config(std::string(argv[1])+"/examples/xdp-veth.conf");
 auto compile=[](const Config& c){return xdp::compile_policy(c,"lan",{{"lan",1},{"wan",2}});};
 auto policy=compile(config);dpdk::validate_binding(config,policy);
 auto extra=config.rules.front();extra.destination_port=9001;config.rules.push_back(extra);
 config.routes.push_back({{address("10.90.0.0"),24},"wan",address("10.89.0.2")});
 rejects([&]{dpdk::validate_binding(config,policy);});
 policy=compile(config);dpdk::validate_binding(config,policy);
 if(policy.rule_count!=2 || policy.route_count!=2)throw std::runtime_error("multiple rules/routes lost");
 auto corrupted=policy;corrupted.routes[0].destination_mac[5]^=1;
 rejects([&]{dpdk::validate_binding(config,corrupted);});
 corrupted=policy;corrupted.rules[0].allow=0;rejects([&]{dpdk::validate_binding(config,corrupted);});
 auto unsupported=config;unsupported.snat=Snat{"wan",address("10.89.0.1"),40000,40031};
 rejects([&]{dpdk::validate_binding(unsupported,policy);});
 unsupported=config;unsupported.routes.push_back({{address("10.88.0.2"),32},"lan",0});
 unsupported.neighbors.push_back({"lan",address("10.88.0.2"),mac_address("02:88:00:00:00:01")});
 rejects([&]{dpdk::validate_binding(unsupported,compile(unsupported));});
 std::cout<<"PASS: DPDK config-policy identity, multiple rules/routes, tampering and unsupported-feature rejection\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
