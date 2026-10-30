# TimeCast-AT Code Reference

## [main.c](main.c)

### File Purpose

### Functions

#### `_round_period_ticks()` (`static`)

#### `_is_master()` (`static`)

#### `_format_class_map()` (`static`)

#### `_local_data_len_for_source()` (`static`)

#### `_build_local_data_with_len()` (`static`)

#### `_apply_round_schedule()` (`static`)

#### `_local_data_init()` (`static`)

#### `_load_local_desired_data_for_p2()` (`static`)

#### `_load_local_scheduled_data_for_p2()` (`static`)

#### `_elapsed_since_ticks()` (`static`)

#### `_log_p2_success_rate()` (`static`)

#### `_log_round_summary()` (`static`)

#### `_handle_p1_rx()` (`static`)

#### `_handle_p2_rx()` (`static`)

#### `_scan_until_reference()` (`static`)

#### `_run_p1_slot()` (`static`)

#### `_run_p2_subslot()` (`static`)

#### `_wait_until_round_end()` (`static`)

#### `_prepare_local_data()` (`static`)

#### `_run_p1_phase()` (`static`)

#### `_run_round()` (`static`)

#### `main()`

## [protocol.c](protocol.c)

### File Purpose

### Functions

#### `_tx_first()` (`static`)

#### `_p2_build_schedule()` (`static`)

#### `_class_span_bytes()` (`static`)

#### `protocol_init()`

#### `p1_start()`

#### `p1_get_slot_ticks()`

#### `p1_get_slot_start_local_ticks()`

#### `p1_prepare_tx()`

#### `p1_handle_rx()`

#### `p1_finish_slot()`

#### `_p2_start_common()` (`static`)

#### `p2_start_original()`

#### `p2_start_at()`

#### `p2_get_subslot_start_local_ticks()`

#### `p2_get_slot_ticks()`

#### `p2_prepare_tx()`

#### `p2_handle_rx()`

#### `p2_finish_subslot()`

#### `prepare_round()`

#### `frame_len_to_class()`

#### `class_to_frame_len()`

#### `master_track_p2_completeness()`

## [include/protocol.h](include/protocol.h)

### File Purpose

### Function Declarations

#### `protocol_init()`

#### `p1_start()`

#### `p1_get_slot_ticks()`

#### `p1_get_slot_start_local_ticks()`

#### `p1_prepare_tx()`

#### `p1_handle_rx()`

#### `p1_finish_slot()`

#### `p2_start_original()`

#### `p2_start_at()`

#### `p2_get_subslot_start_local_ticks()`

#### `p2_get_slot_ticks()`

#### `p2_prepare_tx()`

#### `p2_handle_rx()`

#### `p2_finish_subslot()`

#### `prepare_round()`

#### `frame_len_to_class()`

#### `class_to_frame_len()`

#### `master_track_p2_completeness()`

## [packet.c](packet.c)

### File Purpose

### Functions

#### `_u32_to_le()` (`static`)

#### `_u32_from_le()` (`static`)

#### `encode_p1_sync()`

#### `decode_p1_sync()`

#### `encode_p2_data()`

#### `decode_p2_data()`

## [include/packet.h](include/packet.h)

### File Purpose

### Function Declarations

#### `encode_p1_sync()`

#### `decode_p1_sync()`

#### `encode_p2_data()`

#### `decode_p2_data()`

## [store.c](store.c)

### File Purpose

### Functions

#### `store_import()`

#### `store_has_data()`

#### `store_present_count()`

## [include/store.h](include/store.h)

### File Purpose

### Function Declarations

#### `store_import()`

#### `store_has_data()`

#### `store_present_count()`

## [Makefile](Makefile)

### File Purpose
