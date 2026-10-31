# nrf_sf_radio Code Reference

## [radio_driver.c](radio_driver.c)

### File Purpose
Initializes and configures the RADIO, TIMER2, TIMER3, and PPI peripherals for scheduled transmission and reception and radio event timestamp capture. Also provides interfaces for radio parameter configuration, reading captured timestamps, and waiting for events with timeouts.
### Functions

#### `_timer2_init()` (`static`)
Configures TIMER2 and the related PPI so that a TIMER3 compare event starts TIMER2, and a TIMER2 compare event then triggers RADIO.START to control the delay before transmission or reception starts.
#### `_timer_ppi_init()` (`static`)
Configures TIMER3 parameters and the PPI connections for TIME3 to trigger RADIO TX_EN/RX_EN and capture timestamps of various RADIO events. Also calls _timer2_init.
#### `nrf_sf_radio_wait_until()`
Busy-wait function that waits until an event occurs or the specified duration has elapsed. Time is measured in ticks.
#### `nrf_sf_radio_wait_until_abs()`
Busy-wait function that waits until an event occurs or a specified time point is reached. Time is measured in ticks.
#### `nrf_sf_radio_get_last_ready_time_ticks()`
Reads the time of the latest EVENTS_READY occurrence captured by TIME3, in ticks.
#### `nrf_sf_radio_get_last_end_time_ticks()`
Reads the time of the latest EVENTS_END occurrence captured by TIME3, in ticks.
#### `nrf_sf_radio_get_last_address_time_ticks()`
Reads the time of the latest EVENTS_ADDRESS occurrence captured by TIME3, in ticks.
#### `nrf_sf_radio_now_ticks()`
Reads the current time in ticks.
#### `_timer2_reset()` (`static`)
Prepares TIMER2 for delay timing again, requests that RADIO be disabled, and clears related events to prepare for the next transmission or reception.
#### `nrf_sf_radio_tx_arm()`
Configures the transmit buffer and timed trigger so that RADIO starts transmission ramp-up at the specified TIMER3 time.
#### `nrf_sf_radio_rx_arm()`
Configures the receive buffer and timed trigger so that RADIO starts reception ramp-up at the specified TIMER3 time.
#### `nrf_sf_radio_try_rx_enable()`
Prepares the receive buffer, directly triggers RADIO.RXEN, and starts TIMER2 without waiting for the scheduled TIMER3 compare time.
#### `nrf_sf_radio_set_mode()`
Configures the RADIO mode.
#### `nrf_sf_radio_set_ble_channel()`
Configures the Bluetooth channel of RADIO.
#### `nrf_sf_radio_set_power()`
Configures the RADIO TX power.
#### `_init_radio()` (`static`)
Uses Nimble initialization functions to initialize the RADIO configuration, then adjusts it.
#### `nrf_sf_radio_start()`
Calls _init_radio and _timer_ppi_init for initialization.

## [link_radio.c](link_radio.c)

### File Purpose
Connects the upper layer protocol to the underlying radio driver, encapsulates transmit frames, calls the underlying interfaces for scheduled transmission and reception, checks transmission and reception timeouts and reception CRC status, and returns transmission and reception results to the upper layer.
### Functions

#### `_encode_packet_header()` (`static`)
Adds the required header fields, including S0, LENGTH, the S1 placeholder, and the Advertising Address, to the upper layer data.
#### `nrf_sf_radio_tx_start()`
Adds a header to the upper layer data, calls the underlying interface for scheduled transmission, and waits for transmission to complete; returns true on success and false on timeout.
#### `nrf_sf_radio_rx_start()`
Performs scheduled reception; returns 0 on success, 1 on an ADDRESS event timeout, 2 on an END event timeout, and 3 on a CRC error.
#### `nrf_sf_radio_rx_listen_until_packet()`
Immediately starts the reception process and waits for a packet; returns the corrected ADDRESS timestamp in ticks when reception completes and the CRC check passes; returns 0 on timeout or a CRC error.
