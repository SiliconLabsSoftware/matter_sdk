#!/usr/bin/env bash
# Verify that cpcd.service and otbr-agent.service are installed and running,
# and that the wpan0 interface is up. If wpan0 is missing but an OT dataset is
# active, try to bring the interface up. Prints a checklist at the end.

set -u

# ----- state tracked for the final checklist ---------------------------------
declare -A STATUS   # human label -> "ok" | "fail" | "warn" | "skip"
declare -a ORDER    # preserve print order

record() {
    local label="$1"
    local state="$2"
    STATUS["$label"]="$state"
    ORDER+=("$label")
}

# ----- helpers ---------------------------------------------------------------
service_present() {
    # 0 if unit file is known to systemd (any state), non-zero otherwise.
    systemctl list-unit-files --no-legend --no-pager "$1" 2>/dev/null | grep -q "^$1"
}

service_active() {
    systemctl is-active --quiet "$1"
}

wpan_up() {
    # `ifconfig` (net-tools) only lists interfaces that are administratively
    # up, so appearance of wpan0 in its output means the interface is up.
    if ! command -v ifconfig >/dev/null 2>&1; then
        return 2
    fi
    ifconfig 2>/dev/null | grep -qE '^wpan0[[:space:]:]'
}

# ot-ctl needs elevated privileges. Non-zero exit if not present or errored.
otctl() {
    sudo ot-ctl "$@" 2>/dev/null
}

# Verbose variant: captures both stdout and stderr into $OTCTL_OUT and
# publishes the exit status in $OTCTL_RC. Never suppressed, so callers can
# print the raw output for diagnosis. Uses globals because bash can't return
# multi-line strings cleanly.
otctl_capture() {
    OTCTL_OUT="$(sudo ot-ctl "$@" 2>&1)"
    OTCTL_RC=$?
}

dataset_active() {
    # Probe with the raw-hex form: version-independent across OT builds and
    # avoids depending on the parsed-output label ("Active Timestamp", etc.).
    otctl_capture dataset active -x
    # A valid dataset appears as a hex line >= 20 chars (in practice ~200).
    printf '%s\n' "$OTCTL_OUT" | tr -d '\r' | grep -qiE '^[0-9a-f]{20,}$'
}

# ----- checks ----------------------------------------------------------------
echo ">> Checking cpcd.service"
if service_present cpcd.service; then
    if service_active cpcd.service; then
        record "cpcd.service present and active" ok
    else
        record "cpcd.service present but NOT active" fail
    fi
else
    record "cpcd.service NOT installed" fail
fi

echo ">> Checking otbr-agent.service"
if service_present otbr-agent.service; then
    if service_active otbr-agent.service; then
        record "otbr-agent.service present and active" ok
    else
        record "otbr-agent.service present but NOT active" fail
    fi
else
    record "otbr-agent.service NOT installed" fail
fi

# Only proceed to the wpan0/dataset check if both services exist. Their active
# state is orthogonal; a stopped service will surface as a fail above.
both_services_present=false
if service_present cpcd.service && service_present otbr-agent.service; then
    both_services_present=true
fi

if $both_services_present; then
    echo ">> Checking wpan0 interface"
    wpan_up
    rc=$?
    if [[ $rc -eq 2 ]]; then
        record "ifconfig not installed (install 'net-tools')" fail
    elif [[ $rc -eq 0 ]]; then
        record "wpan0 present in ifconfig output" ok
    else
        echo "   wpan0 not present in ifconfig, running bring-up sequence via ot-ctl..."
        if ! command -v ot-ctl >/dev/null 2>&1; then
            record "ot-ctl not available on PATH" fail
        else
            # `ot-ctl dataset active` can hang or return stale data while wpan0
            # is down, so run the reset sequence unconditionally first.
            reset_ok=true
            reset_failed_step=""
            for step in "ifconfig down" "ifconfig up" "thread start"; do
                echo "   \$ sudo ot-ctl $step"
                # Word-split intentional so "ifconfig down" -> two args.
                # shellcheck disable=SC2086
                otctl_capture $step
                printf '%s\n' "${OTCTL_OUT:-<empty>}" | sed 's/^/     | /'
                if [[ ${OTCTL_RC:-1} -ne 0 ]]; then
                    reset_ok=false
                    reset_failed_step="$step"
                    break
                fi
            done

            if $reset_ok; then
                # Give the stack a moment to attach and expose the netif.
                sleep 2
                if wpan_up; then
                    record "wpan0 brought up via ot-ctl reset sequence" ok
                else
                    record "wpan0 still missing from ifconfig after ot-ctl reset" fail
                fi
            else
                # Probe for a dataset so we can tell "no dataset" apart from
                # other RCP errors and print a helpful hint.
                echo "   Probing dataset to diagnose failure..."
                echo "   \$ sudo ot-ctl dataset active -x"
                if dataset_active; then
                    printf '%s\n' "${OTCTL_OUT:-<empty>}" | sed 's/^/     | /'
                    record "ot-ctl '$reset_failed_step' failed despite active dataset" fail
                else
                    printf '%s\n' "${OTCTL_OUT:-<empty>}" | sed 's/^/     | /'
                    record "No active OT dataset (create one, e.g. 'sudo ot-ctl dataset init new' + 'commit active')" fail
                fi
            fi
        fi
    fi
else
    record "wpan0 / dataset check skipped (missing service)" skip
fi

# ----- checklist -------------------------------------------------------------
echo
echo "======== Verification checklist ========"
overall=0
for label in "${ORDER[@]}"; do
    case "${STATUS[$label]}" in
        ok)   printf '  [OK]   %s\n' "$label" ;;
        fail) printf '  [FAIL] %s\n' "$label"; overall=1 ;;
        warn) printf '  [WARN] %s\n' "$label" ;;
        skip) printf '  [SKIP] %s\n' "$label" ;;
    esac
done
echo "========================================"

exit $overall
