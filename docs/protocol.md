# UART protocol and image format

The host uses USART3 on PD8 (TX) and PD9 (RX) via the ST-LINK VCP. The line is 115200 baud, 8N1, with no flow control. Integers are little-endian. A frame consists of:

| Field | Bytes | Meaning |
| --- | ---: | --- |
| Sync | 2 | `A5 5A` |
| Version | 1 | 1 |
| Command | 1 | Request ID, or request ID with bit 7 set in the response |
| Sequence | 2 | Host selects the first value and increments modulo 65,536 |
| Payload length | 2 | 0 to 512 |
| Payload | 0 to 512 | Command-specific bytes |
| CRC32 | 4 | Standard IEEE CRC32 over version through payload |

Every response begins with a status byte. The values are 0 for success, 1 for bad argument, 2 for bad state, 3 for flash error, 4 for invalid image and 5 for bad sequence. The table lists any response bytes after the status.

| ID | Command | Request bytes | Response bytes on success |
| ---: | --- | --- | --- |
| 1 | HELLO | Empty | Supported version (8-bit) |
| 2 | GET_INFO | Empty | Confirmed, pending, single-bank flag (8-bit each), hardware ID (32-bit) |
| 3 | BEGIN_UPDATE | Slot (8-bit), packaged length (32-bit) | Empty |
| 4 | ERASE_SLOT | Empty | Empty |
| 5 | WRITE_CHUNK | Offset (32-bit), 1 to 256 image bytes | Empty |
| 6 | GET_PROGRESS | Empty | Bytes written (32-bit) |
| 7 | VERIFY_IMAGE | Empty | Empty |
| 8 | ACTIVATE_IMAGE | Empty | Empty |
| 9 | REBOOT | Empty | Empty, sent before reset |
| 10 | ABORT_UPDATE | Empty | Empty |
| 11 | GET_LOG | Empty | Trial attempts (8-bit), journal generation (32-bit) |

A is 0, B is 1 and an empty slot is 255. HELLO starts a version-1 session. The bootloader caches the last accepted request and reply, so resending an identical frame with the same sequence replays the reply without programming again. Out-of-order offsets and sequences are rejected. Packets with a bad checksum or excessive length are dropped. A transfer interrupted by reset can restart at BEGIN_UPDATE and erase the destination again. Transfer progress is kept in RAM for the current session.

The initial host timeouts are one second for most commands, 30 seconds for erase, and 20 seconds for verify, with up to three retries. A REBOOT timeout is ambiguous because the target can reset after sending its reply. Check the LEDs or return to recovery and query the state.

## Image header

The slot starts with a 512-byte header, followed by the application’s vector table and binary data. The packager fills the reserved range with `0xff`.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `0x46574D47` |
| 4 | 2 | Header format version 1 |
| 6 | 2 | Header size 512 |
| 8 | 4 | Firmware version |
| 12 | 4 | Project hardware ID `0x767` |
| 16 | 4 | Payload length, excluding header |
| 20 | 4 | Vector table offset 512 |
| 24 | 32 | SHA-256 of the payload |
| 56 | 4 | IEEE CRC32 of header bytes 0 through 55 |
| 60 | 452 | Reserved |

A firmware version is recorded for identification. This format supports accidental corruption detection. Authenticating the publisher or enforcing a minimum version would need additional policy and a trusted verification key.
