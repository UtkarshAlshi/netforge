#!/usr/bin/env bash
set -euo pipefail

for ns in nf-host-a nf-router nf-host-b; do
  if ip netns list | awk '{print $1}' | grep -qx "$ns"; then
    echo "refusing to overwrite existing namespace: $ns" >&2
    exit 1
  fi
done

ip netns add nf-host-a
ip netns add nf-router
ip netns add nf-host-b
ip link add nf-ha type veth peer name nf-ra
ip link add nf-rb type veth peer name nf-hb
ip link set nf-ha netns nf-host-a
ip link set nf-ra netns nf-router
ip link set nf-rb netns nf-router
ip link set nf-hb netns nf-host-b

ip -n nf-host-a addr add 10.0.1.2/24 dev nf-ha
ip -n nf-router addr add 10.0.1.1/24 dev nf-ra
ip -n nf-router addr add 10.0.2.1/24 dev nf-rb
ip -n nf-host-b addr add 10.0.2.2/24 dev nf-hb
ip -n nf-host-a route add default via 10.0.1.1
ip -n nf-host-b route add default via 10.0.2.1
ip netns exec nf-router sysctl -qw net.ipv4.ip_forward=1
for ns in nf-host-a nf-router nf-host-b; do ip -n "$ns" link set lo up; done
for link in nf-ha nf-ra nf-rb nf-hb; do ip -n "$(case "$link" in nf-ha) echo nf-host-a;; nf-hb) echo nf-host-b;; *) echo nf-router;; esac)" link set "$link" up; done
echo 'Lab ready. Capture: sudo ip netns exec nf-router tcpdump -ni nf-ra -e -vv arp or icmp'
echo 'Probe:   sudo ip netns exec nf-host-a ping -c 1 10.0.2.2'
