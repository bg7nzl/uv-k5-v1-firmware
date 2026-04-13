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

#include "app/catmode.h"
#include "audio.h"
#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#include "functions.h"
#include "radio.h"

extern uint16_t gBatteryVoltageAverage;
#include "settings.h"
#include "misc.h"
#include "ui/ui.h"

#if defined(ENABLE_UART)
#include "driver/uart.h"
#endif

#ifdef ENABLE_DIGMODE
#include "app/digmode.h"
#endif

/* ------------------------------------------------------------------ */
/*  Global state                                                       */
/* ------------------------------------------------------------------ */

volatile bool gCatModeEntered = false;
CatParams_t gCatParams;
CatDisplayState_t gCatDisplay;

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static VFO_Info_t sCatVfoBackup;
static uint16_t sHeartbeatCountdown;  /* 10ms ticks */

/* ------------------------------------------------------------------ */
/*  Parameter size lookup                                              */
/* ------------------------------------------------------------------ */

static uint8_t ParamSize(uint8_t param_id)
{
    switch (param_id) {
        case CAT_PARAM_RX_FREQ:
        case CAT_PARAM_TX_FREQ:
        case CAT_PARAM_TX_OFFSET:
            return 4;
        case CAT_PARAM_RX_TONE_CODE:
        case CAT_PARAM_TX_TONE_CODE:
        case CAT_PARAM_RSSI:
            return 2;
        default:
            if (param_id >= CAT_PARAM_OFFSET_DIR && param_id <= CAT_PARAM_MAX)
                return 1;
            return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  UART frame helpers (same pattern as digmode)                       */
/* ------------------------------------------------------------------ */

static void SendFrame(const uint8_t *data, uint8_t len)
{
#if defined(ENABLE_UART)
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++)
        crc ^= data[i];
    UART_Send(data, len);
    UART_Send(&crc, 1);
#endif
}

static void SendAck(uint8_t cmd, uint8_t result)
{
    uint8_t frame[4];
    frame[0] = CAT_SYNC;
    frame[1] = CAT_CMD_ACK;
    frame[2] = 1;
    frame[3] = (result == CAT_RESULT_OK) ? cmd : (cmd | 0x80);
    SendFrame(frame, 4);
}

/* ------------------------------------------------------------------ */
/*  Circular buffer reader                                             */
/* ------------------------------------------------------------------ */

static inline uint8_t CRead(const uint8_t *buf, uint16_t buf_size,
                             uint16_t start, uint16_t off)
{
    return buf[(start + off) % buf_size];
}

static uint32_t CRead32BE(const uint8_t *buf, uint16_t buf_size,
                           uint16_t start, uint16_t off)
{
    return ((uint32_t)CRead(buf, buf_size, start, off)     << 24) |
           ((uint32_t)CRead(buf, buf_size, start, off + 1) << 16) |
           ((uint32_t)CRead(buf, buf_size, start, off + 2) <<  8) |
           ((uint32_t)CRead(buf, buf_size, start, off + 3));
}

static uint16_t CRead16BE(const uint8_t *buf, uint16_t buf_size,
                           uint16_t start, uint16_t off)
{
    return ((uint16_t)CRead(buf, buf_size, start, off) << 8) |
           CRead(buf, buf_size, start, off + 1);
}

/* ------------------------------------------------------------------ */
/*  Heartbeat                                                          */
/* ------------------------------------------------------------------ */

static inline void HeartbeatReset(void)
{
    sHeartbeatCountdown = CAT_HEARTBEAT_TIMEOUT_MS / 10;
}

/* ------------------------------------------------------------------ */
/*  Apply parameters to hardware                                       */
/* ------------------------------------------------------------------ */

static void CAT_ApplyParams(void)
{
    /* Frequency */
    gCurrentVfo->pRX->Frequency = gCatParams.rx_freq;

    switch (gCatParams.offset_dir) {
        case 1:
            gCurrentVfo->pTX->Frequency = gCatParams.rx_freq + gCatParams.tx_offset;
            break;
        case 2:
            gCurrentVfo->pTX->Frequency = gCatParams.rx_freq - gCatParams.tx_offset;
            break;
        default:
            gCurrentVfo->pTX->Frequency = gCatParams.tx_freq;
            break;
    }

    gCurrentVfo->TX_OFFSET_FREQUENCY = gCatParams.tx_offset;
    gCurrentVfo->TX_OFFSET_FREQUENCY_DIRECTION = gCatParams.offset_dir;

    /* Tone codes */
    gCurrentVfo->pRX->CodeType = (DCS_CodeType_t)gCatParams.rx_tone_type;
    gCurrentVfo->pRX->Code     = gCatParams.rx_tone_code;
    gCurrentVfo->pTX->CodeType = (DCS_CodeType_t)gCatParams.tx_tone_type;
    gCurrentVfo->pTX->Code     = gCatParams.tx_tone_code;

    /* Modulation & bandwidth */
    gCurrentVfo->Modulation        = (ModulationMode_t)gCatParams.modulation;
    gCurrentVfo->CHANNEL_BANDWIDTH = gCatParams.bandwidth;

    /* Power */
    gCurrentVfo->OUTPUT_POWER = gCatParams.tx_power;

    /* Other VFO fields */
    gCurrentVfo->Compander      = gCatParams.compander;
    gCurrentVfo->SCRAMBLING_TYPE = gCatParams.scramble;
    gCurrentVfo->BUSY_CHANNEL_LOCK = gCatParams.busy_lock;

    /* Squelch & output power calibration */
    RADIO_ConfigureSquelchAndOutputPower(gCurrentVfo);

    /* Full register reload */
    RADIO_SetupRegisters(true);

    /* Audio path: keep mic and speaker active (unlike digmode which mutes them) */
    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
    BK4819_SetAF(BK4819_AF_FM);

    /* MIC gain (gMicGain_dB2 has 5 entries; clamp index) */
    {
        uint8_t mic_idx = gCatParams.mic_gain;
        if (mic_idx > 4U)
            mic_idx = 4U;
        BK4819_WriteRegister(BK4819_REG_7D, 0xE940 | (gMicGain_dB2[mic_idx] & 0x3F));
    }

    /* Speaker + DAC gain */
    BK4819_WriteRegister(BK4819_REG_48,
        (11u << 12) |
        ( 0u << 10) |
        ((uint16_t)(gCatParams.speaker_gain & 0x0F) << 4) |
        ((uint16_t)(gCatParams.dac_gain & 0x0F)));

    /* VOX */
#ifdef ENABLE_VOX
    if (gCatParams.vox_switch) {
        BK4819_EnableVox(gEeprom.VOX1_THRESHOLD, gEeprom.VOX0_THRESHOLD);
    } else {
        BK4819_DisableVox();
    }
#endif

    /* Update display state */
    gCatDisplay.rx_freq      = gCatParams.rx_freq;
    gCatDisplay.tx_freq      = gCurrentVfo->pTX->Frequency;
    gCatDisplay.tx_power     = gCatParams.tx_power;
    gCatDisplay.modulation   = gCatParams.modulation;
    gCatDisplay.bandwidth    = gCatParams.bandwidth;
    gCatDisplay.vox_switch   = gCatParams.vox_switch;
    gCatDisplay.vox_level    = gCatParams.vox_level;
    gCatDisplay.squelch_level = gCatParams.squelch_level;
    gCatDisplay.offset_dir   = gCatParams.offset_dir;
    gCatDisplay.tx_offset    = gCatParams.tx_offset;
    gCatDisplay.rx_tone_type = gCatParams.rx_tone_type;
    gCatDisplay.rx_tone_code = gCatParams.rx_tone_code;
    gCatDisplay.tx_tone_type = gCatParams.tx_tone_type;
    gCatDisplay.tx_tone_code = gCatParams.tx_tone_code;
    gCatDisplay.heartbeat_ok = true;

    gUpdateDisplay = true;
}

/* ------------------------------------------------------------------ */
/*  Init params from current VFO state                                 */
/* ------------------------------------------------------------------ */

static void CAT_InitParamsFromVfo(void)
{
    gCatParams.rx_freq      = gCurrentVfo->pRX->Frequency;
    gCatParams.tx_freq      = gCurrentVfo->pTX->Frequency;
    gCatParams.tx_offset    = gCurrentVfo->TX_OFFSET_FREQUENCY;
    gCatParams.offset_dir   = gCurrentVfo->TX_OFFSET_FREQUENCY_DIRECTION;

    gCatParams.rx_tone_type = (uint8_t)gCurrentVfo->pRX->CodeType;
    gCatParams.rx_tone_code = gCurrentVfo->pRX->Code;
    gCatParams.tx_tone_type = (uint8_t)gCurrentVfo->pTX->CodeType;
    gCatParams.tx_tone_code = gCurrentVfo->pTX->Code;

    gCatParams.modulation   = (uint8_t)gCurrentVfo->Modulation;
    gCatParams.bandwidth    = gCurrentVfo->CHANNEL_BANDWIDTH;
    gCatParams.tx_power     = gCurrentVfo->OUTPUT_POWER;
    gCatParams.squelch_level = 1;

    gCatParams.vox_switch   = gEeprom.VOX_SWITCH ? 1 : 0;
    gCatParams.vox_level    = gEeprom.VOX_LEVEL;
    gCatParams.vox_delay    = 10;  /* 1000ms default */

    gCatParams.mic_gain     = gEeprom.MIC_SENSITIVITY;
    gCatParams.speaker_gain = gEeprom.VOLUME_GAIN;
    gCatParams.dac_gain     = gEeprom.DAC_GAIN;

    gCatParams.compander    = gCurrentVfo->Compander;
    gCatParams.scramble     = gCurrentVfo->SCRAMBLING_TYPE;
    gCatParams.busy_lock    = gCurrentVfo->BUSY_CHANNEL_LOCK;
    gCatParams.step_index   = (uint8_t)gCurrentVfo->STEP_SETTING;
}

/* ------------------------------------------------------------------ */
/*  Enter / Exit CAT mode                                              */
/* ------------------------------------------------------------------ */

static void DoEnterCat(void)
{
#ifdef ENABLE_DIGMODE
    if (gDigmodeEntered) {
        SendAck(CAT_CMD_ENTER, CAT_RESULT_ERR);
        return;
    }
#endif

    if (!gCatModeEntered) {
        /* Backup current VFO */
        sCatVfoBackup = *gCurrentVfo;

        CAT_InitParamsFromVfo();
        gCatModeEntered = true;

        GUI_SelectNextDisplay(DISPLAY_CATMODE);
        CAT_ApplyParams();
    }

    HeartbeatReset();
    SendAck(CAT_CMD_ENTER, CAT_RESULT_OK);
}

static void DoExitCat(void)
{
    if (!gCatModeEntered) {
        SendAck(CAT_CMD_EXIT, CAT_RESULT_OK);
        return;
    }

    /* Stop TX if active */
    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        FUNCTION_Select(FUNCTION_FOREGROUND);
    }

    /* Restore VFO */
    *gCurrentVfo = sCatVfoBackup;
    gCurrentVfo->pRX = &gCurrentVfo->freq_config_RX;
    gCurrentVfo->pTX = &gCurrentVfo->freq_config_TX;

    RADIO_SetupRegisters(true);

    gCatModeEntered = false;
    gCatDisplay.heartbeat_ok = false;

    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;

    SendAck(CAT_CMD_EXIT, CAT_RESULT_OK);
}

/* ------------------------------------------------------------------ */
/*  SET_PARAM handler                                                  */
/* ------------------------------------------------------------------ */

static void HandleSetParam(const uint8_t *buf, uint16_t buf_size,
                           uint16_t start_idx, uint8_t len)
{
    if (len < 2)
        return;

    uint8_t param_id = CRead(buf, buf_size, start_idx, 3);
    uint8_t psize = ParamSize(param_id);
    if (psize == 0 || len < 1 + psize)
        return;

    uint32_t val32 = 0;
    uint16_t val16 = 0;
    uint8_t  val8  = 0;

    switch (psize) {
        case 4:
            val32 = CRead32BE(buf, buf_size, start_idx, 4);
            break;
        case 2:
            val16 = CRead16BE(buf, buf_size, start_idx, 4);
            break;
        case 1:
            val8 = CRead(buf, buf_size, start_idx, 4);
            break;
    }

    switch (param_id) {
        case CAT_PARAM_RX_FREQ:      gCatParams.rx_freq      = val32; break;
        case CAT_PARAM_TX_FREQ:      gCatParams.tx_freq      = val32; break;
        case CAT_PARAM_TX_OFFSET:    gCatParams.tx_offset    = val32; break;
        case CAT_PARAM_OFFSET_DIR:   gCatParams.offset_dir   = val8;  break;
        case CAT_PARAM_RX_TONE_TYPE: gCatParams.rx_tone_type = val8;  break;
        case CAT_PARAM_RX_TONE_CODE: gCatParams.rx_tone_code = val16; break;
        case CAT_PARAM_TX_TONE_TYPE: gCatParams.tx_tone_type = val8;  break;
        case CAT_PARAM_TX_TONE_CODE: gCatParams.tx_tone_code = val16; break;
        case CAT_PARAM_MODULATION:   gCatParams.modulation   = val8;  break;
        case CAT_PARAM_TX_POWER:     gCatParams.tx_power     = val8;  break;
        case CAT_PARAM_BANDWIDTH:    gCatParams.bandwidth    = val8;  break;
        case CAT_PARAM_SQUELCH:      gCatParams.squelch_level = val8; break;
        case CAT_PARAM_VOX_SWITCH:   gCatParams.vox_switch   = val8;  break;
        case CAT_PARAM_VOX_LEVEL:    gCatParams.vox_level    = val8;  break;
        case CAT_PARAM_VOX_DELAY:    gCatParams.vox_delay    = val8;  break;
        case CAT_PARAM_MIC_GAIN:     gCatParams.mic_gain     = val8;  break;
        case CAT_PARAM_SPEAKER_GAIN: gCatParams.speaker_gain = val8;  break;
        case CAT_PARAM_DAC_GAIN:     gCatParams.dac_gain     = val8;  break;
        case CAT_PARAM_COMPANDER:    gCatParams.compander    = val8;  break;
        case CAT_PARAM_SCRAMBLE:     gCatParams.scramble     = val8;  break;
        case CAT_PARAM_BUSY_LOCK:    gCatParams.busy_lock    = val8;  break;
        case CAT_PARAM_STEP:         gCatParams.step_index   = val8;  break;
        default:
            SendAck(CAT_CMD_SET_PARAM, CAT_RESULT_ERR);
            return;
    }

    SendAck(CAT_CMD_SET_PARAM, CAT_RESULT_OK);
}

/* ------------------------------------------------------------------ */
/*  SET_MULTI handler                                                  */
/* ------------------------------------------------------------------ */

static void HandleSetMulti(const uint8_t *buf, uint16_t buf_size,
                           uint16_t start_idx, uint8_t len)
{
    if (len < 1)
        return;

    uint8_t count = CRead(buf, buf_size, start_idx, 3);
    uint16_t offset = 4;

    for (uint8_t i = 0; i < count; i++) {
        if (offset - 3 >= len)
            break;

        uint8_t param_id = CRead(buf, buf_size, start_idx, offset);
        uint8_t psize = ParamSize(param_id);
        if (psize == 0 || offset + 1 + psize - 3 > len)
            break;

        offset++;

        uint32_t val32 = 0;
        uint16_t val16 = 0;
        uint8_t  val8  = 0;

        switch (psize) {
            case 4:
                val32 = CRead32BE(buf, buf_size, start_idx, offset);
                break;
            case 2:
                val16 = CRead16BE(buf, buf_size, start_idx, offset);
                break;
            case 1:
                val8 = CRead(buf, buf_size, start_idx, offset);
                break;
        }

        offset += psize;

        switch (param_id) {
            case CAT_PARAM_RX_FREQ:      gCatParams.rx_freq      = val32; break;
            case CAT_PARAM_TX_FREQ:      gCatParams.tx_freq      = val32; break;
            case CAT_PARAM_TX_OFFSET:    gCatParams.tx_offset    = val32; break;
            case CAT_PARAM_OFFSET_DIR:   gCatParams.offset_dir   = val8;  break;
            case CAT_PARAM_RX_TONE_TYPE: gCatParams.rx_tone_type = val8;  break;
            case CAT_PARAM_RX_TONE_CODE: gCatParams.rx_tone_code = val16; break;
            case CAT_PARAM_TX_TONE_TYPE: gCatParams.tx_tone_type = val8;  break;
            case CAT_PARAM_TX_TONE_CODE: gCatParams.tx_tone_code = val16; break;
            case CAT_PARAM_MODULATION:   gCatParams.modulation   = val8;  break;
            case CAT_PARAM_TX_POWER:     gCatParams.tx_power     = val8;  break;
            case CAT_PARAM_BANDWIDTH:    gCatParams.bandwidth    = val8;  break;
            case CAT_PARAM_SQUELCH:      gCatParams.squelch_level = val8; break;
            case CAT_PARAM_VOX_SWITCH:   gCatParams.vox_switch   = val8;  break;
            case CAT_PARAM_VOX_LEVEL:    gCatParams.vox_level    = val8;  break;
            case CAT_PARAM_VOX_DELAY:    gCatParams.vox_delay    = val8;  break;
            case CAT_PARAM_MIC_GAIN:     gCatParams.mic_gain     = val8;  break;
            case CAT_PARAM_SPEAKER_GAIN: gCatParams.speaker_gain = val8;  break;
            case CAT_PARAM_DAC_GAIN:     gCatParams.dac_gain     = val8;  break;
            case CAT_PARAM_COMPANDER:    gCatParams.compander    = val8;  break;
            case CAT_PARAM_SCRAMBLE:     gCatParams.scramble     = val8;  break;
            case CAT_PARAM_BUSY_LOCK:    gCatParams.busy_lock    = val8;  break;
            case CAT_PARAM_STEP:         gCatParams.step_index   = val8;  break;
            default:
                break;
        }
    }

    SendAck(CAT_CMD_SET_MULTI, CAT_RESULT_OK);
}

/* ------------------------------------------------------------------ */
/*  GET_PARAM / PARAM_RESP helper                                      */
/* ------------------------------------------------------------------ */

static void SendParamResp(uint8_t param_id)
{
    uint8_t psize = ParamSize(param_id);
    if (psize == 0)
        return;

    uint8_t frame[8];  /* max: SYNC(1)+CMD(1)+LEN(1)+id(1)+val(4) = 8 */
    frame[0] = CAT_SYNC;
    frame[1] = CAT_CMD_PARAM_RESP;
    frame[2] = 1 + psize;
    frame[3] = param_id;

    uint32_t val32 = 0;
    uint16_t val16 = 0;
    uint8_t  val8  = 0;

    switch (param_id) {
        case CAT_PARAM_RX_FREQ:      val32 = gCatParams.rx_freq;      break;
        case CAT_PARAM_TX_FREQ:      val32 = gCatParams.tx_freq;      break;
        case CAT_PARAM_TX_OFFSET:    val32 = gCatParams.tx_offset;    break;
        case CAT_PARAM_OFFSET_DIR:   val8  = gCatParams.offset_dir;   break;
        case CAT_PARAM_RX_TONE_TYPE: val8  = gCatParams.rx_tone_type; break;
        case CAT_PARAM_RX_TONE_CODE: val16 = gCatParams.rx_tone_code; break;
        case CAT_PARAM_TX_TONE_TYPE: val8  = gCatParams.tx_tone_type; break;
        case CAT_PARAM_TX_TONE_CODE: val16 = gCatParams.tx_tone_code; break;
        case CAT_PARAM_MODULATION:   val8  = gCatParams.modulation;   break;
        case CAT_PARAM_TX_POWER:     val8  = gCatParams.tx_power;     break;
        case CAT_PARAM_BANDWIDTH:    val8  = gCatParams.bandwidth;    break;
        case CAT_PARAM_SQUELCH:      val8  = gCatParams.squelch_level; break;
        case CAT_PARAM_VOX_SWITCH:   val8  = gCatParams.vox_switch;   break;
        case CAT_PARAM_VOX_LEVEL:    val8  = gCatParams.vox_level;    break;
        case CAT_PARAM_VOX_DELAY:    val8  = gCatParams.vox_delay;    break;
        case CAT_PARAM_MIC_GAIN:     val8  = gCatParams.mic_gain;     break;
        case CAT_PARAM_SPEAKER_GAIN: val8  = gCatParams.speaker_gain; break;
        case CAT_PARAM_DAC_GAIN:     val8  = gCatParams.dac_gain;     break;
        case CAT_PARAM_COMPANDER:    val8  = gCatParams.compander;    break;
        case CAT_PARAM_SCRAMBLE:     val8  = gCatParams.scramble;     break;
        case CAT_PARAM_BUSY_LOCK:    val8  = gCatParams.busy_lock;    break;
        case CAT_PARAM_STEP:         val8  = gCatParams.step_index;   break;
        case CAT_PARAM_MIC_BAR: {
            uint16_t amp;
            BK4819_GetVoxAmp(&amp);
            val8 = (amp > 255) ? 255 : (uint8_t)amp;
            break;
        }
        case CAT_PARAM_RSSI:
            val16 = BK4819_GetRSSI();
            break;
        default:
            return;
    }

    switch (psize) {
        case 4:
            frame[4] = (val32 >> 24) & 0xFF;
            frame[5] = (val32 >> 16) & 0xFF;
            frame[6] = (val32 >>  8) & 0xFF;
            frame[7] = (val32 >>  0) & 0xFF;
            break;
        case 2:
            frame[4] = (val16 >> 8) & 0xFF;
            frame[5] = val16 & 0xFF;
            break;
        case 1:
            frame[4] = val8;
            break;
    }

    SendFrame(frame, 3 + 1 + psize);
}

static void HandleGetParam(const uint8_t *buf, uint16_t buf_size,
                           uint16_t start_idx, uint8_t len)
{
    if (len < 1)
        return;

    uint8_t param_id = CRead(buf, buf_size, start_idx, 3);
    SendParamResp(param_id);
}

/* ------------------------------------------------------------------ */
/*  GET_ALL / ALL_RESP                                                 */
/* ------------------------------------------------------------------ */

static void HandleGetAll(void)
{
    for (uint8_t id = CAT_PARAM_RX_FREQ; id <= CAT_PARAM_MAX; id++)
        SendParamResp(id);
}

/* ------------------------------------------------------------------ */
/*  STATUS                                                             */
/* ------------------------------------------------------------------ */

static void SendStatusResp(void)
{
    uint16_t rssi = BK4819_GetRSSI();
    uint16_t batt = gBatteryVoltageAverage;

    uint8_t frame[11];
    frame[0]  = CAT_SYNC;
    frame[1]  = CAT_CMD_STATUS_RESP;
    frame[2]  = 8;
    frame[3]  = (gCurrentFunction == FUNCTION_TRANSMIT) ? 1 : 0;
    frame[4]  = (gCurrentFunction == FUNCTION_RECEIVE)  ? 1 : 0;
    frame[5]  = (rssi >> 8) & 0xFF;
    frame[6]  = rssi & 0xFF;
    frame[7]  = (batt >> 8) & 0xFF;
    frame[8]  = batt & 0xFF;
#ifdef ENABLE_VOX
    frame[9]  = gVOX_NoiseDetected ? 1 : 0;
#else
    frame[9]  = 0;
#endif
    frame[10] = 0;  /* temperature placeholder */
    SendFrame(frame, 11);

    gCatDisplay.tx_active = (gCurrentFunction == FUNCTION_TRANSMIT);
    gCatDisplay.rx_active = (gCurrentFunction == FUNCTION_RECEIVE);
    gCatDisplay.rssi      = rssi;
}

/* ------------------------------------------------------------------ */
/*  Frame parser                                                       */
/* ------------------------------------------------------------------ */

uint16_t CAT_ProcessByte(const uint8_t *buf, uint16_t available,
                         uint16_t buf_size, uint16_t start_idx)
{
    if (available < 4)
        return 0;

    uint8_t sync = CRead(buf, buf_size, start_idx, 0);
    if (sync != CAT_SYNC)
        return 1;

    uint8_t cmd = CRead(buf, buf_size, start_idx, 1);
    if (cmd < CAT_CMD_MIN || cmd > CAT_CMD_MAX)
        return 1;

    uint8_t len = CRead(buf, buf_size, start_idx, 2);
    uint16_t frame_size = 3 + len + 1;  /* SYNC+CMD+LEN + payload + CRC */

    if (available < frame_size)
        return 0;

    /* CRC check */
    uint8_t crc = 0;
    for (uint16_t i = 0; i < frame_size - 1; i++)
        crc ^= CRead(buf, buf_size, start_idx, i);
    uint8_t rx_crc = CRead(buf, buf_size, start_idx, frame_size - 1);

    if (crc != rx_crc) {
        SendAck(cmd, CAT_RESULT_ERR);
        return frame_size;
    }

    HeartbeatReset();

    switch (cmd) {
        case CAT_CMD_ENTER:
            DoEnterCat();
            break;

        case CAT_CMD_EXIT:
            DoExitCat();
            break;

        case CAT_CMD_SET_PARAM:
            if (gCatModeEntered)
                HandleSetParam(buf, buf_size, start_idx, len);
            else
                SendAck(cmd, CAT_RESULT_ERR);
            break;

        case CAT_CMD_GET_PARAM:
            if (gCatModeEntered)
                HandleGetParam(buf, buf_size, start_idx, len);
            else
                SendAck(cmd, CAT_RESULT_ERR);
            break;

        case CAT_CMD_SET_MULTI:
            if (gCatModeEntered)
                HandleSetMulti(buf, buf_size, start_idx, len);
            else
                SendAck(cmd, CAT_RESULT_ERR);
            break;

        case CAT_CMD_GET_ALL:
            if (gCatModeEntered)
                HandleGetAll();
            else
                SendAck(cmd, CAT_RESULT_ERR);
            break;

        case CAT_CMD_APPLY:
            if (gCatModeEntered) {
                CAT_ApplyParams();
                SendAck(CAT_CMD_APPLY, CAT_RESULT_OK);
            } else {
                SendAck(cmd, CAT_RESULT_ERR);
            }
            break;

        case CAT_CMD_STATUS:
            if (gCatModeEntered)
                SendStatusResp();
            else
                SendAck(cmd, CAT_RESULT_ERR);
            break;

        case CAT_CMD_NOOP:
            /* Heartbeat — already reset above, no response needed */
            break;

        default:
            break;
    }

    return frame_size;
}

/* ------------------------------------------------------------------ */
/*  10ms poll — heartbeat watchdog                                     */
/* ------------------------------------------------------------------ */

void CAT_Poll(void)
{
    if (!gCatModeEntered)
        return;

    if (sHeartbeatCountdown > 0) {
        sHeartbeatCountdown--;
        if (sHeartbeatCountdown == 0) {
            /* Link lost — stop TX if active, but stay in CAT mode */
            if (gCurrentFunction == FUNCTION_TRANSMIT) {
                FUNCTION_Select(FUNCTION_FOREGROUND);
            }
#ifdef ENABLE_VOX
            BK4819_DisableVox();
#endif
            gCatDisplay.heartbeat_ok = false;
            gUpdateDisplay = true;
        }
    }

    /* Update display RSSI periodically */
    gCatDisplay.rssi = BK4819_GetRSSI();
    gCatDisplay.tx_active = (gCurrentFunction == FUNCTION_TRANSMIT);
    gCatDisplay.rx_active = (gCurrentFunction == FUNCTION_RECEIVE);
}
