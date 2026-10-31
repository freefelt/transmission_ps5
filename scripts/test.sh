#!/usr/bin/env bash
# Runs the tests. They start the real daemon and manager and write to /data, /mnt, /user: use Linux with root in a VM made
# for this (see the Tests section of README.md). Prepare once: sudo tests/setup-env.sh --install-chrome, ./build.sh, tests/build/build-test-binaries.sh.
#
#   scripts/test.sh list                     the areas and their suites
#   scripts/test.sh smoke                    a quick check (7 suites)
#   scripts/test.sh full                     all Python suites (about 30 minutes)
#   scripts/test.sh <area>...                core, sleep, webconfig, folders, network, syssvc, language
#   scripts/test.sh suite <test_x.py>...     single suites by file name, e.g. suite test_piece_order.py
#   scripts/test.sh pages [segment...]       the web page tests (Node: jsdom and Chrome); segments: settings daemon-card sleep
#                                            webui effects language webui-language (default: the first five)
# Results: /tmp/ps5tm-test-suites.txt (a line per suite, the slowest at the end), /tmp/ps5tm-test-timing.txt, /tmp/ps5tm-test-pages.txt (pages).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
SUITES="$ROOT/tests/suites"
declare -A AREA=(
  [core]="core/test_daemon_lifecycle.py core/test_power_state.py core/test_dns_does_not_block.py core/test_system_messages.py core/test_hung_daemon_restart.py core/test_no_restart_without_network.py core/test_config_load.py core/test_settings_port.py core/test_disk_space_thresholds.py core/test_restart_delay_migration.py core/test_network_change_restart.py"
  [sleep]="sleep/test_sleep_now.py sleep/test_autosleep.py sleep/test_autosleep_timeout_setting.py sleep/test_sleep_actions.py"
  [webconfig]="webconfig/test_disk_panel_modes.py"
  [folders]="folders/test_download_folders_guard.py folders/test_start_verify_disk_check.py folders/test_forced_settings.py folders/test_default_folder_placeholder.py folders/test_sequential_download_on_usb.py folders/test_state_saved_on_add.py folders/test_folders_rpc.py folders/test_in_folder_field.py folders/test_path_aliases_and_unmount.py folders/test_folder_browser.py folders/test_disk_kind_and_unmount.py"
  [network]="network/test_piece_order.py network/test_uneven_swarm.py network/test_far_pieces_window.py network/test_io_watchdog.py network/test_hang_journal.py network/test_usb_fix_per_torrent.py"
  [syssvc]="syssvc/test_system_service_and_game_pause.py"
  [language]="language/test_page_language.py language/test_console_language.py language/test_web_client_translation.py"
  [smoke]="core/test_daemon_lifecycle.py core/test_config_load.py webconfig/test_disk_panel_modes.py folders/test_download_folders_guard.py folders/test_folders_rpc.py syssvc/test_system_service_and_game_pause.py language/test_page_language.py"
)
AREAS="core sleep webconfig folders network syssvc language"

run_suites() {   # relative suite paths...; returns 1 if any suite failed (a failed check, a crash or a timeout)
    local res=/tmp/ps5tm-test-suites.txt tim=/tmp/ps5tm-test-timing.txt failed=0
    : > "$res"; : > "$tim"
    for t in "$@"; do
        local name; name="$(basename "$t" .py)"
        pkill -9 -x Transmission.el 2>/dev/null; pkill -9 -x transmission_d. 2>/dev/null; sleep 1
        rm -rf /user/app /tmp/ps5tm_icon_* /tmp/ps5_usb1; mkdir -p /user/app
        local t0 t1 rc; t0=$(date +%s)
        timeout 600 python3 -u "$SUITES/$t" > "/tmp/ps5tm-test-$name.out" 2>&1; rc=$?
        t1=$(date +%s)
        echo "$name: $(grep -E 'checks passed' "/tmp/ps5tm-test-$name.out") (exit $rc, FAIL lines: $(grep -c '^FAIL' "/tmp/ps5tm-test-$name.out"))" >> "$res"
        echo "$((t1 - t0)) $name" >> "$tim"
        if [ "$rc" -ne 0 ] || ! grep -q 'checks passed' "/tmp/ps5tm-test-$name.out" || grep -q '^FAIL' "/tmp/ps5tm-test-$name.out"; then
            failed=1
            # what failed, right under the summary line (on a CI runner the .out file is not at hand), and the end of the output if the suite did not finish
            grep '^FAIL' "/tmp/ps5tm-test-$name.out" | cut -c1-600 | sed 's/^/    /' >> "$res"
            grep -q 'checks passed' "/tmp/ps5tm-test-$name.out" || tail -12 "/tmp/ps5tm-test-$name.out" | cut -c1-300 | sed 's/^/    | /' >> "$res"
        fi
    done
    echo "--- seconds per suite, the slowest first (total $(awk '{s+=$1} END {print s}' "$tim") s):" >> "$res"
    sort -rn "$tim" | head -10 >> "$res"
    echo "ALL DONE" >> "$res"
    cat "$res"
    return $failed
}

cmd="${1:-list}"
case "$cmd" in
    list)
        for a in $AREAS smoke; do echo "$a: ${AREA[$a]}"; done
        echo "full: all the suites of the areas above"; exit 0 ;;
    pages)
        shift; exec python3 "$ROOT/tests/run_pages.py" "$@" ;;
    suite)
        shift; list=()
        for n in "$@"; do f="$(cd "$SUITES" && find . -name "$n" | head -1)"; [ -n "$f" ] || { echo "no such suite: $n"; exit 2; }; list+=("${f#./}"); done
        run_suites "${list[@]}"; exit $? ;;
    *)
        list=""
        for a in "$@"; do
            if [ "$a" = full ]; then list=""; for x in $AREAS; do list="$list ${AREA[$x]}"; done; break; fi
            [ -n "${AREA[$a]:-}" ] || { echo "no such area: $a (scripts/test.sh list)"; exit 2; }
            list="$list ${AREA[$a]}"
        done
        run_suites $(echo $list | tr ' ' '\n' | awk '!seen[$0]++'); exit $? ;;
esac
