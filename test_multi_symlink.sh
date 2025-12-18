#!/usr/bin/env bash
set -euo pipefail

# Test multi-level symlink chain:
#   a -> b -> c -> real_dir
# And access a/d (where real_dir/d exists)
#
# This test checks whether file_monitor records the accessed path containing symlinks.

ROOT="$(pwd)/test_multi_symlink"
echo "ROOT: ${ROOT}"
LOG="${ROOT}/access.log"

cleanup() {
  set +e
  if [[ -n "${MON_PID:-}" ]]; then
    kill -INT "${MON_PID}" 2>/dev/null || true
    wait "${MON_PID}" 2>/dev/null || true
  fi
  #rm -rf "${ROOT}"
}
trap cleanup EXIT

rm -rf "${ROOT}"

mkdir -p "${ROOT}/real_dir"
echo "hello" > "${ROOT}/real_dir/d"

ln -s "real_dir" "${ROOT}/c"
ln -s "c" "${ROOT}/b"
ln -s "b" "${ROOT}/a"

# Start monitor on ROOT so it sees a/b/c links and the real_dir
: > "${LOG}"
./file_monitor -s -i 1 -l "${LOG}" "${ROOT}" &
MON_PID=$!

# Give monitor time to install watches and ensure it started successfully
sleep 1
if ! kill -0 "${MON_PID}" 2>/dev/null; then
  echo "FAIL: file_monitor failed to start (lock/permission?)." >&2
  echo "log path: ${LOG}" >&2
  exit 1
fi

# Access through the multi-level symlink path
cat "${ROOT}/a/d" >/dev/null

# Wait for event processing
sleep 1

# Stop monitor to flush
kill -INT "${MON_PID}" >/dev/null 2>&1 || true
wait "${MON_PID}" >/dev/null 2>&1 || true
unset MON_PID

EXPECTED_A="${ROOT}/a/d"

if grep -F -x "${EXPECTED_A}" "${LOG}" >/dev/null 2>&1; then
  echo "PASS: recorded symlink path: ${EXPECTED_A}"
else
  echo "FAIL: did not record symlink path: ${EXPECTED_A}" >&2
  echo "--- log ---" >&2
  cat "${LOG}" >&2
  exit 1
fi

ROOT2="$(pwd)/test_multi_symlink_chain"
LOG2="${ROOT2}/access.log"

rm -rf "${ROOT2}"
mkdir -p "${ROOT2}/real0" "${ROOT2}/real1" "${ROOT2}/real2" "${ROOT2}/real3"
echo "hello2" > "${ROOT2}/real3/file"

ln -s "real0" "${ROOT2}/a"
ln -s "../real1" "${ROOT2}/real0/b"
ln -s "../real2" "${ROOT2}/real1/c"
ln -s "../real3" "${ROOT2}/real2/d"

: > "${LOG2}"
./file_monitor -s -i 1 -l "${LOG2}" "${ROOT2}" &
MON_PID=$!
sleep 1
if ! kill -0 "${MON_PID}" 2>/dev/null; then
  echo "FAIL: file_monitor failed to start (lock/permission?)." >&2
  echo "log path: ${LOG2}" >&2
  exit 1
fi

cat "${ROOT2}/a/b/c/d/file" >/dev/null
sleep 1
kill -INT "${MON_PID}" >/dev/null 2>&1 || true
wait "${MON_PID}" >/dev/null 2>&1 || true
unset MON_PID

EXPECTED_CHAIN="${ROOT2}/a/b/c/d/file"
if grep -F -x "${EXPECTED_CHAIN}" "${LOG2}" >/dev/null 2>&1; then
  echo "PASS: recorded symlink chain path: ${EXPECTED_CHAIN}"
else
  echo "FAIL: did not record symlink chain path: ${EXPECTED_CHAIN}" >&2
  echo "--- log ---" >&2
  cat "${LOG2}" >&2
  exit 1
fi
