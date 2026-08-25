# BLE bring-up security debt (obelix Zephyr port)

Tracked workarounds applied to get the stock CoreApp connecting over BLE on the
Zephyr port. Each cuts a real security corner and MUST be restored before ship.

## 1. Reversed PPoGATT characteristics: encryption gating removed
Files: `src/bluetooth-fw/nimble/ppog_reversed_service.c`
- Notify char (0x40000001): `F_READ_ENC` -> `F_READ`.
- Write char  (0x40000003): `F_WRITE_ENC` -> `F_WRITE_NO_RSP` (dropped ENC).

Why: on this port, under Just-Works bonding, nimble silently drops the
encrypted CCCD write (subscribe) and the encrypted write-without-response
(PPoGATT ResetRequest) -- the phone times out (CCCD subscribe timeout /
TimeoutInitializingPpog). Plain (non-ENC) permissions let both through and the
PPoGATT link comes up. The LE link is still encrypted+bonded (bonding is
required to connect), so payloads are not in the clear, but the ATT-layer
enforcement that these ops require encryption is gone -- a non-encrypted peer
could subscribe/write.

Restore path: find why nimble's att ENC-permission check rejects these ops on a
Just-Works (unauthenticated) encrypted link on this port (candidates:
min_key_size handling, sec_state.encrypted not set at check time, or the
CCCD/att flags derivation in ble_gatts.c:ble_gatts_register_clt_cfg_dsc). Once
fixed, restore F_READ_ENC / F_WRITE_ENC.

## 2. Bond store is RAM-only (not persistent)
File: `zephyr-port-apps/ble/src/ram_store.c`
Bonds are lost on reboot/reflash; requires re-pair each firmware flash. Fine for
bring-up; replace with a flash/settings-backed store for real use (pt2 has no
storage partition yet + XIP flash, so this is a deliberate later task).

## 3. Just-Works pairing, no MITM
`ble_hs_cfg.sm_mitm = 0`, `sm_io_cap = NO_INPUT_OUTPUT`. Matches phone Just-Works
but provides no MITM protection. Revisit alongside #1.
