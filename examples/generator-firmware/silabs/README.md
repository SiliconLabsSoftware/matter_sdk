# Matter Generator Firmware (Silicon Labs)

The Generator Firmware (GFW) is a small application that is flashed temporarily
on a Silicon Labs device during manufacturing. It is driven by the Silicon Labs
provisioning tool over SEGGER RTT and:

-   generates the DAC key pair on the device and returns a CSR,
-   writes the provided factory data (certificates, commissioning data,
    attestation data) into the storage used by the production application.

The provisioning protocol itself is implemented by the Provision Core library
(`libProvision-*.a`), published with its headers in the `matter_support`
repository under `provision/libs` and `provision/headers`. This folder only
contains the application glue.

## Layout

| Path       | Content                                                     |
| ---------- | ----------------------------------------------------------- |
| `include/` | Application and configuration headers                       |
| `src/`     | FreeRTOS application (EFR32, SiWx917, TrustZone secure side) |
| `zephyr/`  | Zephyr application, custom boards and required Zephyr patches |

## Building

### Simplicity SDK (SLC)

The SLC project files live in the Silicon Labs Matter extension under
`slc/apps/generator_firmware/`:

-   `efr32/generator_firmware_efr32.slcp`: EFR32 Series 2 and Series 3
-   `si917/generator_firmware_si917.slcp`: SiWx917 SoC
-   `trustzone/generator_firmware_trustzone.slcw`: EFR32 with TrustZone

Example:

```bash
slc generate -p slc/apps/generator_firmware/efr32/generator_firmware_efr32.slcp \
    -d out/generator_firmware/efr32mg24 --with EFR32MG24A010F1024IM48
make -C out/generator_firmware/efr32mg24 -f generator.Makefile
```

For SiWx917, convert the output to an `.rps` image with
`commander rps create --app generator.s37 si917_core.rps`.

### Zephyr

See [zephyr/README.md](zephyr/README.md).

## Prebuilt images

Prebuilt images are published by the Silicon Labs Matter extension CI on
Artifactory. Their names match the entries expected by the provisioning tool
`devices.yaml` (for example `efr32mg24_core.s37`, `si917_core.rps`,
`zephyr_xg24_core.hex`).
