# Packet semantics

IPv4 header length comes from IHL in four-byte units, while total length includes
header and payload. Ethernet padding past IP total length is ignored. The header
checksum covers the entire IPv4 header, not payload. UDP/TCP checksums include a
pseudoheader with addresses, protocol and transport length. IPv4 UDP checksum zero
is accepted as absent; translated/tracked output receives a generated checksum.

The appliance checks UDP length against the full IP payload (strict equality),
TCP header bounds and checksum, and ICMP length/checksum. It does not implement a
TCP endpoint, TCP sequence validation or a full ICMP protocol suite. A parseable
packet is not necessarily forwardable.

Transit forwarding leaves destination IP unchanged except for service DNAT,
rewrites Ethernet addresses for the egress link, decrements TTL and recalculates
checksums. TTL <=1 yields a rate-limited ICMP Time Exceeded. Missing routes yield
Destination Unreachable. MTU excess with DF set yields Fragmentation Needed with
the configured next-hop MTU. Without DF, packets requiring fragmentation are
explicitly dropped because fragmentation is unsupported.

Errors quote the original IPv4 header plus up to eight payload bytes. This release
uses fixed 20-byte headers in its forwarding subset. It suppresses errors in
response to ICMP errors and rejects non-unicast endpoints and fragments before
error generation. A global limit of ten generated errors per one-second window
bounds responses. This is not a full RFC 1812 conformance implementation.

[IPv4 header specification](https://www.rfc-editor.org/rfc/rfc791),
[Internet checksum](https://www.rfc-editor.org/rfc/rfc1071),
[router requirements](https://www.rfc-editor.org/rfc/rfc1812).
