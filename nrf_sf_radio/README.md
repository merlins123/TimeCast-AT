# nrf_sf_radio Code Reference

## [radio_driver.c](radio_driver.c)

### File Purpose

### Functions

#### `_timer2_init()` (`static`)

#### `_timer_ppi_init()` (`static`)

#### `nrf_sf_radio_wait_until()`

#### `nrf_sf_radio_wait_until_abs()`

#### `nrf_sf_radio_get_last_ready_time_ticks()`

#### `nrf_sf_radio_get_last_end_time_ticks()`

#### `nrf_sf_radio_get_last_address_time_ticks()`

#### `nrf_sf_radio_now_ticks()`

#### `_timer2_reset()` (`static`)

#### `nrf_sf_radio_tx_arm()`

#### `nrf_sf_radio_rx_arm()`

#### `nrf_sf_radio_try_rx_enable()`

#### `nrf_sf_radio_set_mode()`

#### `nrf_sf_radio_set_ble_channel()`

#### `nrf_sf_radio_set_power()`

#### `_init_radio()` (`static`)

#### `nrf_sf_radio_start()`

## [include/radio_driver.h](include/radio_driver.h)

### File Purpose

### Function Declarations

#### `nrf_sf_radio_start()`

#### `nrf_sf_radio_wait_until()`

#### `nrf_sf_radio_wait_until_abs()`

#### `nrf_sf_radio_get_last_ready_time_ticks()`

#### `nrf_sf_radio_get_last_end_time_ticks()`

#### `nrf_sf_radio_get_last_address_time_ticks()`

#### `nrf_sf_radio_now_ticks()`

#### `nrf_sf_radio_tx_arm()`

#### `nrf_sf_radio_rx_arm()`

#### `nrf_sf_radio_try_rx_enable()`

#### `nrf_sf_radio_set_mode()`

#### `nrf_sf_radio_set_ble_channel()`

#### `nrf_sf_radio_set_power()`

## [link_radio.c](link_radio.c)

### File Purpose

### Functions

#### `_encode_packet_header()` (`static`)

#### `nrf_sf_radio_tx_start()`

#### `nrf_sf_radio_rx_start()`

#### `nrf_sf_radio_rx_listen_until_packet()`

## [include/link_radio.h](include/link_radio.h)

### File Purpose

### Function Declarations

#### `nrf_sf_radio_tx_start()`

#### `nrf_sf_radio_rx_start()`

#### `nrf_sf_radio_rx_listen_until_packet()`

## [Makefile](Makefile)

### File Purpose

## [Makefile.dep](Makefile.dep)

### File Purpose

## [Makefile.include](Makefile.include)

### File Purpose
