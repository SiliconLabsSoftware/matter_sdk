# Zephyr generator firmware

Minimal Zephyr application for CSR provisioning on EFR32 Series 2 and SiWx917
devices that runs Silicon Labs Matter Protocol 2 over SEGGER RTT and writes
factory data through `ProvisionStorageZephyr`.

## What is different

-   `xg24_gfw` and `xg26_gfw` use the smallest part with Secure Vault High in
    each family and enable only the features needed for provisioning. The image
    can therefore be shared across that family.
-   Provisioning clients select this firmware with `--rtos zephyr`.
-   Zephyr has no button sequence for entering provisioning mode. The firmware
    stays in provisioning mode because its NVS is created after the first
    provisioning packet supplies the storage address and size.
-   The host provides the production app's NVS region. Zephyr receives the
    address and size at runtime and derives the sector count from the flash
    erase size. The small `storage` partition in the board overlay exists only
    to give the linker a valid layout; it is not necessarily used at runtime.

## Zephyr patch

The GFW requires the patches listed in `patches/patches.yml`. It lets the
firmware initialize Settings/NVS after the first provisioning packet.

Normally, Zephyr initializes Settings and PSA secure storage during startup. The
patch defers that initialization so the provisioning code can first set the NVS
address and size received from the host. This allows the GFW to use the
production app's storage region instead of the board overlay's placeholder.

**SiWx917 prerequisite:** flash the WiseConnect **NWP** connectivity firmware
once (Zephyr flash driver selects NWP; without it boot hangs in `sl_wifi_init`):

```bash
west blobs fetch hal_silabs   # if blobs missing
commander flash \
  $ZEPHYR_BASE/../modules/hal/silabs/zephyr/blobs/wiseconnect/connectivity_firmware/standard/SiWG917-*.rps \
  --device Si917
```

`provision.py --rtos zephyr` will also try to flash this automatically when the
blob is found (or set `SIWX917_NWP_FIRMWARE`).

Board definitions live under `boards/`.

Apply before building the GFW:

```bash
west patch \
  -b /path/to/matter_sdk/examples/generator-firmware/silabs/zephyr/patches \
  -l /path/to/matter_sdk/examples/generator-firmware/silabs/zephyr/patches/patches.yml \
  -w /path/to/silabs_zephyr \
  apply
```

Do this only when building the GFW locally. Revert the patch before building a
normal Zephyr application in the same workspace. If a Zephyr update breaks the
patch, update the patch and its checksum in `patches.yml`.

## Build and provision

From a Silabs Zephyr workspace with CHIP pigweed environment:

```bash
west build -b xg24_gfw -p always \
  /path/to/matter_sdk/examples/generator-firmware/silabs/zephyr
```

`CHIP_ROOT` defaults to the root of this repository and `PROVISION_LIBS_DIR`
defaults to `third_party/silabs/matter_support/provision/libs`. Both can be
overridden with `-DCHIP_ROOT=...` and `-DPROVISION_LIBS_DIR=...`.

Select `--rtos zephyr` in the provisioning client. The rest of the protocol is
the same as the FreeRTOS path.

Command to provision EFR:

```bash
cd client/python
python3 provision.py -i ../defaults.json -d efr32mg24 \
  -v 2.10 --rtos zephyr \
  -gf /path/to/images/zephyr_xg24_core.hex \
  -ct /path/to/connectedhomeip/out/chip-cert \
  -o /tmp/zephyr_csr_test.json --csr
```

Command to provision Si917:

```bash
python3 client/python/provision.py --rtos zephyr \
  -gf /path/to/images/zephyr_siwx917_core.rps \
  -pf /path/to/lighting-app.rps \
  ...
```

SiWx917 GFW must be an `.rps` image (`commander rps load`). After rebuilding,
copy `build/.../zephyr/zephyr.rps` to `zephyr_siwx917_core.rps`.

Provisioning keys use the `mt-fct` prefix defined in `ZephyrConfig`. Certificate
keys use the `its` prefix and are managed by PSA.
