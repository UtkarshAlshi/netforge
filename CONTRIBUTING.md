# Contributing

Use short-lived feature branches and small reviewable commits. Each behavioral
change needs a packet-level expected result, support-matrix update where relevant,
and tests for failure/resource boundaries. Keep ownership and configuration
semantics explicit. Build serially, run the sanitizer suites, and avoid test fixtures
that only repeat implementation logic. Capture-derived fixtures need provenance,
privacy review and a declared capture location/offload state.

CI is configured for Linux and macOS CMake/CTest builds. It has not run until this
repository is published and the workflow completes. No remote repository, PR,
release or commit is created automatically by these source files.

Do not use broad Docker pruning or privileged host-network containers. Preserve
manual Docker execution and deletion approvals in this workspace. Never commit
credentials, private traffic captures or generated build binaries.
