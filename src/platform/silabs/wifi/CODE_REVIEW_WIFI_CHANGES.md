# Code Review: Wi-Fi Platform Changes

## Summary of Changes

### 1. Scan and join split (WifiInterfaceImpl)
- **Events**: Added `kStationStartScan` (5); renumbered `kStationStartJoin` (6), `kConnectionComplete` (7), etc.
- **Flow**: `ConnectToAccessPoint()` posts `kStationStartScan` -> ProcessEvent runs high-perf (ICD), `InitiateScan()`, then posts `kStationStartJoin` -> ProcessEvent runs `JoinWifiNetwork()` only.
- **Effect**: Scan and join are separate events; join runs only after scan completes.

### 2. Trigger disconnection via event (WifiInterfaceImpl)
- **TriggerDisconnection()**: Now only posts `kStationDisconnect` and returns `CHIP_NO_ERROR` (no direct platform call).
- **kStationDisconnect handler**: Calls `TriggerPlatformWifiDisconnection()` at the start, then clears state and notifies IP/connectivity.
- **Effect**: Disconnect is asynchronous and runs on the Wi-Fi task.

### 3. QuickJoinToAccessPoint API (WifiInterface + WifiInterfaceImpl)
- **New API**: `QuickJoinToAccessPoint()` validates provisioned state, then posts `kStationStartJoin` only (no scan).
- **Use case**: When channel/BSSID is already known to avoid a prior scan.

### 4. Credential validation in SetWifiCredentials (WifiInterfaceImpl + interface)
- **SetWifiCredentials**: Now returns `CHIP_ERROR`; validates `ssidLength > 0` and `ssidLength <= WFX_MAX_SSID_LENGTH` before storing.
- **ConnectToAccessPoint / QuickJoinToAccessPoint**: Removed redundant ssidLength checks (rely on validation at set time).
- **Call site**: NetworkCommissioningWiFiDriver uses `ReturnErrorOnFailure(SetWifiCredentials(wifiConfig))`.

### 5. Retry with scan+join on specific join failure (WifiInterfaceImpl)
- **JoinWifiNetwork** (sync failure): On failure, if `status == SL_STATUS_SI91X_NO_AP_FOUND` schedules retry with scan+join; otherwise schedules default retry.
- **JoinCallback**: Calls `ScheduleConnectionAttempt()` with no argument (default).
- **Note**: Earlier design used 0x10003 and `mJoinAttemptIsQuickJoin` so only quick-join attempts would fall back to scan+join; current code uses `SL_STATUS_SI91X_NO_AP_FOUND` for all join paths.

### 6. ScheduleConnectionAttempt(quickJoin) and timer (WifiInterface)
- **ScheduleConnectionAttempt(bool quickJoin = false)**: `quickJoin == true` -> next retry uses quick join; `quickJoin == false` -> next retry uses scan then join (ConnectToAccessPoint).
- **Timer**: Intended to pass the quickJoin value via the timer argument (e.g. timer created with `&mRetryTimerQuickJoin`, callback reads `*static_cast<bool*>(arg)`).

---

## Issues and Inconsistencies

### 1. NO_AP_FOUND retry mode (likely bug)
**File**: `WifiInterfaceImpl.cpp` (JoinWifiNetwork failure path)

```cpp
if (status == SL_STATUS_SI91X_NO_AP_FOUND)
{
    ScheduleConnectionAttempt(true);   // -> next retry = quick join
}
else
{
    ScheduleConnectionAttempt(false);   // -> next retry = scan + join
}
```

When the AP is not found, the next retry should use **scan then join**, not quick join. So for `SL_STATUS_SI91X_NO_AP_FOUND` it should be `ScheduleConnectionAttempt(false)`.

**Suggested fix**: Use `ScheduleConnectionAttempt(false)` when `status == SL_STATUS_SI91X_NO_AP_FOUND`, and `ScheduleConnectionAttempt(true)` (or no arg) in the `else` branch.

### 2. Timer callback still using global (WifiInterface.cpp)
**Current**: A file-scope `bool mWiFiQuickJoin` is set in `ScheduleConnectionAttempt(quickJoin)` and read in `RetryConnectionTimerHandler`. The timer is created with `this`, but the callback does not use `arg`.

**Intended design**: Pass `&mRetryTimerQuickJoin` when creating the timer; in `ScheduleConnectionAttempt(quickJoin)` set `mRetryTimerQuickJoin = quickJoin`; in the callback read `*static_cast<bool*>(arg)`. No global.

**Suggested fix**: Remove the global; add private member `bool mRetryTimerQuickJoin` in `WifiInterface`; create timer with `&mRetryTimerQuickJoin`; in the callback use `bool quickJoin = *static_cast<bool*>(arg);` and `GetInstance()` for the interface.

### 3. Timer failed-to-start fallback (WifiInterface.cpp)
**Current**: When `osTimerStart` fails, the code always calls `ConnectToAccessPoint()`.

**Consideration**: For consistency with the scheduled retry, the fallback could use the same `quickJoin` value just set (e.g. if `mRetryTimerQuickJoin` then `QuickJoinToAccessPoint()`, else `ConnectToAccessPoint()`). Optional improvement.

### 4. JoinCallback and quick-join vs scan+join retry
**Current**: `JoinCallback` on failure always calls `ScheduleConnectionAttempt()` with no argument (default quick join).

If the intent is “only when the attempt was quick-join and failed with AP-not-found, retry with scan+join”, then you need to track whether the current join was from quick join (e.g. `mJoinAttemptIsQuickJoin` set in `QuickJoinToAccessPoint`, cleared in scan path and on success/failure), and in JoinCallback when `status == SL_STATUS_SI91X_NO_AP_FOUND` (or 0x10003) and `mJoinAttemptIsQuickJoin` call `ScheduleConnectionAttempt(false)`. Otherwise JoinCallback’s behavior is consistent with “always default retry (quick join).”

### 5. ScheduleConnectionAttempt default parameter
**Header**: `void ScheduleConnectionAttempt(bool quickJoin = false);`

Default `false` means “next retry = scan and join.” If the preferred default is “next retry = quick join” (to match previous behavior), the default should be `true`.

---

## What Looks Good

- Clear split between scan and join events and use of `kStationStartScan` / `kStationStartJoin`.
- TriggerDisconnection fully event-driven; platform disconnect in one place (kStationDisconnect).
- SetWifiCredentials validation and return value; callers simplified.
- QuickJoinToAccessPoint and ConnectToAccessPoint have clear, separate roles.
- Event enum renumbering is consistent.

---

## Suggested Next Steps

1. Fix NO_AP_FOUND retry: use `ScheduleConnectionAttempt(false)` when `status == SL_STATUS_SI91X_NO_AP_FOUND`.
2. Remove the global in WifiInterface.cpp and pass quickJoin via the timer argument as designed.
3. Decide default for `ScheduleConnectionAttempt(bool quickJoin)` and document it.
4. Optionally: add quick-join tracking and use it in JoinCallback so only quick-join failures with AP-not-found trigger scan+join retry.
