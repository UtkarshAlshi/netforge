#!/usr/bin/env bash
set -euo pipefail
for ns in nf-host-a nf-router nf-host-b; do
  ip netns del "$ns" 2>/dev/null || true
done

