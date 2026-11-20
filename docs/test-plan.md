# Test plan

Run `make test` and `make test-native` on Linux before flashing. The native test emulates one-way flash writes and runs the target C code for the journal, hashing and image validation. It checks an interrupted commit and rotation as well as recovery from a corrupted newest record. The Python suite checks packaging, slot linking, frame validation and retransmission.

## Board checks

Use the README walkthrough to provision A. Check boot state with `info` in recovery and use the LED colors to identify which demo runs. These are expected results for the first hardware run.

| Scenario | Expected result |
| --- | --- |
| Reset a board with A confirmed | Green LED and A remains confirmed |
| Install and boot healthy B | Red LED and B confirms, pending clears |
| Install hanging B while A is confirmed | Watchdog restarts B up to three times and A starts again |
| Reset or disconnect serial during a transfer | A stays confirmed. Re-enter recovery to erase and restart B |
| Reset during destination erase or programming | Incomplete B is never made pending. A remains available |
| Flash a package with a mismatched digest or slot address | Verification fails and the image stays inactive |
| Deliver the same write packet twice | The reply is replayed and progress advances once |
| Deliver an out-of-order or oversized packet | The next valid request still works |
| Try to update the confirmed slot | The device rejects the request |
| Reset a board with no bootable confirmed or pending image | UART recovery stays open |

A reset at an operation boundary checks the software state transitions. A controlled power-cut test is needed to check behavior during an actual flash erase or program under a loss of supply. Record the affected slot, operation, previous boot state, reset cause and observed result for each run.

## Measurements to collect

Record startup and SHA-256 validation time, effective serial throughput, sector erase time, programming time, total update time and elapsed rollback time. Record the board revision, compiler version, payload size, clock, and option bytes beside each measurement. Check repeated boots and run interrupted updates at several offsets. A first target is 100 consecutive normal resets, with rollback within three failed trial starts. Those are bench targets.
