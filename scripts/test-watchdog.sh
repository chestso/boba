#!/bin/sh
# test-watchdog.sh - per-test runtime cap for make check
#
# Wired in as automake's LOG_COMPILER (the suffix-less unit binaries) and
# SH_LOG_COMPILER (the tmux e2e scripts): every test runs with a
# background watchdog. If it exceeds BOBA_TEST_TIMEOUT seconds (default
# 10 - a unit test is ms-fast; a hang is a bug, not a condition to wait
# out), the watchdog:
#
#   1. announces the hang into the test's .log (test-driver redirects our
#      stdout there),
#   2. grabs a stack snapshot - every diagnostic individually time-boxed
#      and SIGKILLed if it overstays. This is the macOS CI lesson: `lldb
#      -p` used to run BEFORE the kill chain, so one stuck attach wedged
#      the wrapper and stalled the whole workflow for hours (Linux/Windows
#      have no lldb and stayed green - the asymmetry was the tell).
#      MSYS2 CI ships no gdb/lldb at all, so there the kill chain is the
#      whole story - the progress log is what names the culprit,
#   3. kills the test: QUIT, then ABRT, then KILL, each signal followed by
#      a bounded settle, so the chain ALWAYS completes even if a signal is
#      ignored or a diagnostic left the target SIGSTOPped (we CONT after
#      debuggers die),
#
# so a hang FAILS loudly, naming itself, instead of stalling the workflow
# until the job's own timeout cancels it with nothing to read (the
# 2026-09-22 MSYS2 run: the Windows job sat in `make check` past its
# 20-minute cap and the live log was not fetchable, so which test hung
# was unknowable).
#
# A START/END pair with UTC timestamps also lands in BOBA_TEST_PROGRESS
# (default test-progress.log, the tests build dir) - which test hung and
# for how long is answerable from CI artifacts even when the stack
# snapshot comes up empty. The workflows upload that file.
#
# The nonzero wait status (128+signal) makes test-driver report FAIL for
# a killed test. Passes through transparently otherwise.
# BOBA_TEST_TIMEOUT=0 disables the cap for interactive debugging.
#
# Never a polling loop in the app sense: this is test harness only.

secs=${BOBA_TEST_TIMEOUT:-10}
progress=${BOBA_TEST_PROGRESS:-test-progress.log}

now() { date -u +%H:%M:%S; }

echo "$(now) START $*" >>"$progress"

# Run one diagnostic under a hard time box: if it does not exit on its own
# within $1 seconds it is SIGKILLed - a stuck lldb/sample must never delay
# the kill chain. (Killing an attached debugger leaves the target
# SIGSTOPped; callers CONT it afterwards.)
diag() {
	d_secs=$1
	shift
	"$@" &
	d_pid=$!
	d_left=$((d_secs * 10))
	while kill -0 "$d_pid" 2>/dev/null && [ "$d_left" -gt 0 ]; do
		sleep 0.1
		d_left=$((d_left - 1))
	done
	if kill -0 "$d_pid" 2>/dev/null; then
		echo "=== WATCHDOG: diagnostic still running after ${d_secs}s, SIGKILLing it ==="
		kill -KILL "$d_pid" 2>/dev/null || true
	fi
	wait "$d_pid" 2>/dev/null || true
}

ulimit -c unlimited 2>/dev/null || true

"$@" &
pid=$!

if [ "$secs" -gt 0 ]; then
	(
		sleep "$secs" 2>/dev/null
		if kill -0 "$pid" 2>/dev/null; then
			echo ""
			echo "=== WATCHDOG: test still running after ${secs}s: $* (pid $pid) ==="
			echo "=== WATCHDOG: stack snapshot (each boxed), then kill ==="
			# The debuggers attach by NATIVE pid: under MSYS2 $! is a
			# Cygwin pid and `gdb -p` answers "error 87: The parameter
			# is incorrect" (the 2026-09-22 MSYS2 run, where the
			# snapshot came up empty for exactly that reason). ps -W's
			# WINPID column is the translation. Gated on MSYSTEM, not
			# on ps's exit status: GNU ps accepts -W as a no-op and
			# would hand back a TTY name to attach to.
			diag_pid=$pid
			if [ -n "${MSYSTEM:-}" ]; then
				win_pid=$(ps -W 2>/dev/null |
					awk -v p="$pid" '$1 == p { print $4; exit }')
				[ -n "$win_pid" ] && diag_pid=$win_pid
			fi
			if command -v sample >/dev/null 2>&1; then
				diag 5 sample "$diag_pid" 1 2>&1 || true
				kill -CONT "$pid" 2>/dev/null || true
			fi
			if command -v lldb >/dev/null 2>&1; then
				diag 8 lldb -p "$diag_pid" -b \
					-o "thread backtrace all" -o "detach" 2>&1 || true
				kill -CONT "$pid" 2>/dev/null || true
			fi
			if command -v gdb >/dev/null 2>&1; then
				diag 8 gdb -p "$diag_pid" -batch -ex "thread apply all bt" 2>&1 || true
				kill -CONT "$pid" 2>/dev/null || true
			fi
			# Kill chain: each signal, then a bounded settle, so the
			# chain completes even if a signal is ignored.
			for sig in QUIT ABRT; do
				kill -"$sig" "$pid" 2>/dev/null || true
				i=0
				while kill -0 "$pid" 2>/dev/null && [ "$i" -lt 20 ]; do
					sleep 0.1
					i=$((i + 1))
				done
				kill -0 "$pid" 2>/dev/null || break
			done
			kill -KILL "$pid" 2>/dev/null || true
			echo "=== WATCHDOG: end of watchdog for pid $pid ==="
		fi
	) &
	wd=$!
fi

wait "$pid"
st=$?
if [ -n "$wd" ]; then
	kill "$wd" 2>/dev/null || true
	wait "$wd" 2>/dev/null || true
fi
echo "$(now) END status=$st $*" >>"$progress"
exit $st
