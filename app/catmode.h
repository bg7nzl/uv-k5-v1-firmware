/* Copyright 2025 bg7nzl
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APP_CATMODE_H
#define APP_CATMODE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * CAT (Computer Aided Transceiver) control protocol (PC <-> radio)
 *
 * Frame: SYNC(0xAB) CMD(1B) LEN(1B) PAYLOAD(0..N) CRC8(1B)
 * CRC8 = XOR of all bytes from SYNC to last payload byte
 *
 * All parameter SET/GET operate directly on the current VFO.
 * Entering CAT mode forces the active channel into VFO (frequency) mode.
 * Exiting CAT mode leaves all VFO settings in place (no backup/restore).
 */

#define CAT_SYNC   0xAB

/* --- CMD codes (0x10-0x2F, disjoint from digmode 0x01-0x0A) --- */

#define CAT_CMD_ENTER       0x10
#define CAT_CMD_EXIT        0x11
#define CAT_CMD_SET_PARAM   0x12
#define CAT_CMD_GET_PARAM   0x13
#define CAT_CMD_PARAM_RESP  0x14
#define CAT_CMD_SET_MULTI   0x15
#define CAT_CMD_GET_ALL     0x16
#define CAT_CMD_ALL_RESP    0x17
#define CAT_CMD_APPLY       0x18
#define CAT_CMD_STATUS      0x19
#define CAT_CMD_STATUS_RESP 0x1A
#define CAT_CMD_NOOP        0x1B

#define CAT_CMD_MIN         0x10
#define CAT_CMD_MAX         0x1B

#define CAT_CMD_ACK         0x05

#define CAT_RESULT_OK       0x00
#define CAT_RESULT_ERR      0x01

/* --- Parameter IDs --- */

#define CAT_PARAM_RX_FREQ       0x01   /* 4B, 10 Hz units */
#define CAT_PARAM_TX_FREQ       0x02   /* 4B, 10 Hz units */
#define CAT_PARAM_TX_OFFSET     0x03   /* 4B, 10 Hz units */
#define CAT_PARAM_OFFSET_DIR    0x04   /* 1B: 0=none, 1=+, 2=- */
#define CAT_PARAM_RX_TONE_TYPE  0x05   /* 1B: 0=OFF, 1=CTCSS, 2=DCS, 3=DCS-I */
#define CAT_PARAM_RX_TONE_CODE  0x06   /* 2B */
#define CAT_PARAM_TX_TONE_TYPE  0x07   /* 1B */
#define CAT_PARAM_TX_TONE_CODE  0x08   /* 2B */
#define CAT_PARAM_MODULATION    0x09   /* 1B: 0=FM, 1=AM, 2=USB */
#define CAT_PARAM_TX_POWER      0x0A   /* 1B: K1 levels 0-7, mapped to K5 0-2 */
#define CAT_PARAM_BANDWIDTH     0x0B   /* 1B: 0=wide, 1=narrow */
#define CAT_PARAM_SQUELCH       0x0C   /* 1B: 0-9 */
#define CAT_PARAM_VOX_SWITCH    0x0D   /* 1B: 0=off, 1=on */
#define CAT_PARAM_VOX_LEVEL     0x0E   /* 1B: 0-9 */
#define CAT_PARAM_VOX_DELAY     0x0F   /* 1B: 0-10 (x100ms) */
#define CAT_PARAM_MIC_GAIN      0x10   /* 1B: 0-4 */
#define CAT_PARAM_SPEAKER_GAIN  0x11   /* 1B: 0-15 (REG_48 AF Rx Gain-2) */
#define CAT_PARAM_DAC_GAIN      0x12   /* 1B: 0-15 (REG_48 low nibble) */
#define CAT_PARAM_COMPANDER     0x13   /* 1B: 0=OFF, 1=TX, 2=RX, 3=both */
#define CAT_PARAM_SCRAMBLE      0x14   /* 1B: 0=OFF, 1-10 */
#define CAT_PARAM_BUSY_LOCK     0x15   /* 1B: 0=OFF, 1=ON */
#define CAT_PARAM_STEP          0x16   /* 1B: step index */
#define CAT_PARAM_MIC_BAR       0x17   /* 1B: read-only, mic level */
#define CAT_PARAM_RSSI          0x18   /* 2B: read-only, RSSI */

#define CAT_PARAM_MAX           0x18

#define CAT_HEARTBEAT_TIMEOUT_MS  5000

extern volatile bool gCatModeEntered;

uint16_t CAT_ProcessByte(const uint8_t *buf, uint16_t available,
                         uint16_t buf_size, uint16_t start_idx);

void CAT_Poll(void);

#endif
