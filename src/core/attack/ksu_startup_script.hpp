#ifndef GHOSTLOCK_ATTACK_KSU_STARTUP_SCRIPT_HPP
#define GHOSTLOCK_ATTACK_KSU_STARTUP_SCRIPT_HPP

namespace ghostlock::attack {
    /* Kept as one literal so the host harness executes the shipped shell code.
     * Only the detached, already-rooted worker uses these functions. */
    inline constexpr char kKsuStartupScript[] = R"glk(
run_late_load() {
  LATE_TAG="ghostlock-ksud-late-$$"
  (exec -a "$LATE_TAG" "$KSUD" late-load --kmi "$KMI" --allow-shell) >>"$LOG" 2>&1
  LATE_RC=$?
  echo "[*] late-load exit=$LATE_RC" >>"$LOG"
  return "$LATE_RC"
}
complete_ksu_startup() {
  if [ "$KSU_ALREADY" = 1 ] || [ "$SAFE_MODE" = 1 ]; then
    echo '[*] automatic KSU soft-reboot skipped: existing KSU or safe mode' >>"$LOG"
    return 0
  fi
  # The CLI exits after daemonizing, before the module stages finish. Track only
  # this invocation's inherited argv0; never overlap its stages with soft-reboot.
  for attempt in $(seq 1 120); do
    LATE_PROCESSES=$(ps -A -o ARGS 2>>"$LOG") || {
      echo '[!] cannot inspect late-load; automatic soft-reboot skipped' >>"$LOG"
      return 1
    }
    if [ -z "$LATE_PROCESSES" ]; then
      echo '[!] empty process inventory; automatic soft-reboot skipped' >>"$LOG"
      return 1
    fi
    printf '%s\n' "$LATE_PROCESSES" | awk -v tag="$LATE_TAG" '
      $1 == tag { found=1 }
      END { exit !found }
    '
    LATE_SCAN_RC=$?
    if [ "$LATE_SCAN_RC" = 1 ]; then
      break
    fi
    if [ "$LATE_SCAN_RC" != 0 ]; then
      echo '[!] cannot parse late-load processes; automatic soft-reboot skipped' >>"$LOG"
      return 1
    fi
    if [ "$attempt" = 120 ]; then
      echo '[!] late-load still running; automatic soft-reboot timed out' >>"$LOG"
      return 1
    fi
    sleep 1
  done
  if ! grep -q '^kernelsu[[:space:]]' /proc/modules 2>/dev/null; then
    echo '[!] KSU driver unavailable; automatic soft-reboot skipped' >>"$LOG"
    return 1
  fi
  if ! "$KSUD" soft-reboot --help >>"$LOG" 2>&1; then
    echo '[!] KSU soft-reboot unsupported; automatic soft-reboot skipped' >>"$LOG"
    return 1
  fi
  # /dev survives a framework restart and is cleared by a full boot. Keep the
  # claim on failure too: an automatic reboot must never become a retry loop.
  if ! (umask 077; mkdir /dev/.ghostlock-ksu-soft-reboot) 2>>"$LOG"; then
    if [ -d /dev/.ghostlock-ksu-soft-reboot ]; then
      echo '[*] automatic KSU soft-reboot already claimed; skipping' >>"$LOG"
      return 0
    fi
    echo '[!] cannot claim automatic KSU soft-reboot; skipping' >>"$LOG"
    return 1
  fi
  echo '[+] late-load exited; requesting complete KSU soft-reboot' >>"$LOG"
  dump_debug
  # Match the Manager's root-shell entry, including the global mount namespace,
  # instead of passing the temporary root shell's process context to the reboot.
  printf 'exec "%s" soft-reboot\n' "$KSUD" | "$KSUD" debug su -g >>"$LOG" 2>&1
  REBOOT_RC=$?
  echo "[*] KSU soft-reboot request exit=$REBOOT_RC (completion is asynchronous)" >>"$LOG"
  return "$REBOOT_RC"
}
)glk";
} // namespace ghostlock::attack

#endif
