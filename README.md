# TimeCast-AT

This repository contains two components for synchronous flooding experiments
with RIOT OS: the `nrf_sf_radio` low-level radio driver and the TimeCast-AT
protocol built on top of it.

## Building

Place the RIOT checkout alongside this repository:

```text
workspace/
├── RIOT/
└── modules/
    ├── nrf_sf_radio/
    └── timecast_at/
```

With the RIOT build dependencies and ARM toolchain installed, run the following
from this repository's root:

```sh
make -C timecast_at BOARD=nrf52840dk -j4
```

The application Makefile locates the sibling RIOT checkout and registers
`nrf_sf_radio` as an external module. To enable the adaptive schedule, add
`TIMECAST_USE_PRE_P2=1` to the build command. Further application parameters
are defined in [timecast_at/Makefile](timecast_at/Makefile).

## License

The project's own source files are licensed under `LGPL-2.1-only`.
See [LICENSE](LICENSE) for the full license text. Third-party sources retain
their respective copyright notices and license terms.
