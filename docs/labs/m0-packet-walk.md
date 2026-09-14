# M0 packet walk: first ICMP probe

## Experiment

1. Start the topology in [`lab/README.md`](../../lab/README.md).
2. Capture ARP and ICMP on `nf-ra` before sending the first ping from host-a to host-b.
3. Repeat after the neighbor cache is warm.

## Prediction to test

Host-a routes `10.0.2.2` through its default gateway `10.0.1.1`. It therefore ARPs for the gateway's IP address and sends an Ethernet frame to the router interface's MAC address. The IP destination remains `10.0.2.2` across the router; the router decrements TTL, resolves host-b on its directly connected subnet, and creates a new Ethernet frame for the next link.

## Record before proceeding

- The ARP request/reply fields and the learned neighbor entries
- Ethernet source/destination MAC addresses on ingress and egress
- IPv4 TTL values before and after routing
- Why MAC addresses change but the IPv4 destination does not
