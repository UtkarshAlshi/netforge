# Contributing

Start with the [architecture](docs/architecture.md) and [capability matrix](docs/capabilities.md).
Use a focused branch and explain the observable before/after behavior.

```sh
python3 tools/verify.py --sanitizers --output ../netforge-check-change
```

Keep the build serial on small machines. Tests should check independent packet
bytes or meaningful state/failure boundaries rather than duplicate the algorithm.
Runtime changes need the relevant isolated Linux acceptance in addition to portable
checks. Do not replace a failed backend gate with a native model-only result.

Update configuration/backend contracts when semantics change. Record benchmark
source, environment, raw observations and limitations; do not claim NIC performance
from offline invocation rates. Use synthetic fixtures or review capture provenance.

After adding/moving source files, refresh `SOURCE-MANIFEST.sha256` with:

```sh
python3 tools/update_manifest.py
python3 tools/check_docs.py
```

The manifest intentionally includes authored source/docs and small sanitized
results, not generated builds, secrets or private deployment files. Automatic CI runs Linux portable checks; manual **Run workflow** additionally
runs macOS and real DPDK/private-veth acceptance in a capped disposable container. A green badge corresponds to that workflow, not hardware research.

Preserve historical records under `docs/archive/`; do not present their pending
milestone text as current setup guidance. Never prune unrelated Docker resources,
commit credentials/binaries, change host networking or attach a production NIC.
