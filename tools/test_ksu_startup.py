#!/usr/bin/env python3
"""Exercise the shipped post-root shell with mocked KSU and system commands.

Run with --shell PATH (bash or Android sh). The harness never invokes a real
ksud, touches /dev, or restarts Android. All state lives in a temporary directory.
"""

import argparse
import pathlib
import re
import subprocess
import tempfile


HEADER = pathlib.Path(__file__).resolve().parents[1] / "src/core/attack/ksu_startup_script.hpp"
MOCKS = r'''
LOG="$TEST_TMP/log"
COUNT="$TEST_TMP/count"
REQUESTS="$TEST_TMP/requests"
CLAIM="$TEST_TMP/claim"
: >"$LOG"
echo 0 >"$COUNT"
KSU_ALREADY=0
SAFE_MODE=0
LATE_TAG=ghostlock-test-unique
KSUD=mock_ksud
PENDING=0
PS_FAIL=0
DRIVER_MISSING=0
HELP_FAIL=0
REQUEST_FAIL=0
dump_debug() { echo dumped >>"$TEST_TMP/dumps"; }
ps() {
  [ "$*" = '-A -o ARGS' ] || return 70
  [ "$PS_FAIL" = 0 ] || return 71
  IFS= read -r count <"$COUNT"
  echo $((count+1)) >"$COUNT"
  echo ARGS
  echo 'unrelated-ksud late-load --kmi android16-6.12'
  if [ "$count" -lt "$PENDING" ]; then
    echo "$LATE_TAG late-load --kmi android16-6.12"
  fi
}
sleep() { [ "$*" = 1 ] || return 72; }
grep() {
  [ "$*" = '-q ^kernelsu[[:space:]] /proc/modules' ] || return 73
  [ "$DRIVER_MISSING" = 0 ]
}
mkdir() {
  [ "$*" = /dev/.ghostlock-ksu-soft-reboot ] || return 74
  [ ! -d "$CLAIM" ] || return 1
  command mkdir "$CLAIM"
}
mock_ksud() {
  if [ "$*" = 'soft-reboot --help' ]; then
    [ "$HELP_FAIL" = 0 ]
  elif [ "$*" = 'debug su -g' ]; then
    IFS= read -r request
    [ "$request" = 'exec "mock_ksud" soft-reboot' ] || return 77
    # A request while late-load is still pending is a test failure.
    [ "$(cat "$COUNT")" -gt "$PENDING" ] || return 75
    echo requested >>"$REQUESTS"
    [ "$REQUEST_FAIL" = 0 ]
  else
    return 76
  fi
}
expect_requests() {
  count=0
  [ ! -f "$REQUESTS" ] || count=$(wc -l <"$REQUESTS" | tr -d ' ')
  [ "$count" = "$1" ] || { echo "unexpected requests=$count expected=$1"; exit 90; }
}
'''

CASES = {
    "normal": 'complete_ksu_startup || exit 91; expect_requests 1',
    "wait-for-daemon": 'PENDING=3; complete_ksu_startup || exit 91; '
                       '[ "$(cat "$COUNT")" = 4 ] || exit 92; expect_requests 1',
    "timeout": 'PENDING=200; complete_ksu_startup && exit 93; expect_requests 0',
    "process-enumeration-fails": 'PS_FAIL=1; complete_ksu_startup && exit 93; expect_requests 0',
    "missing-driver": 'DRIVER_MISSING=1; complete_ksu_startup && exit 93; expect_requests 0',
    "unsupported-command": 'HELP_FAIL=1; complete_ksu_startup && exit 93; expect_requests 0',
    "single-request": 'complete_ksu_startup || exit 91; complete_ksu_startup; expect_requests 1',
    "failed-request-not-retried": 'REQUEST_FAIL=1; complete_ksu_startup && exit 93; '
                                  'complete_ksu_startup; expect_requests 1',
    "empty-process-inventory": 'ps() { return 0; }; complete_ksu_startup && exit 93; expect_requests 0',
    "claim-unavailable": 'mkdir() { return 1; }; complete_ksu_startup && exit 93; expect_requests 0',
    "process-parser-fails": 'awk() { return 2; }; complete_ksu_startup && exit 93; expect_requests 0',
    "safe-mode": 'SAFE_MODE=1; complete_ksu_startup || exit 91; expect_requests 0; '
                 '[ "$(cat "$COUNT")" = 0 ] || exit 92',
    "existing-ksu": 'KSU_ALREADY=1; complete_ksu_startup || exit 91; expect_requests 0; '
                    '[ "$(cat "$COUNT")" = 0 ] || exit 92',
    "late-load-command-fails": 'KSUD=false; KMI=android16-6.12; run_late_load && exit 93; expect_requests 0',
    "late-load-command-accepts": 'KSUD=true; KMI=android16-6.12; run_late_load || exit 91; expect_requests 0',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shell", default="sh")
    parser.add_argument("--emit", type=pathlib.Path, help="Write isolated cases for Android shell testing")
    args = parser.parse_args()
    match = re.search(r'R"glk\((.*?)\)glk"', HEADER.read_text(encoding="utf-8"), re.S)
    if match is None:
        raise SystemExit("startup shell literal not found")
    startup = match.group(1)
    for name, body in CASES.items():
        script = startup + MOCKS + '\n' + body + '\n'
        if args.emit:
            args.emit.mkdir(parents=True, exist_ok=True)
            (args.emit / (name + ".sh")).write_text(script, encoding="utf-8", newline="\n")
            continue
        with tempfile.TemporaryDirectory(prefix="ghostlock-startup-") as directory:
            # Forward slashes work with both MSYS bash and ordinary POSIX shells.
            prefix = 'TEST_TMP="' + directory.replace("\\", "/") + '"\n'
            result = subprocess.run([args.shell], input=(prefix + script).encode("utf-8"),
                                    capture_output=True, timeout=90)
            if result.returncode != 0:
                raise SystemExit(f"FAIL {name} ({result.returncode}): {result.stdout}\n{result.stderr}")
        print("PASS", name)


if __name__ == "__main__":
    main()
