# Getting Started

## 1. Prerequisites
This guide uses a Linux host and two nRF52-based development boards connected to the host via USB.
After flashing the firmware, you can monitor each board’s serial output on the host to view information about data transmission and reception.
## 2. Install Dependencies
On Ubuntu 24.04, install the Arm GNU Toolchain and the supporting tools needed to build the firmware and access the serial ports:
```bash
sudo apt update
sudo apt install git make \
    gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi \
    python3 python3-serial python3-psutil
```
After installation, run
```bash
arm-none-eabi-gcc --version
```
to check that the compiler is available.

If the command prints the compiler version, the compiler is installed and available in your PATH.
## 3. Clone RIOT and the Modules
This module is based on RIOT OS.
First, run
```bash
git clone https://github.com/RIOT-OS/RIOT.git
```
to clone RIOT.

Next, run
```bash
git clone https://github.com/merlins123/TimeCast-AT.git
```
in the same directory to clone TimeCast-AT repository.

The resulting directory structure should be:
```text
.
├── RIOT/
└── TimeCast-AT/
    ├── nrf_sf_radio/
    └── timecast_at/
```

## 4. Configure the Application
From the directory containing both repositories, enter the application directory:

```bash
cd TimeCast-AT/timecast_at
```

Connect both development boards to the host via USB and make sure they are powered on.
## 5. Build and Flash
Run

```bash
make UF2_SOFTDEV=DROP PORT=/dev/ttyACM0 flash term
```

to build and flash the firmware for the master board, and open the terminal

Open another terminal window, navigate to the same application directory, and run:

```bash
make UF2_SOFTDEV=DROP PORT=/dev/ttyACM1 LOCAL_NODE_ID=1 flash term
```

to build and flash the firmware for the follower board, and open the terminal


## 6. Verify
If both serial terminals display the following message such as:

```text
[tc] R=10 present=2/2
```

`R` is the local round counter. `present=2/2` means that the node has data from both nodes for that round, including its own data.

If both terminals show `present=2/2` over several consecutive rounds, the two nodes are exchanging data successfully.