#!/bin/sh
# Ephemeral GitHub Linux container only; never execute on the workstation.
set -eu
test "${NETFORGE_HOSTED_DPDK:-}" = 1 || { echo 'Hosted DPDK opt-in required'; exit 1; }
test "$(cat /sys/fs/cgroup/memory.max)" = 536870912
test "$(cat /sys/fs/cgroup/pids.max)" = 128
test "$(cat /sys/fs/cgroup/cpu.max)" = '50000 100000'
export DEBIAN_FRONTEND=noninteractive
mkdir -p /tmp/netforge-results
apt-get update
apt-get install -y --no-install-recommends g++ cmake make python3 ca-certificates iproute2
# Archive is generated from the verified manifest by the hosted parent job.
tar -xzf /tmp/netforge-source.tar.gz -C /tmp
src=/tmp/netforge
out=/tmp/netforge-results
NETFORGE_DPDK_BUILD_CONTAINER=1 python3 "$src/tools/install_dpdk_subset.py" /tmp/netforge-sdk
cmake -S "$src" -B /tmp/netforge-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/netforge-build --parallel 1
ctest --test-dir /tmp/netforge-build --output-on-failure --timeout 40
NETFORGE_DPDK_CORE_ARCHIVE=/tmp/netforge-build/libnetforge_core.a python3 "$src/tools/build_dpdk_ring.py" "$src" /tmp/netforge-sdk /tmp/nf-dpdk-ring
/tmp/netforge-build/nf-xdp-compile "$src/examples/xdp-veth.conf" lan /tmp/policy.bin lan=1 wan=2
/tmp/nf-dpdk-ring "$src/examples/xdp-veth.conf" /tmp/policy.bin
python3 "$src/tests/dpdk_pcap_test.py" /tmp/nf-dpdk-ring "$src/examples/xdp-veth.conf" /tmp/policy.bin /tmp/netforge-build/netforge "$out/capture-cases"
python3 "$src/tests/dpdk_config_test.py" /tmp/nf-dpdk-ring /tmp/netforge-build/nf-xdp-compile /tmp/netforge-build/netforge "$src" "$out/multi-cases"
python3 "$src/tests/capture_agent_http_test.py" /tmp/nf-dpdk-ring /tmp/netforge-build/nf-xdp-compile "$out/multi-cases/multi.conf" "$out/multi-cases/input.pcap" "$src" "$out/capture-service.json"
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror "$src/tools/workers_live_probe.cpp" -o /tmp/netforge-worker-probe
python3 "$src/tests/durable_worker_live_test.py" /tmp/netforge-build/netforge /tmp/netforge-worker-probe "$src" "$out/durable-worker.json"
python3 "$src/tests/secure_node_test.py" "$src" /tmp/netforge-build/netforge
cp "$src/SOURCE-MANIFEST.sha256" "$out/SOURCE-MANIFEST.sha256"
cp /tmp/netforge-sdk/install.log "$out/dependencies.log"
echo 'PASS: hosted real DPDK corpus, capture, policy identity, multi-route parity and bounded cleanup'
