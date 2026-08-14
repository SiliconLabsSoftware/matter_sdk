#!/usr/bin/env bash
# End-to-end validation for the wake-on-matter NCP setup.
#
# Steps:
#   1. Run verify_install.sh (services + wpan0 up).
#   2. Fetch the active OT dataset and commission a Matter accessory over
#      BLE-Thread via chip-tool.
#   3. Commission the Matter NCP via mmic_host and verify with matter_state.
#   4. Write an ACL on the accessory so it accepts operational CASE from the
#      NCP nodeId.
#   5. Establish a subscription from the NCP toward the accessory.
#
# All configurable values can be overridden via env vars (see CONFIG block).
# The script fails fast: any step non-zero aborts with a summary.

set -uo pipefail

# ----- CONFIG ---------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERIFY_SCRIPT="${VERIFY_SCRIPT:-$SCRIPT_DIR/verify_install.sh}"

CHIP_TOOL="${CHIP_TOOL:-chip-tool}"
MMIC_HOST="${MMIC_HOST:-mmic_host}"

# chip-tool BLE-Thread commissioning parameters (defaults match the standard
# Silabs example: setup pin 20202021, discriminator 3840).
ACCESSORY_NODE_ID="${ACCESSORY_NODE_ID:-1}"
SETUP_PINCODE="${SETUP_PINCODE:-20202021}"
DISCRIMINATOR="${DISCRIMINATOR:-3840}"

# NCP-side commissioning (via mmic_host).
NCP_NODE_ID="${NCP_NODE_ID:-100}"

# Subscription parameters (Basic Information cluster / DataModelRevision).
SUB_FABRIC_INDEX="${SUB_FABRIC_INDEX:-1}"
SUB_ENDPOINT_ID="${SUB_ENDPOINT_ID:-0}"
SUB_CLUSTER_ID="${SUB_CLUSTER_ID:-0x0028}"
SUB_ATTRIBUTE_ID="${SUB_ATTRIBUTE_ID:-0x0000}"

# Timeouts (seconds).
COMMISSION_TIMEOUT="${COMMISSION_TIMEOUT:-180}"
CHIP_TOOL_TIMEOUT="${CHIP_TOOL_TIMEOUT:-60}"
MMIC_TIMEOUT="${MMIC_TIMEOUT:-30}"

# ----- reporting ------------------------------------------------------------
declare -a STEPS
declare -a RESULTS

pass() { STEPS+=("$1"); RESULTS+=("PASS"); printf '  [PASS] %s\n' "$1"; }
fail() { STEPS+=("$1"); RESULTS+=("FAIL"); printf '  [FAIL] %s\n' "$1"; summary; exit 1; }
info() { printf '  ..    %s\n' "$1"; }

summary() {
    echo
    echo "======== End-to-end validation summary ========"
    for i in "${!STEPS[@]}"; do
        printf '  [%s] %s\n' "${RESULTS[$i]}" "${STEPS[$i]}"
    done
    echo "==============================================="
}

trap 'echo; echo "Interrupted."; summary; exit 130' INT TERM

require_bin() {
    command -v "$1" >/dev/null 2>&1 || fail "Required binary not found: $1"
}

# ----- Step 1: verify_install.sh -------------------------------------------
echo ">> Step 1: verify_install.sh"
[[ -x "$VERIFY_SCRIPT" ]] || fail "verify_install.sh not executable at $VERIFY_SCRIPT"
if "$VERIFY_SCRIPT"; then
    pass "verify_install.sh completed successfully"
else
    fail "verify_install.sh reported errors (see output above)"
fi

# Preflight for remaining steps.
require_bin "$CHIP_TOOL"
require_bin "$MMIC_HOST"
require_bin ot-ctl


# ----- Step 2: mmic_host commission + matter_state -------------------------
echo ">> Step 2: Commission NCP via mmic_host + verify"

# The device serializes matter_state with "\r\n" line endings; strip CR so
# regex anchors and numeric comparisons behave as expected.
fabric_count_from() {
    # Extract the first "Number of Fabrics: <n>" occurrence, or "" if absent.
    sed -nE 's/^Number of Fabrics:[[:space:]]+([0-9]+).*$/\1/p' <<<"$1" | head -n1
}

info "Running: $MMIC_HOST matter_state (pre-check)"
PRE_STATE="$(timeout "$MMIC_TIMEOUT" "$MMIC_HOST" matter_state 2>&1 | tr -d '\r')" || {
    echo "$PRE_STATE"
    fail "mmic_host matter_state (pre-check) failed"
}
echo "$PRE_STATE" | sed 's/^/     | /'

PRE_FAB_COUNT="$(fabric_count_from "$PRE_STATE")"
if [[ -z "$PRE_FAB_COUNT" ]]; then
    fail "matter_state output missing 'Number of Fabrics' line (see dump above)"
fi

if (( PRE_FAB_COUNT > 0 )); then
    info "Device already has $PRE_FAB_COUNT fabric(s); running decommission first"
    if ! timeout "$MMIC_TIMEOUT" "$MMIC_HOST" decommission; then
        fail "mmic_host decommission failed"
    fi
    POST_DEC_STATE="$(timeout "$MMIC_TIMEOUT" "$MMIC_HOST" matter_state 2>&1 | tr -d '\r')" || {
        echo "$POST_DEC_STATE"
        fail "mmic_host matter_state (post-decommission) failed"
    }
    echo "$POST_DEC_STATE" | sed 's/^/     | /'
    POST_DEC_COUNT="$(fabric_count_from "$POST_DEC_STATE")"
    if [[ "$POST_DEC_COUNT" != "0" ]]; then
        fail "Device still reports $POST_DEC_COUNT fabric(s) after decommission"
    fi
    pass "Device decommissioned (fabric count now 0)"
else
    info "Device has no fabrics; skipping decommission"
fi

info "Running: $MMIC_HOST commission $NCP_NODE_ID"
if ! timeout "$MMIC_TIMEOUT" "$MMIC_HOST" commission "$NCP_NODE_ID"; then
    fail "mmic_host commission $NCP_NODE_ID failed"
fi

info "Running: $MMIC_HOST matter_state (post-commission)"
STATE_OUT="$(timeout "$MMIC_TIMEOUT" "$MMIC_HOST" matter_state 2>&1 | tr -d '\r')" || {
    echo "$STATE_OUT"
    fail "mmic_host matter_state failed"
}
echo "$STATE_OUT" | sed 's/^/     | /'

POST_FAB_COUNT="$(fabric_count_from "$STATE_OUT")"
if [[ -z "$POST_FAB_COUNT" || "$POST_FAB_COUNT" -lt 1 ]]; then
    fail "matter_state reports zero fabrics after commission"
fi

NCP_HEX="$(printf '0x%016x' "$NCP_NODE_ID")"
if ! grep -Fq "nodeId=$NCP_HEX" <<<"$STATE_OUT"; then
    fail "matter_state fabric nodeId does not match $NCP_HEX"
fi
pass "NCP commissioned (nodeId=$NCP_HEX visible in matter_state, $POST_FAB_COUNT fabric(s))"

# ----- Step 3: chip-tool pairing ble-thread --------------------------------
echo ">> Step 3: Commission accessory over BLE-Thread"
info "Reading active OT dataset via 'ot-ctl dataset active -x'"
# ot-ctl typically emits:
#   <hex-string>
#   Done
# Strip CR, ignore any non-hex lines (Done, blanks, warnings), take first match.
DATASET_HEX="$(ot-ctl dataset active -x 2>&1 \
    | tr -d '\r' \
    | grep -oiE '^[0-9a-f]{20,}$' \
    | head -n1)" || true
if [[ -z "${DATASET_HEX:-}" ]]; then
    fail "Failed to read active OT dataset (no hex line found; check 'ot-ctl dataset active -x' manually)"
fi
info "Dataset length: ${#DATASET_HEX} hex chars"

info "Running: $CHIP_TOOL pairing ble-thread $ACCESSORY_NODE_ID hex:<dataset> $SETUP_PINCODE $DISCRIMINATOR"
if timeout "$COMMISSION_TIMEOUT" "$CHIP_TOOL" pairing ble-thread \
        "$ACCESSORY_NODE_ID" "hex:$DATASET_HEX" \
        "$SETUP_PINCODE" "$DISCRIMINATOR"; then
    pass "chip-tool commissioned accessory nodeId=$ACCESSORY_NODE_ID"
else
    fail "chip-tool pairing ble-thread failed (exit $?)"
fi

# ----- Step 4: chip-tool accesscontrol write acl ---------------------------
echo ">> Step 4: Grant NCP CASE access on accessory via ACL"
ACL_JSON="$(printf '[{"fabricIndex": 1, "privilege": 1, "authMode": 2, "subjects": [%d], "targets": null}]' "$NCP_NODE_ID")"
info "Running: $CHIP_TOOL accesscontrol write acl '<json>' $ACCESSORY_NODE_ID 0"
if timeout "$CHIP_TOOL_TIMEOUT" "$CHIP_TOOL" accesscontrol write acl \
        "$ACL_JSON" "$ACCESSORY_NODE_ID" 0; then
    pass "ACL written on accessory (subjects=[$NCP_NODE_ID])"
else
    fail "chip-tool accesscontrol write acl failed"
fi

# ----- Step 5: mmic_host establish_subscription ----------------------------
echo ">> Step 5: Establish subscription from NCP to accessory"
info "Running: $MMIC_HOST establish_subscription $SUB_FABRIC_INDEX $ACCESSORY_NODE_ID $SUB_ENDPOINT_ID $SUB_CLUSTER_ID $SUB_ATTRIBUTE_ID"
if timeout "$MMIC_TIMEOUT" "$MMIC_HOST" establish_subscription \
        "$SUB_FABRIC_INDEX" "$ACCESSORY_NODE_ID" \
        "$SUB_ENDPOINT_ID" "$SUB_CLUSTER_ID" "$SUB_ATTRIBUTE_ID"; then
    pass "Subscription established"
else
    fail "mmic_host establish_subscription failed"
fi

summary
exit 0
