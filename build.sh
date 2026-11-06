#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${NETFORGE_BUILD_DIR:-"$project_dir/build"}
mkdir -p "$build_dir"
compiler=${CXX:-clang++}
set -- -std=c++20 -Wall -Wextra -Wpedantic -Werror
if [ "${SANITIZE:-1}" = 1 ]; then
    set -- "$@" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
else
    set -- "$@" -O2
fi
if [ "$(uname -s)" = Darwin ]; then set -- "$@" -stdlib=libc++; fi
compile() { env -u CPATH -u CPLUS_INCLUDE_PATH -u C_INCLUDE_PATH "$compiler" "$@"; }
for component in ethernet ipv4 tcp config scheduler appliance; do
    compile "$@" -I "$project_dir/include" -c "$project_dir/src/$component.cpp" -o "$build_dir/$component.o"
done
compile "$@" -pthread -I "$project_dir/include" -c "$project_dir/src/workers.cpp" -o "$build_dir/workers.o"
compile "$@" -pthread -I "$project_dir/include" "$project_dir/src/main.cpp" "$project_dir/src/linux_io.cpp" \
    "$build_dir/ethernet.o" "$build_dir/ipv4.o" "$build_dir/tcp.o" "$build_dir/config.o" "$build_dir/scheduler.o" "$build_dir/appliance.o" \
    "$build_dir/workers.o" -o "$build_dir/netforge"
for component in ethernet ipv4 tcp; do
    compile "$@" -I "$project_dir/include" "$project_dir/tests/${component}_test.cpp" \
        "$build_dir/$component.o" -o "$build_dir/${component}_test"
done
compile "$@" -I "$project_dir/include" "$project_dir/tests/scheduler_test.cpp" \
    "$build_dir/scheduler.o" "$build_dir/config.o" "$build_dir/ipv4.o" -o "$build_dir/scheduler_test"
compile "$@" -I "$project_dir/include" "$project_dir/tests/appliance_test.cpp" \
    "$build_dir/ethernet.o" "$build_dir/ipv4.o" "$build_dir/tcp.o" "$build_dir/config.o" "$build_dir/scheduler.o" "$build_dir/appliance.o" \
    -o "$build_dir/appliance_test"
compile "$@" -I "$project_dir/include" "$project_dir/tests/xdp_test.cpp" \
    "$build_dir/ethernet.o" "$build_dir/ipv4.o" "$build_dir/tcp.o" "$build_dir/config.o" "$build_dir/scheduler.o" "$build_dir/appliance.o" \
    -o "$build_dir/xdp_test"
for tool in udp_probe tcp_probe icmp_probe; do
    compile "$@" "$project_dir/tools/$tool.cpp" -o "$build_dir/$tool"
done
compile "$@" -I "$project_dir/include" "$project_dir/backends/xdp/compile.cpp" \
    "$build_dir/config.o" "$build_dir/ipv4.o" "$build_dir/scheduler.o" -o "$build_dir/nf-xdp-compile"
compile "$@" -pthread -I "$project_dir/include" "$project_dir/tests/workers_test.cpp" \
    "$build_dir/workers.o" "$build_dir/ethernet.o" "$build_dir/ipv4.o" "$build_dir/tcp.o" \
    "$build_dir/config.o" "$build_dir/scheduler.o" "$build_dir/appliance.o" -o "$build_dir/workers_test"
printf 'Built NetForge and tests in %s\n' "$build_dir"
