# BEKO soil telemetry protocol

## Radio profile

The sensor node uses raw LoRa point-to-point communication (not LoRaWAN):

- frequency: 868.1 MHz
- bandwidth: 125 kHz
- spreading factor: SF7
- coding rate: 4/5
- preamble: 8 symbols
- sync word: `0x34`
- explicit header
- normal IQ
- LoRa PHY payload CRC enabled

The gateway must use exactly the same profile.

## Payload

Every payload is exactly 11 bytes. Multi-byte fields use network byte order
(big-endian).

| Offset | Size | Field | Unit / invalid value |
|---:|---:|---|---|
| 0 | 2 | Network ID (`uint16`) | default `0x0001` |
| 2 | 2 | Device ID (`uint16`) | default `0x0001` |
| 4 | 1 | Soil moisture 1, PA2 | `0..100` %, `0xFF` invalid |
| 5 | 1 | Soil moisture 2, PA3 | `0..100` %, `0xFF` invalid |
| 6 | 2 | Temperature (`int16`) | 0.01 deg C, `0x8000` invalid |
| 8 | 2 | Pressure (`uint16`) | 0.1 hPa, `0xFFFF` invalid |
| 10 | 1 | Application CRC | CRC-8/ATM over bytes 0 through 9 |

The application CRC parameters are:

- polynomial: `0x07`
- initial value: `0x00`
- input/output reflection: false
- final XOR: `0x00`

This CRC remains part of the application payload even though the SX1276 PHY
CRC is also enabled.

## Reference vector

Input values:

- network ID: 1
- device ID: 1
- soil 1: 42%
- soil 2: 87%
- temperature: 23.45 deg C
- pressure: 1013.2 hPa

Encoded frame:

```text
00 01 00 01 2A 57 09 29 27 94 AC
```

## Decoder outline

1. Reject payloads whose length is not 11 bytes.
2. Calculate CRC-8/ATM over bytes 0..9 and compare it with byte 10.
3. Decode all 16-bit fields as big-endian.
4. Divide temperature by 100 and pressure by 10 after checking their invalid
   sentinels.

The transmitter sends once immediately after startup data becomes available,
then once every 3600 seconds. There is no acknowledgement or retransmission.
