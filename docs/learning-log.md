# Learning log

## Demonstrated

_No concept is marked demonstrated until it is explained from an observed experiment or implementation review._

## In progress — M0/M1

- Linux network namespaces, veth links, IP routes, and forwarding
- Ethernet frame layout and network byte order
- IPv4 fixed header, IHL, TTL, and protocol fields
- Bounds checking when decoding untrusted packet bytes

## Open questions

- Why does a remote-subnet packet use the router's MAC address rather than the destination host's MAC address?
- Which exact packets are emitted before the first ICMP echo crosses the namespace router?
- What future buffer owner will be responsible for returning a received packet to a pool?
