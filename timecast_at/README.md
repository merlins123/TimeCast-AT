# TimeCast-AT

TimeCast-AT is a RIOT application for synchronous flooding experiments. It
uses the external [`nrf_sf_radio`](../nrf_sf_radio/README.md) module for scheduled
radio transmission, reception, and hardware timestamps on nRF52 devices.

The application implements round synchronization, data dissemination, and
adaptive scheduling based on each node's payload size. The current
experiment generates synthetic payloads filled with the character `c`.

## Protocol operation

Node `0` is the master and initiates each round. Other nodes receive and relay
the P1 synchronization packet to establish a local time reference. Logical node
IDs must be unique and contiguous, from `0` to `TIMECAST_P2_NODE_COUNT - 1`.
The configured node count includes the master.

Two modes are selected at build time:

| Mode | Setting | Round sequence |
|------|---------|----------------|
| Fixed schedule | `TIMECAST_USE_PRE_P2=0` | P1 synchronization, P2 data, round gap |
| Adaptive schedule (default) | `TIMECAST_USE_PRE_P2=1` | P1 synchronization with an optional schedule table, P2 data, round gap |

P2 assigns one subslot to each source node within a slot. Nodes alternate
transmit and receive slots, forwarding data already available in their local
store. P2 runs for `2 * TIMECAST_P2_NTX` slots. The store is cleared each round
and retains the first data entry accepted for each source, including local data.

In adaptive mode, payload sizes are represented by 16 classes. Each P2 packet
carries its source's desired class and schedule-update bit, which relays
preserve with the data. The master records received update requests and appends
a packed class table to the next round's P1 packet. Nodes derive their P2
subslot durations from the received classes. A P1 flag indicates whether the
table is present; carrying the table also extends each P1 slot by its airtime.

The initial class table is derived from `TIMECAST_MAX_DATA_LEN`; the master
also sets its own entry to its desired class before the first P1. The
master distributes the table in P1 on the first adaptive round, when its own
class changes, after it observes an update request, or after the configured
number of consecutive incomplete P2 rounds.
The master retains the previous class for a source without a received update.

Every P1 packet carries a schedule version. The master increments it whenever
it distributes a table, including recovery retransmissions. Followers start
without a valid schedule and must receive a table in P1 before entering P2.
On rounds without a table, a follower enters P2 only if its cached schedule is
valid and its version matches P1. A follower missing an update still relays P1
with the received version, skips P2, and resumes P2 after receiving a table.

## Source layout

| File | Responsibility |
|------|----------------|
| [main.c](main.c) | Radio calls, phase execution, experiment data, and logging |
| [protocol.c](protocol.c) | Protocol state, slot scheduling, and adaptive schedule management |
| [packet.c](packet.c) | Packet serialization and deserialization |
| [store.c](store.c) | Per-source data storage and duplicate suppression |
| [include/](include/) | Packet formats, state structures, and interfaces |
| [Makefile](Makefile) | RIOT integration and build-time configuration |

## Build and run

Install the RIOT build dependencies and ARM toolchain, and place the RIOT
checkout alongside this repository:

```text
workspace/
├── RIOT/
└── modules/
    ├── nrf_sf_radio/
    └── timecast_at/
```

The following commands run from the repository root (`modules/`). Build a
master for a four-node experiment:

```sh
make -C timecast_at BOARD=nrf52840dk LOCAL_NODE_ID=0 \
    TIMECAST_P2_NODE_COUNT=4 -j4
```

To select fixed scheduling instead:

```sh
make -C timecast_at BOARD=nrf52840dk LOCAL_NODE_ID=0 \
    TIMECAST_P2_NODE_COUNT=4 TIMECAST_USE_PRE_P2=0 -j4
```

The firmware is generated at `bin/nrf52840dk/timecast_at.elf` inside this
application directory. Repeat the build for IDs `1`, `2`, and `3` for the
followers. Each build uses the same output path, so flash or copy each node's
firmware before building the next one.

For a locally connected board, the corresponding adaptive master can be built
and flashed with RIOT's board-specific flashing tools:

```sh
make -C timecast_at BOARD=nrf52840dk LOCAL_NODE_ID=0 \
    TIMECAST_P2_NODE_COUNT=4 TIMECAST_USE_PRE_P2=1 all flash
make -C timecast_at BOARD=nrf52840dk term
```

Use the same mode, node count, and compatible radio and timing settings across
all nodes. Flash followers with their respective IDs and start them before
starting or resetting the master. Followers wait for a P1 reference before
progressing through a round.

The application Makefile registers the sibling driver through
`EXTERNAL_MODULE_DIRS`. If the RIOT checkout is elsewhere, pass
`RIOTBASE=/absolute/path/to/RIOT` to `make`. 

## Configuration

The main Makefile variables are listed below. They can be overridden on the
`make` command line.

| Variable | Default | Meaning |
|----------|---------|---------|
| `LOCAL_NODE_ID` | `0` | Logical node ID; `0` selects the master |
| `TIMECAST_P2_NODE_COUNT` | `4` | Total participating nodes, including the master |
| `TIMECAST_STORE_MAX_NODES` | Node count | Number of per-source store entries |
| `TIMECAST_USE_PRE_P2` | `1` | Adaptive scheduling; select fixed scheduling with `0` |
| `TIMECAST_NTX` | `5` | P1 repetition parameter; the phase has `2 * NTX` slots |
| `TIMECAST_P2_NTX` | `10` | P2 repetition parameter; phase length is `2 * P2_NTX` slots |
| `TIMECAST_APP_DATA_LEN` | `75` | Desired application data length in bytes |
| `TIMECAST_MAX_DATA_LEN` | `75` | Application data length used to initialize the adaptive class table |
| `TIMECAST_ROUND_TIME` | `1050` | Local round-loop limit |
| `TIMECAST_FAST_RAMPUP` | `1` | Select 40 us radio ramp-up; `0` selects 140 us |
| `TIMECAST_MASTER_P2_INCOMPLETE_PRE_THRESHOLD` | `10` | Consecutive incomplete master rounds before requesting an update; `0` disables this trigger |


Keep the store capacity at least as large as the node count. The current
store supports up to 110 application data bytes per source. Set
`TIMECAST_MAX_DATA_LEN` consistently across nodes, within this capacity and at
least as large as every node's initial payload. In fixed mode, the configured
P2 subslot must accommodate every node's payload. In adaptive mode, a larger
desired payload waits for a suitable schedule received in P1. The application
retains cached data that fits the scheduled capacity; otherwise, it limits
the transmitted cached data to the fitting prefix.

The [Makefile](Makefile) also exposes processing margins, phase guards,
receive windows, and slot durations. Defaults derive airtime at BLE 1 Mbit/s
and convert microseconds to the driver's 16 MHz timer ticks. Repetition
parameters describe the schedule, not measured successful transmission counts.

The application currently selects BLE channel `24` and TX power `+4 dBm` in
`main.c` after driver initialization. These override the driver's channel and
power defaults and are not application Makefile options. See the
[driver configuration](../nrf_sf_radio/README.md#configuration) for its access
address and other radio settings.

## Hardware requirements

The current radio backend targets nRF52 devices. It directly uses RADIO,
TIMER2, TIMER3, and PPI channels 9 through 15. Other enabled components must
not configure these resources concurrently. See the
[driver resource allocation](../nrf_sf_radio/README.md#hardware-resource-usage).
Compilation verifies source integration; synchronization accuracy and
data dissemination performance require measurements on the target boards.

## License

The application's source files are licensed under `LGPL-2.1-only`.
See [LICENSE](../LICENSE) for the full license text.
