# Historical M0 namespace lab

These are early Linux learning scripts, not the current portable demo or a
macOS setup path. Start with the [current demo](../docs/demo.md) and
[isolated live-lab guide](../docs/live-lab.md). Do not run these sudo namespace
commands on a workstation merely to try NetForge.


The topology is intentionally small:

```text
host-a (10.0.1.2/24) -- router -- host-b (10.0.2.2/24)
```

Run `sudo ./lab/create_topology.sh`, then use the commands printed by the script. The script only creates namespaces with the `nf-` prefix and refuses to overwrite an existing lab. Tear it down with `sudo ./lab/destroy_topology.sh`.

Suggested first observations:

```bash
sudo ip netns exec nf-router tcpdump -ni nf-ra -e -vv arp or icmp
sudo ip netns exec nf-host-a ping -c 1 10.0.2.2
sudo ip netns exec nf-host-a ip neigh show
```

Before reading code, explain from the capture why host-a resolves its default gateway rather than host-b.

