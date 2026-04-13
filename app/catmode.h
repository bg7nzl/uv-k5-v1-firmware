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
 * Shares the 0xAB sync byte with digmode but uses a disjoint CMD range
 * (0x10–0x2F) so both protocols can coexist without conflict.
 */

#define CAT_SYNC   0xAB

/* --- CMD codes (0x10–0x2F, disjoint from digmode 0x01–0x0A) --- */

#define CAT_CMD_ENTER       0x10   /* PC->Radio: enter CAT mode */
#define CAT_CMD_EXIT        0x11   /* PC->Radio: exit CAT mode */
#define CAT_CMD_SET_PARAM   0x12   /* PC->Radio: set single parameter */
#define CAT_CMD_GET_PARAM   0x13   /* PC->Radio: query single parameter */
#define CAT_CMD_PARAM_RESP  0x14   /* Radio->PC: parameter query response */
#define CAT_CMD_SET_MULTI   0x15   /* PC->Radio: set multiple parameters */
#define CAT_CMD_GET_ALL     0x16   /* PC->Radio: query all parameters */
#define CAT_CMD_ALL_RESP    0x17   /* Radio->PC: all parameters response */
#define CAT_CMD_APPLY       0x18   /* PC->Radio: apply params to hardware */
#define CAT_CMD_STATUS      0x19   /* PC->Radio: query radio status */
#define CAT_CMD_STATUS_RESP 0x1A   /* Radio->PC: status response */
#define CAT_CMD_NOOP        0x1B   /* PC->Radio: heartbeat / keep-alive */

#define CAT_CMD_MIN         0x10
#define CAT_CMD_MAX         0x1B

/* Reuse digmode's ACK frame format */
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
#define CAT_PARAM_TX_POWER      0x0A   /* 1B: 0–7 */
#define CAT_PARAM_BANDWIDTH     0x0B   /* 1B: 0=wide, 1=narrow */
#define CAT_PARAM_SQUELCH       0x0C   /* 1B: 0–9 */
#define CAT_PARAM_VOX_SWITCH    0x0D   /* 1B: 0=off, 1=on */
#define CAT_PARAM_VOX_LEVEL     0x0E   /* 1B: 0–9 */
#define CAT_PARAM_VOX_DELAY     0x0F   /* 1B: 0–10 (×100ms) */
#define CAT_PARAM_MIC_GAIN      0x10   /* 1B: 0–4 */
#define CAT_PARAM_SPEAKER_GAIN  0x11   /* 1B: 0–15 */
#define CAT_PARAM_DAC_GAIN      0x12   /* 1B: 0–15 */
#define CAT_PARAM_COMPANDER     0x13   /* 1B: 0=OFF, 1=TX, 2=RX, 3=both */
#define CAT_PARAM_SCRAMBLE      0x14   /* 1B: 0=OFF, 1–10 */
#define CAT_PARAM_BUSY_LOCK     0x15   /* 1B: 0=OFF, 1=ON */
#define CAT_PARAM_STEP          0x16   /* 1B: step index */
#define CAT_PARAM_MIC_BAR       0x17   /* 1B: read-only, mic level */
#define CAT_PARAM_RSSI          0x18   /* 2B: read-only, RSSI */

#define CAT_PARAM_MAX           0x18

#define CAT_HEARTBEAT_TIMEOUT_MS  5000   /* link-loss timeout (ms) */

/* --- Parameter structure (independent of main VFO) --- */

typedef struct {
    uint32_t rx_freq;           /* 10 Hz units */
    uint32_t tx_freq;           /* 10 Hz units */
    uint32_t tx_offset;         /* 10 Hz units */
    uint8_t  offset_dir;        /* 0=none, 1=plus, 2=minus */

    uint8_t  rx_tone_type;      /* 0=OFF, 1=CTCSS, 2=DCS, 3=DCS-I */
    uint16_t rx_tone_code;
    uint8_t  tx_tone_type;
    uint16_t tx_tone_code;

    uint8_t  modulation;        /* 0=FM, 1=AM, 2=USB */
    uint8_t  bandwidth;         /* 0=Wide, 1=Narrow */

    uint8_t  tx_power;          /* OUTPUT_POWER level */
    uint8_t  squelch_level;     /* 0–9 */

    uint8_t  vox_switch;        /* 0=OFF, 1=ON */
    uint8_t  vox_level;         /* 0–9 */
    uint8_t  vox_delay;         /* release delay (×100ms) */

    uint8_t  mic_gain;          /* 0–4 */
    uint8_t  speaker_gain;      /* 0–15 (REG_48 AF Rx Gain-2) */
    uint8_t  dac_gain;          /* 0–15 (REG_48 low nibble) */

    uint8_t  compander;         /* 0=OFF, 1=TX, 2=RX, 3=both */
    uint8_t  scramble;          /* 0=OFF, 1–10 */
    uint8_t  busy_lock;         /* 0=OFF, 1=ON */
    uint8_t  step_index;
} CatParams_t;

/* Display state for UI */
typedef struct {
    uint32_t rx_freq;
    uint32_t tx_freq;
    uint8_t  tx_power;
    uint8_t  modulation;
    uint8_t  bandwidth;
    uint8_t  vox_switch;
    uint8_t  vox_level;
    uint8_t  squelch_level;
    uint8_t  offset_dir;
    uint32_t tx_offset;
    uint8_t  rx_tone_type;
    uint16_t rx_tone_code;
    uint8_t  tx_tone_type;
    uint16_t tx_tone_code;
    bool     tx_active;
    bool     rx_active;
    bool     heartbeat_ok;
    uint16_t rssi;
} CatDisplayState_t;

extern volatile bool gCatModeEntered;
extern CatParams_t gCatParams;
extern CatDisplayState_t gCatDisplay;

/*
 * Parse a CAT frame from the UART/DMA circular buffer.
 * Returns bytes consumed (>0), 0 if need more data, 1 to skip.
 */
uint16_t CAT_ProcessByte(const uint8_t *buf, uint16_t available,
                         uint16_t buf_size, uint16_t start_idx);

/*
 * Called every 10ms from the main loop for heartbeat/status housekeeping.
 */
void CAT_Poll(void);

#endif
