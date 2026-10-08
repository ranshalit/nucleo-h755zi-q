# native_sim build-and-run helper

Add `scripts/run_zephyr_hello_shell_native.sh` as a separate, repository-relative
entry point for the existing Zephyr application. Leave the board flash script
unchanged. The default action builds the localhost profile with the host
toolchain, then runs its `zephyr.exe` in the foreground. `--profile tap`
selects the TAP configuration instead. `--build-only` builds without running,
and `--run-only` runs an already-built executable without building.

Derive repository and app paths from the script's location, not the caller's
working directory or branch name. Pass absolute app, config, and build paths
to `west build` from the vendored Zephyr directory; this also handles a
symlinked Zephyr workspace in a git worktree. Keep TAP and localhost build
directories separate under the repository's ignored `build/` directory.
Support `--build-dir PATH` for an explicitly chosen output directory; resolve
relative values against the caller's working directory. Allow `WEST_BIN` to
select the `west` executable; require it only when building. The script must
work unchanged after the feature branch merges into the main checkout.

Validate profile and option arguments; show usage on `--help`. Report missing
Zephyr workspace, `west`, config file, or run-only executable as explicit
errors. Propagate build and simulator exit status. Announce the dashboard URL:
`http://127.0.0.1:8080/` for localhost and `http://192.168.55.2/` for TAP.
Never create a TAP interface or change host networking: TAP needs a separately
configured host interface at `192.168.55.100/24`. The simulator remains a
foreground process, stopped by Ctrl+C.

Update `README.md` to make the helper the primary way to build/run while
retaining manual `west build` examples. Verify invocation from another working
directory, both profiles, help and invalid arguments, build-only and run-only,
localhost HTTP, TAP HTTP with an isolated network namespace, and unchanged
board build behavior.
