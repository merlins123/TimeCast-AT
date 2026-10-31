# TimeCast-AT Code Reference

## [main.c](main.c)

### File Purpose
The main execution flow for the TimeCast reproduction and TimeCast-AT, responsible for initialization, round execution, radio transmit/receive calls, and log output.
### Functions

#### `_round_period_ticks()` (`static`)
Calculate the round period, including the gap between rounds, to determine the scheduled start time of the next round.
#### `_is_master()` (`static`)
Determine whether the node is the master based on its node ID.
#### `_format_class_map()` (`static`)
Convert the class IDs corresponding to each node's length into a string for log output.
#### `_local_data_len_for_source()` (`static`)
Get the local node's data length.
#### `_build_local_data_with_len()` (`static`)
Fill the data according to the data length.
#### `_apply_round_schedule()` (`static`)
Update the local node's data to be sent according to the current schedule table. If the desired length does not exceed the length allowed by the schedule, refill the data and update the transmission length; otherwise, retain the existing data and shorten the transmission length if necessary.
#### `_local_data_init()` (`static`)
Initialize the scheduled class IDs of all nodes to the class ID corresponding to the maximum data length, and initialize the local node's data buffer, transmission length, and desired class ID.
#### `_load_local_desired_data_for_p2()` (`static`)
Generate data according to the local node's current desired length and write it to the P2 data store for fixed slot mode.
#### `_load_local_scheduled_data_for_p2()` (`static`)
Write the local node's data prepared according to the schedule to the P2 data store for AT mode.
#### `_elapsed_since_ticks()` (`static`)
Calculate the interval from the starting timestamp to the current timestamp for printing the elapsed P2 time.
#### `_log_p2_success_rate()` (`static`)
Calculate and print the node data collection ratio for the current round and the average ratio across the rounds included in the statistics. The statistics include the local node's data, and the average only includes rounds in which data from more than one node is stored.
#### `_log_round_summary()` (`static`)
A log output function for printing the required statistics.
#### `_handle_p1_rx()` (`static`)
Handle packets received during the P1 phase.
#### `_handle_p2_rx()` (`static`)
Handle packets received during the P2 phase.
#### `_scan_until_reference()` (`static`)
The follower listens repeatedly before synchronization until a P1 synchronization time reference is obtained or the P1 phase ends.
#### `_run_p1_slot()` (`static`)
The flow of one slot in the P1 phase.
#### `_run_p2_subslot()` (`static`)
The flow of one subslot in the P2 phase.
#### `_wait_until_round_end()` (`static`)
When P2 is not entered in AT mode, wait until the scheduled P2 end time of the current round.
#### `_prepare_local_data()` (`static`)
Update the local node's desired class ID before each round; when the master finds that its desired class ID differs from the current schedule, update the corresponding scheduled class ID and set the schedule table transmission flag.
#### `_run_p1_phase()` (`static`)
The P1 phase flow function.
#### `_run_round()` (`static`)
The round flow function.
#### `main()`
The main function.
## [protocol.c](protocol.c)

### File Purpose
Responsible for P1/P2 protocol state management, synchronization handling, slot scheduling, and AT schedule table updates.
### Functions

#### `_tx_first()` (`static`)
Determine whether to transmit first in the P2 phase based on hop parity. Nodes with even hop counts transmit first, and nodes with odd hop counts receive first.
#### `_p2_build_schedule()` (`static`)
Calculate the duration and start offset of each P2 subslot in the current round, as well as the total slot duration including guard intervals, according to the operating mode. AT mode uses the scheduled class IDs for the calculation, and fixed mode uses the configured fixed duration.
#### `_class_span_bytes()` (`static`)
Calculate the byte span used for dividing lengths into classes, based on the maximum allowed data length and the number of classes.
#### `protocol_init()`
Initialize the protocol state information.
#### `p1_start()`
Initialize the P1 phase state information.
#### `p1_get_slot_ticks()`
Calculate the duration of one P1 slot based on whether the current round carries update information.
#### `p1_get_slot_start_local_ticks()`
Calculate the start time of each P1 slot before it begins.
#### `p1_prepare_tx()`
Prepare the P1 synchronization frame fields and increment the transmission count.
#### `p1_handle_rx()`
Process the received P1 synchronization information and update the round, synchronization reference, hop, and schedule table state.
#### `p1_finish_slot()`
Wrap-up and state updates after a slot in the P1 phase ends.
#### `p2_start()`
Initialize the P2 state, build the slot schedule, and determine whether to start P2 based on the synchronization state and operating mode.
#### `p2_get_subslot_start_local_ticks()`
Calculate the start time of each P2 subslot before it begins.
#### `p2_get_slot_ticks()`
Get the duration of one P2 slot for log output.
#### `p2_prepare_tx()`
Check whether the node data corresponding to the current subslot exists, prepare the P2 frame header, and provide the data pointer.
#### `p2_handle_rx()`
Check whether the received P2 frame corresponds to the current slot and subslot, and store the data; when the master detects a change in the desired class ID, update the corresponding scheduled class ID and set the flag to send the schedule table in the next round.
#### `p2_finish_subslot()`
Wrap-up and state updates after a subslot in the P2 phase ends.
#### `prepare_round()`
Clear the data store for the current round; in AT mode, the master determines whether the current round carries the schedule table based on the update flag, and increments the schedule version when the table is carried.
#### `frame_len_to_class()`
Calculate the data class ID corresponding to the frame length.
#### `class_to_frame_len()`
Calculate the maximum frame length corresponding to the data class ID.
#### `master_track_p2_completeness()`
In AT mode, when the number of consecutive rounds in which the master has not collected all P2 node data reaches the threshold, set the flag for the next round to carry the schedule table to help followers resume participating in P2.
## [packet.c](packet.c)

### File Purpose
Responsible for encoding P1 synchronization frames and P2 data frames before transmission, and parsing their headers after reception.
### Functions

#### `_u32_to_le()` (`static`)
Write a 32-bit unsigned integer into 4 elements of a byte array in little-endian order.
#### `_u32_from_le()` (`static`)
Combine 4 bytes from a byte array into a 32-bit unsigned integer in little-endian order and return it.
#### `encode_p1_sync()`
Pack the P1 synchronization frame header and optional schedule table, and return the frame length.
#### `decode_p1_sync()`
Unpack the synchronization frame header for the P1 phase.
#### `encode_p2_data()`
Pack the P2 data frame header and write the data to the corresponding position after the header.
#### `decode_p2_data()`
Unpack the data frame header for the P2 phase.
## [store.c](store.c)

### File Purpose
Responsible for storing data from multiple nodes during the P2 phase, and providing checks for data presence and counts of nodes whose data has been stored.
### Functions

#### `store_import()`
Store the node data, data length, and desired class ID at the position corresponding to the node ID, and update the number of nodes with stored data; do not write again if data for that node already exists.
#### `store_has_data()`
Determine whether data already exists at the index corresponding to the node ID.
#### `store_present_count()`
Get the number of nodes whose data has been stored.
## [Makefile](Makefile)

### File Purpose
Configure the external module paths, default board, and application compile-time parameters.
