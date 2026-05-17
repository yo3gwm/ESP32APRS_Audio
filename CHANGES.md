# ESP32APRS Audio — Change Log (GWM Fork)

Changes applied to the original source code (v1.7) by **Cristi Mitroi — YO3GWM**.  
Hardware target: **ESP32DR Simple** with SA818/SA8x8 RF module.

---

## Build System

### Platform version pinned to 54.03.20
**File:** `platformio.ini`

Pinned to the first stable pioarduino release (ESP-IDF 5.4.x) instead of `stable`.
The newer 55.x platform allocates ~38 KB more framework code in IRAM, reducing heap
from ~298 KB to ~260 KB. Pinning to 54.03.20 restores the original memory layout.

### Removed `-DPPPOS` from `esp32-nodisp` environment
**File:** `platformio.ini`

PPPoS (cellular modem support) is not needed for ESP32DR Simple. Removed to reduce
binary size and eliminate unnecessary initialization overhead.

---

## Compiler & Linker Fixes

### `frameDecodeCount` — undefined reference
**File:** `lib/LibAPRS_ESP32/AFSK.cpp`

Added definition for `volatile uint32_t frameDecodeCount = 0` which was declared
`extern` in `main.cpp` but never defined, causing a linker error.

### `analogRead()` before `analogSetPinAttenuation()`
**File:** `lib/LibAPRS_ESP32/AFSK.cpp` — `afskSetADCAtten()` and `AFSK_hw_init()`

On ESP32 classic, calling `analogSetPinAttenuation()` on an unconfigured pin produces
`[E] Pin is not configured as analog channel`. A preceding `analogRead()` forces the
Arduino framework to register the ADC handle, eliminating the error.

### Explicit `volatile` increment — `-Wvolatile` warning
**Files:** `lib/LibAPRS_ESP32/AFSK.cpp`, `lib/LibAPRS_ESP32/modem.cpp`

Replaced `volatile_var++` with `volatile_var = volatile_var + 1` throughout to
suppress the C++20 `-Wvolatile` deprecation warning. Applies to `adcIsrCount`,
`fifoOverflowCount`, and CPU idle hook counters.

### Explicit cast in `PLL9600_STEP` macro — `-Woverflow` warning
**File:** `lib/LibAPRS_ESP32/modem.cpp`

```cpp
#define PLL9600_STEP ((int32_t)(uint32_t)(((uint64_t)1 << 32) / N9600))
```
The expression evaluates to 2^32 which overflows `int32_t`. The two-step cast makes
the truncation deliberate and silences the warning without changing runtime behaviour.

### Removed deprecated PCNT driver
**File:** `src/main.cpp`

Removed `#include "driver/pcnt.h"`, `pcnt_isr_handle_t user_isr_handle`, and
`pcnt_evt_t` struct. These were commented-out legacy declarations causing
`pcnt_isr_handle_t does not name a type` linker errors and deprecation warnings.

---

## Removed Features

### CPU temperature sensor (`ESPCPUTemp`)
The ESP32 D0WD-Q5 does not support the IDF temperature sensor driver. Removed:
- `lib/ESPCPUTemp/` — entire library directory deleted
- `src/sensor.cpp` — `getCPU_TEMP()` function and `PORT_CPU_TEMP` case removed
- `src/webservice.cpp` — CPU temp column removed from system info table
- `include/webservice.h` — legacy `temprature_sens_read()` declaration removed

### CTCSS tones
APRS does not use CTCSS. Removed from all layers:
- `include/main.h` — `ctcss[]` frequency array deleted
- `include/config.h` — `int tone_rx`, `int tone_tx` fields removed
- `src/config.cpp` — `rfToneRX` / `rfToneTX` JSON keys removed
- `src/main.cpp` — `ctcssToHex()` function removed; all RF module AT commands
  now send hardcoded `0` (SA8x8) or `0xFF 0xFF` (SR120U) for CTCSS disabled
- `src/webservice.cpp` — TX/RX CTCSS dropdowns and POST handlers removed
- `src/handleATCommand.cpp` — `AT+TONE_RX` / `AT+TONE_TX` commands removed

---

## AFSK Modem Fixes

### `taskAPRSPoll` stack size — 2048 → 4096 words
**File:** `src/main.cpp`

The gap detector added `Serial.printf()` calls to `AFSK_Poll()`. Each call uses
~400–600 bytes of stack on ESP32. With the original 2048-word stack, the first gap
event triggered `Stack canary watchpoint triggered (taskAPRSPoll)` and a hard fault.
Stack increased to 4096 words in both Xtensa and non-Xtensa branches.

### GPTimer double-start fix
**File:** `lib/LibAPRS_ESP32/AFSK.cpp` — `AFSK_TimerEnable(true)` and `DAC_TimerEnable(true)`

Added `timerStop(timer_adc/dac)` before `timerStart()` to guard against the
`gptimer_start: timer is not ready for a new start` error at boot when the timer
was started by `AFSK_hw_init()` and then again by the first `AFSK_TimerEnable(true)` call.

### FIFO flush on ADC timer restart
**File:** `lib/LibAPRS_ESP32/AFSK.cpp` — `AFSK_TimerEnable(true)`

Replaced `RingBuffer_IsEmpty(&fifo)` (no-op) with `RingBuffer_Init(&fifo)` to flush
stale samples from the FIFO every time the ADC timer is restarted. This ensures that
samples accumulated during WiFi reconnection or RF module reconfiguration are discarded
before new clean samples arrive.

### FIFO overflow protection in `sample_adc_isr()`
**File:** `lib/LibAPRS_ESP32/AFSK.cpp`

The ISR previously wrote unconditionally to the ring buffer, overwriting unread samples
when the FIFO was full. Added bounds check: overflowed samples increment `fifoOverflowCount`
instead of corrupting the buffer. Count is printed by the RX diagnostic logger.

### TX stuck detection in `AFSK_Poll()`
**File:** `lib/LibAPRS_ESP32/AFSK.cpp`

Added periodic warning `[AFSK] WARNING: hw_afsk_dac_isr stuck TRUE for Xs — RX blocked!`
when `hw_afsk_dac_isr` remains `true` for more than 15 seconds, indicating a stuck TX
flag that silently blocks all RX processing.

### Gap detector in `AFSK_Poll()`
**File:** `lib/LibAPRS_ESP32/AFSK.cpp`

Added two automatic diagnostics that run unconditionally (no RX log needed):
- **ISR stuck:** prints `[GAP] ADC ISR STOPPED` if `adcIsrCount` does not advance
  for more than 5 seconds.
- **Modem gate closed:** prints `[GAP] Modem gate closed for Xs` if `dcd_cnt ≤ 2`
  for more than 30 seconds, and `[GAP] Modem gate RECOVERED` when it opens again.

### AGC float format removed from gap detector
**File:** `lib/LibAPRS_ESP32/AFSK.cpp`

Replaced `%.2f` (agc_gain) with `%d%%` (integer × 100) in the gap detector
`Serial.printf`. Float formatting adds ~200 bytes of extra stack per call on ESP32,
reducing the margin before stack overflow in `taskAPRSPoll`.

### `audio_buffer` re-allocation guards
**File:** `lib/LibAPRS_ESP32/AFSK.cpp`

- **`AFSK_hw_init()`:** re-allocates `audio_buffer` if `AFSK_deinit()` raced and
  freed it during boot (WiFi init vs AFSK init concurrent execution).
- **`AFSK_Poll()`:** re-allocates `audio_buffer` at the start of every call if it is
  `NULL`, recovering from any subsequent `AFSK_deinit()` call.
  Prints `[AFSK] audio_buffer re-allocated in AFSK_Poll` to serial.

### TX LED debounce fix
**File:** `lib/LibAPRS_ESP32/AFSK.cpp` — `LED_Status2()`

The 100 ms debounce window blocked the RX→TX colour transition. Fixed by making TX
state changes (red channel on/off) bypass the rate limiter entirely. Non-critical
transitions (RX, idle) are rate-limited to 20 ms.

### CSMA — block TX during active HDLC decode
**File:** `lib/LibAPRS_ESP32/AX25.cpp` — `Ax25TransmitCheck()`

Added check against HDLC decoder state in addition to analogue PLL DCD:
```cpp
bool rxActive = (Ax25GetRxStage(0) != RX_STAGE_IDLE) ||
                (Ax25GetRxStage(1) != RX_STAGE_IDLE);
if (!ModemDcdState() && !rxActive) transmitStart();
```
Prevents TX collision when PLL DCD drops momentarily during a received frame.

### FX25 `rxFrameBufferFull` — wrong pointer
**File:** `lib/LibAPRS_ESP32/AX25.cpp` — `parseFx25Frame()`

```cpp
// Before (compares RX head against TX head — wrong):
if (rxFrameHead == txFrameHead) rxFrameBufferFull = true;
// After (correct):
if (rxFrameHead == rxFrameTail) rxFrameBufferFull = true;
```
The bug caused FX25 frames to be dropped after the first transmission because
`txFrameHead` advanced to 1, prematurely triggering the full flag.

### Stale bytes in comment after in-place edit
**File:** `src/parse_aprs.cpp` — `parse_aprs_comment()`

```cpp
// Before (includes stale bytes past the '\0'):
rest_len = &input[input_len] - rest;
// After:
rest_len = (unsigned int)strnlen(rest, max_len);
```
`parse_remove_part()` edits the buffer in-place and writes `'\0'` at the new end,
but leaves stale bytes beyond it. The original `rest_len` calculation included those
bytes, causing repeated text fragments in the WX/TEXT column.

### `toCharArray` → `memcpy` for raw packet buffer
**File:** `src/main.cpp`

`String::toCharArray(buf, len)` copies `len - 1` characters (Arduino semantics),
silently dropping the last byte of every packet. Replaced with `memcpy`.

---

## WiFi / Network Fixes

### WiFi reconnect — stop ADC during mode changes
**File:** `src/main.cpp` — `wifiConnection()`

RF subsystem reinitialisation during `WiFi.mode(WIFI_OFF/STA)` causes
`analogReadMilliVolts()` to return near-constant values. The FIFO fills with flat
data → `mVrms ≈ 2`, `dcd_cnt = 0`, reception stops for 2–5 minutes.

**Fix:** `AFSK_TimerEnable(false)` before WiFi mode changes, `AFSK_TimerEnable(true)`
after reconnect. The timer restart calls `RingBuffer_Init()` to flush stale samples.
Recovery is complete within 40 ms.

### WiFi reconnect — removed `AFSK_deinit()` call
**File:** `src/main.cpp` — `wifiConnection()`

`AFSK_deinit()` in the ESP32/ADC_SAMPLE path only freed `audio_buffer` (the ADC
continuous mode handles are NULL). This triggered unnecessary `audio_buffer`
re-allocation and the `[AFSK] audio_buffer re-allocated` message. Removed.

### WiFi reconnect — radio has priority
**File:** `src/main.cpp` — `wifiConnection()`

Added TX interlock: waits up to 5 seconds for any active transmission to finish
before proceeding with WiFi reconnection. Forces PTT off only as a last resort.

### WiFi disconnect fix — error 12308
**File:** `src/main.cpp` — `wifiConnection()`

Changed `WiFi.disconnect(true, true, 500)` to `WiFi.disconnect(false, true)` to
prevent double `esp_wifi_deinit` when `WiFi.mode(WIFI_OFF)` is called immediately
after. Added 1500 ms delay after `WIFI_OFF` to allow netif teardown to complete.

---

## Task & Performance Fixes

### Task priorities raised above `async_tcp`
**File:** `src/main.cpp`

| Task | Before | After |
|------|--------|-------|
| `taskAPRSPoll` | 0 / 11 | **12** |
| `taskAPRS` | 2 / 11 | **11** |
| `async_tcp` | 10 | 10 (unchanged) |

`async_tcp` at priority 10 previously starved `taskAPRSPoll`, causing FIFO overflow
and missed packets during active web page loads.

### PTT watchdog — 10 s TOT
**File:** `src/main.cpp` — `taskAPRS` main loop

Added check at the top of the loop: if `pttActiveSince > 0` for more than 10 seconds,
PTT is forced off and a warning is printed. Prevents the radio from remaining locked
in TX if PTT release is missed for any reason.

### SlotTime default — 2000 ms → 200 ms
**File:** `src/main.cpp`

Corrected the default CSMA slot time to 200 ms as per the APRS specification.

---

## RF Module Changes

### SA868 → SA8x8 rename throughout the project
All references to `SA868` renamed to `SA8x8` to correctly reflect support for both
SA818 and SA868 variants:
- `#define RF_SA868_*` → `#define RF_SA8x8_*` in `include/main.h`
- `RF_TYPE[]` strings updated: `"SA868_VHF"` → `"SA8x8_VHF"` etc.
- Functions `SA868_waitResponse()`, `SA868_getRSSI()`, `SA868_getVERSION()` renamed
- All `sprintf` log strings and web UI labels updated

### RF module configuration moved to Apply Change only
**File:** `src/main.cpp`

At boot, only `RF_HW_INIT()` is called: powers on the module (PWR + PD pins),
initialises `SerialRF`, and waits for startup — no AT commands are sent.
AT command configuration is sent exclusively when the user presses **Apply Change**
in the web UI (`RF_MODULE(false)`), giving full control over when the module is
programmed.

### TX interlock during AT command session
**Files:** `lib/LibAPRS_ESP32/AFSK.cpp`, `lib/LibAPRS_ESP32/modem.cpp`, `src/main.cpp`

Added `volatile bool txInhibit` flag. Set to `true` before any AT command session
(Apply Change or individual Write buttons), cleared after. `ModemTransmitStart()`
checks the flag and returns immediately without activating PTT.

### Apply Change — reception loss bug fixed
**File:** `src/main.cpp` — `RF_MODULE(false)`

**Root cause:** The original Apply Change path power-cycled the RF module (PD LOW →
HIGH). This pulled the SQL pin low. `AFSK_Poll()` detected the SQL transition
(`sqlActiveOld = true` → `sqlActive = false`) and called `AFSK_TimerEnable(false)`.
Since `adcEn = 1` was already consumed by `taskAPRS`, no one restarted the timer —
reception was permanently blocked until reboot.

**Fix:** When called from Apply Change (`boot = false`), `RF_MODULE()` does **not**
power-cycle the module. It only flushes the serial buffer, waits for ongoing TX,
sets `txInhibit`, and sends AT commands. The ADC timer is never stopped.

### SA8x8 filter control in web UI
**Files:** `src/webservice.cpp`, `include/config.h`, `src/config.cpp`, `src/main.cpp`

Added three toggle switches to the RF Analog Module section:
- **Pre/De-emphasis** — bit 0 of `config.rf_filters`
- **High Pass Filter** — bit 1
- **Low Pass Filter** — bit 2

SA818 protocol is inverted: UI `1` (on) → sends `0` to module (filter enabled);
UI `0` (off) → sends `1` (filter bypassed). Default: all filters off (`0,1,1` sent →
both HP and LP bypassed, matching SA818 power-on defaults).
Stored in JSON as `rfFilters`. Applied via `AT+SETFILTER`.

### Frequency unification — single field + PPM correction
**Files:** `src/webservice.cpp`, `include/config.h`, `src/config.cpp`, `src/main.cpp`

Replaced separate TX/RX frequency fields with a single **Frequency** field (APRS
is simplex — TX = RX always). Added **Freq Correction** field in PPM:

```
corrected_freq = nominal_freq × (1 + ppm / 1,000,000)
```

Applied to all RF module types (SA8x8, SR110, SR120U). Stored as `rfFreq` + `rfFreqPPM`
in JSON. AT command `AT+FREQ_PPM` replaces `AT+OFFSET_RX` / `AT+OFFSET_TX`.

### SA8x8 individual write buttons
**File:** `src/webservice.cpp`, `src/main.cpp`

Added three buttons visible only for SA8x8 module type:
- **Write Group** — sends `AT+DMOSETGROUP` with current frequency/SQL/band settings
- **Write Volume** — sends `AT+DMOSETVOLUME`
- **Write Filters** — sends `AT+SETFILTER`

Each button opens a popup showing the command sent and the module response.
Includes TX interlock (waits for ongoing TX, sets `txInhibit`).
Timeout: 1500 ms per command.

### SA8x8 Check Version button
**File:** `src/webservice.cpp`, `src/main.cpp`

Added **Check Version** button (visible for SA8x8 types). Sends `AT+DMOCONNECT`
then `AT+VERSION` and displays the firmware version string in a popup.
Returns `"Version not supported by this module"` if the module responds with
`+DMOERROR`, `"Connection error to SA8x8"` on timeout.

### RF Enable interlock in web UI
**File:** `src/webservice.cpp`

When the **Enable** toggle is `OFF`, all RF module parameters (frequency, filters,
volume, SQL, etc.) are automatically disabled (`disabled` attribute) via JavaScript.
Apply Change remains always enabled so the disabled state can be saved.
Initial state is applied on page load via inline `<script>` after the form.

---

## Web UI Improvements

### WX/TEXT column in Last-Heard table
**File:** `src/webservice.cpp`

Added a new column showing WX data (`T:xx.xC H:xx% P:xxxxmb W:xxkm/h`) for weather
packets and the APRS comment field for all other packets. Container width increased
900 px → 1100 px.

### TxDelay / SlotTime — renamed and changed to NUMBER input
**File:** `src/webservice.cpp`

`Preamble` → **TxDelay** (range 100–5000 ms, step 100). `TX Time Slot` → **SlotTime**.
Both changed from SELECT dropdowns to NUMBER inputs.

### CPU load display — replaces CPU MHz
**File:** `src/main.cpp`, `src/webservice.cpp`

The system info bar `CPU(Mhz)` column replaced with `CPU Load` showing per-core
utilisation (`C0:23% C1:71%`). Implemented via FreeRTOS idle hooks registered for
both cores. Overhead: one `uint32_t` increment per idle task iteration (~2 CPU cycles).
Calibrates automatically after the first 2-second measurement window.

### Log file fixes
**Files:** `src/main.cpp`, `src/webservice.cpp`

- **Bug fix:** Fixed wrong flag in fixed-position IGate log — `LOG_TRACKER` was
  checked instead of `LOG_IGATE` (line 6860), preventing IGate log files from
  being created when using a fixed position.
- **Bug fix:** Removed `adcEn = -1; dacEn = -1; delay(100)` from `handle_storage()`.
  Accessing the File tab stopped AFSK reception permanently.
- **Improvement:** File open errors now print `[LOG] Failed to open <file>` to serial
  unconditionally (previously required `#ifdef DEBUG`).

### Author credit added
**File:** `src/webservice.cpp` — About page

```
Author: Mr.Somkiat Nakhonthai (updated by Cristi Mitroi - YO3GWM)
```

### Copyright year updated
**File:** `src/webservice.cpp` — Footer: `©2023` → `©2026`

---

## Packet Decoding Improvements

### Third-party packet inner decode
**File:** `src/main.cpp`

Packets with Data Type Indicator `}` (third-party frames) were previously dropped.
Now the inner packet is extracted and displayed in the last-heard table under its
own callsign (e.g., objects gated by an igate are shown directly).

### Object packets — separate pkgList slot from source beacon
**File:** `src/main.cpp`

For object packets, the RF receive path uses the object name (e.g., `"YO3A"`) as
the `pkgList` key instead of the source callsign (`"YO3KXL-10"`). Objects and beacons
from the same station now occupy separate table rows.

### Object name extraction — allow spaces
**File:** `src/main.cpp` — `pkgListUpdate()`

Changed break condition from `body[z] < 0x30` to `body[z] < 0x20` to allow spaces
(0x20) in object names. Standard APRS object names are 9 characters wide with
trailing spaces.

---

## Summary

| # | File | Change | Category |
|---|------|--------|----------|
| 1 | `platformio.ini` | Platform pinned to 54.03.20 | Build |
| 2 | `platformio.ini` | Removed `-DPPPOS` | Build |
| 3 | `AFSK.cpp` | `frameDecodeCount` defined | Linker fix |
| 4 | `AFSK.cpp` | `analogRead()` before attenuation | Runtime fix |
| 5 | `AFSK.cpp` | `volatile` increment warnings | Compiler |
| 6 | `modem.cpp` | `PLL9600_STEP` cast | Compiler |
| 7 | `main.cpp` | PCNT driver removed | Compiler |
| 8 | `main.cpp` | `taskAPRSPoll` stack 2048→4096 | Crash fix |
| 9 | `AFSK.cpp` | GPTimer double-start guard | Boot fix |
| 10 | `AFSK.cpp` | FIFO flush on timer restart | Reception fix |
| 11 | `AFSK.cpp` | FIFO overflow protection | Data integrity |
| 12 | `AFSK.cpp` | TX stuck detection | Diagnosis |
| 13 | `AFSK.cpp` | Gap detector | Diagnosis |
| 14 | `AFSK.cpp` | AGC integer format in gap detector | Stack fix |
| 15 | `AFSK.cpp` | `audio_buffer` re-allocation guards | Reception fix |
| 16 | `AFSK.cpp` | TX LED debounce fixed | UI fix |
| 17 | `AX25.cpp` | CSMA blocks TX during HDLC decode | Protocol fix |
| 18 | `AX25.cpp` | FX25 `rxFrameBufferFull` pointer | Bug fix |
| 19 | `parse_aprs.cpp` | `strnlen` for comment length | Display fix |
| 20 | `main.cpp` | `toCharArray` → `memcpy` | Data integrity |
| 21 | `main.cpp` | Task priorities 0→12 / 2→11 | Performance |
| 22 | `main.cpp` | PTT watchdog 10 s TOT | Safety |
| 23 | `main.cpp` | WiFi reconnect waits for TX | Radio priority |
| 24 | `main.cpp` | WiFi disconnect error 12308 fixed | Network fix |
| 25 | `main.cpp` | WiFi reconnect stops ADC timer | Reception fix |
| 26 | `main.cpp` | SlotTime default 2000→200 ms | Protocol |
| 27 | `main.cpp` | SA868 → SA8x8 rename | Naming |
| 28 | `main.cpp` | RF config moved to Apply Change only | UX |
| 29 | `main.cpp` | TX interlock during AT commands | Safety |
| 30 | `main.cpp` | Apply Change reception loss fixed | Bug fix |
| 31 | `main.cpp` | Frequency unified + PPM correction | Feature |
| 32 | `main.cpp` | RF_SA8x8_WriteGroup/Volume/Filters | Feature |
| 33 | `main.cpp` | SA8x8_getVERSION with DMOCONNECT | Bug fix |
| 34 | `main.cpp` | RF_HW_INIT at boot (no AT commands) | Feature |
| 35 | `main.cpp` | Log file IGate flag bug fixed | Bug fix |
| 36 | `main.cpp` | Third-party packet inner decode | Feature |
| 37 | `main.cpp` | Object pkgList keyed by name | Bug fix |
| 38 | `main.cpp` | Object name allows spaces | Bug fix |
| 39 | `main.cpp` | PTT activation timestamp | Safety |
| 40 | `main.cpp` | CPU load idle hooks | Feature |
| 41 | `modem.cpp` | TX interlock in `ModemTransmitStart` | Safety |
| 42 | `webservice.cpp` | WX/TEXT column | Feature |
| 43 | `webservice.cpp` | TxDelay/SlotTime renamed + NUMBER | UX |
| 44 | `webservice.cpp` | Copyright 2023→2026 | Maintenance |
| 45 | `webservice.cpp` | CPU load replaces CPU MHz | Feature |
| 46 | `webservice.cpp` | SA8x8 filter toggles | Feature |
| 47 | `webservice.cpp` | Write Group/Volume/Filters buttons | Feature |
| 48 | `webservice.cpp` | Check Version button | Feature |
| 49 | `webservice.cpp` | RF Enable interlock | UX |
| 50 | `webservice.cpp` | Log file handle_storage ADC bug | Bug fix |
| 51 | `webservice.cpp` | Author credit YO3GWM | Maintenance |
| 52 | `config.h` | `tone_rx`/`tone_tx` removed (CTCSS) | Cleanup |
| 53 | `config.h` | `offset_rx`/`offset_tx` → `freq_ppm` | Feature |
| 54 | `config.h` | `rf_filters` field added | Feature |
| 55 | `main.h` | `ctcss[]` array removed | Cleanup |
| 56 | `main.h` | `RF_SA868_*` → `RF_SA8x8_*` | Naming |
| 57 | `handleATCommand.cpp` | `AT+TONE_RX/TX` removed | Cleanup |
| 58 | `handleATCommand.cpp` | `AT+FREQ_PPM` added | Feature |
| 59 | `lib/ESPCPUTemp` | Library deleted | Cleanup |

---

*YO3GWM — 2026*
