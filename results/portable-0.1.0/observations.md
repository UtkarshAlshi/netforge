# Descriptive virtual-network observations

Descriptive capped virtual-network observations; two trials/condition, no statistical speedup claim.

Each row reports medians of two trial rates and two trial p50/p95/p99 values.
Latency is same-host injection-to-capture, including generator/scheduling overhead.

|Mode|Frame bytes|Burst|Median trial packets/s|p50 µs|p95 µs|p99 µs|
|---|---:|---:|---:|---:|---:|---:|
|reference|64|1|38852|21.8|24.6|81.4|
|reference|64|4|95788|20.6|29.5|71.7|
|reference|64|16|100521|74.4|92.3|130.3|
|reference|128|1|36433|22.6|24.7|70.4|
|reference|128|4|73279|27.2|44.8|72.3|
|reference|128|16|76299|103.6|118.5|155.9|
|reference|512|1|21951|31.6|36.6|71.4|
|reference|512|4|32026|64.0|72.9|98.5|
|reference|512|16|31154|257.4|275.4|313.7|
|reference|1500|1|10316|57.3|71.7|98.4|
|reference|1500|4|12042|168.0|177.5|204.5|
|reference|1500|16|12221|655.0|678.4|702.8|
|dpdk|64|1|940|1062.5|1067.6|1078.1|
|dpdk|64|4|3735|1057.2|1059.5|1067.2|
|dpdk|64|16|14385|1035.1|1045.7|1047.4|
|dpdk|128|1|941|1061.1|1067.3|1074.6|
|dpdk|128|4|3729|1052.8|1056.0|1063.2|
|dpdk|128|16|14344|1012.8|1023.5|1024.9|
|dpdk|512|1|941|1052.0|1053.4|1060.3|
|dpdk|512|4|3721|1017.9|1023.7|1028.7|
|dpdk|512|16|13866|884.5|973.1|1008.8|
|dpdk|1500|1|941|1026.3|1029.5|1038.1|
|dpdk|1500|4|3710|918.3|992.4|1024.2|
|dpdk|1500|16|9544|915.7|1101.8|1116.0|

Raw SHA256: `1fd5facee2acf4bcc4473986f474efc3afc925c2baa09bdef42337c0de0b6197`

This is not a physical-NIC rate, saturation benchmark or confidence-based comparison.
