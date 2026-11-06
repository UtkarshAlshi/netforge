#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${NETFORGE_BUILD_DIR:-"$project_dir/build"}
sh "$project_dir/build.sh"
"$build_dir/ethernet_test"
"$build_dir/ipv4_test"
"$build_dir/tcp_test"
"$build_dir/scheduler_test"
"$build_dir/xdp_test"
python3 "$project_dir/tests/xdp_compile_test.py" "$build_dir/nf-xdp-compile" "$project_dir"
"$build_dir/workers_test" "$project_dir"
"$build_dir/appliance_test" "$project_dir/examples/appliance.conf"
"$build_dir/appliance_test" "$project_dir/examples/indexed-appliance.conf"
python3 "$project_dir/tests/cli_test.py" "$build_dir/netforge" "$project_dir"
python3 "$project_dir/tests/control_test.py" "$build_dir/netforge" "$project_dir"
python3 "$project_dir/tests/fleet_journal_test.py" "$project_dir"
python3 "$project_dir/tests/durable_test.py" "$build_dir/netforge" "$project_dir"
python3 "$project_dir/tests/health_test.py" "$project_dir"
python3 "$project_dir/tests/health_controller_test.py" "$build_dir/netforge" "$project_dir"

python3 "$project_dir/tests/worker_agent_test.py" "$build_dir/netforge" "$project_dir"

python3 "$project_dir/tests/audit_test.py" "$project_dir"

python3 "$project_dir/tests/metrics_view_test.py"
