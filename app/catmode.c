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

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static uint16_t sHeartbeatCountdown;  /* 10ms ticks */

/* Saved VOX params that we override while in CAT (read from gEeprom on enter) */
static uint8_t sSavedVoxSwitch;
static uint8_t sSavedVoxLevel;
static uint8_t sSavedVoxDelay;

/* ------------------------------------------------------------------ */
/*  K1 -> K5 power mapping                                             */
/*  K1: 0-4=LOW1..LOW5, 5=MID, 6=HIGH, 7=HIGH                        */
/*  K5: 0=LOW, 1=MID, 2=HIGH                                          */
/* ------------------------------------------------------------------ */

static uint8_t MapPowerK1toK5(uint8_t k1_power)
{
    if (k1_power <= 4) return 0;  /* LOW */
    if (k1_power == 5) return 1;  /* MID */
    return 2;                     /* HIGH */
}

static uint8_t MapPowerK5toK1(uint8_t k5_power)
{
    switch (k5_power) {
        case 0: return 0;   /* LOW  -> LOW1 */
        case 1: return 5;   /* MID  -> MID  */
        case 2: return 6;   /* HIGH -> HIGH */
        default: return 0;
    }
}

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
/*  UART frame helpers                                                 */
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
/*  Apply current VFO settings to hardware                             */
/* ------------------------------------------------------------------ */

static void CAT_ApplyHardware(void)
{
    RADIO_ConfigureSquelchAndOutputPower(gCurrentVfo);
    RADIO_SetupRegisters(true);

    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
    BK4819_SetAF(BK4819_AF_FM);

    {
        uint8_t mic_idx = gEeprom.MIC_SENSITIVITY;
        if (mic_idx > 4U)
            mic_idx = 4U;
        BK4819_WriteRegister(BK4819_REG_7D, 0xE940 | (gMicGain_dB2[mic_idx] & 0x3F));
    }

    BK4819_WriteRegister(BK4819_REG_48,
        (11u << 12) |
        ( 0u << 10) |
        ((uint16_t)(gEeprom.VOLUME_GAIN & 0x0F) << 4) |
        ((uint16_t)(gEeprom.DAC_GAIN & 0x0F)));

#ifdef ENABLE_VOX
    if (gEeprom.VOX_SWITCH) {
        BK4819_EnableVox(gEeprom.VOX1_THRESHOLD, gEeprom.VOX0_THRESHOLD);
    } else {
        BK4819_DisableVox();
    }
#endif

    gUpdateDisplay = true;
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
        uint8_t vfo = gEeprom.TX_VFO;

        /* Force current channel into VFO (frequency) mode if it's MR */
        if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo])) {
            gEeprom.ScreenChannel[vfo] = gEeprom.FreqChannel[vfo];
            gRequestSaveVFO   = true;
            gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
        }

        /* Save VOX state so we can apply CAT-requested VOX separately */
        sSavedVoxSwitch = gEeprom.VOX_SWITCH;
        sSavedVoxLevel  = gEeprom.VOX_LEVEL;
        sSavedVoxDelay  = 10;

        gCatModeEntered = true;

        GUI_SelectNextDisplay(DISPLAY_MAIN);
        CAT_ApplyHardware();
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

    if (gCurrentFunction == FUNCTION_TRANSMIT) {
        FUNCTION_Select(FUNCTION_FOREGROUND);
    }

    gCatModeEntered = false;

    RADIO_SetupRegisters(true);

    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;

    SendAck(CAT_CMD_EXIT, CAT_RESULT_OK);
}

/* ------------------------------------------------------------------ */
/*  Write a single parameter to the current VFO / gEeprom             */
/* ------------------------------------------------------------------ */

static void WriteParam(uint8_t param_id, uint32_t val32, uint16_t val16, uint8_t val8)
{
    switch (param_id) {
        case CAT_PARAM_RX_FREQ:
            gCurrentVfo->pRX->Frequency = val32;
            break;
        case CAT_PARAM_TX_FREQ:
            gCurrentVfo->pTX->Frequency = val32;
            break;
        case CAT_PARAM_TX_OFFSET:
            gCurrentVfo->TX_OFFSET_FREQUENCY = val32;
            break;
        case CAT_PARAM_OFFSET_DIR:
            gCurrentVfo->TX_OFFSET_FREQUENCY_DIRECTION = val8;
            break;
        case CAT_PARAM_RX_TONE_TYPE:
            gCurrentVfo->pRX->CodeType = (DCS_CodeType_t)val8;
            break;
        case CAT_PARAM_RX_TONE_CODE:
            gCurrentVfo->pRX->Code = val16;
            break;
        case CAT_PARAM_TX_TONE_TYPE:
            gCurrentVfo->pTX->CodeType = (DCS_CodeType_t)val8;
            break;
        case CAT_PARAM_TX_TONE_CODE:
            gCurrentVfo->pTX->Code = val16;
            break;
        case CAT_PARAM_MODULATION:
            gCurrentVfo->Modulation = (ModulationMode_t)val8;
            break;
        case CAT_PARAM_TX_POWER:
            gCurrentVfo->OUTPUT_POWER = MapPowerK1toK5(val8);
            break;
        case CAT_PARAM_BANDWIDTH:
            gCurrentVfo->CHANNEL_BANDWIDTH = val8;
            break;
        case CAT_PARAM_SQUELCH:
            gEeprom.SQUELCH_LEVEL = val8;
            break;
        case CAT_PARAM_VOX_SWITCH:
            gEeprom.VOX_SWITCH = val8;
            break;
        case CAT_PARAM_VOX_LEVEL:
            gEeprom.VOX_LEVEL = val8;
            break;
        case CAT_PARAM_VOX_DELAY:
            sSavedVoxDelay = val8;
            break;
        case CAT_PARAM_MIC_GAIN:
            gEeprom.MIC_SENSITIVITY = (val8 > 4) ? 4 : val8;
            break;
        case CAT_PARAM_SPEAKER_GAIN:
            gEeprom.VOLUME_GAIN = val8 & 0x0F;
            break;
        case CAT_PARAM_DAC_GAIN:
            gEeprom.DAC_GAIN = val8 & 0x0F;
            break;
        case CAT_PARAM_COMPANDER:
            gCurrentVfo->Compander = val8;
            break;
        case CAT_PARAM_SCRAMBLE:
            gCurrentVfo->SCRAMBLING_TYPE = val8;
            break;
        case CAT_PARAM_BUSY_LOCK:
            gCurrentVfo->BUSY_CHANNEL_LOCK = val8;
            break;
        case CAT_PARAM_STEP:
            gCurrentVfo->STEP_SETTING = val8;
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Read a single parameter from the current VFO / gEeprom            */
/* ------------------------------------------------------------------ */

static void ReadParam(uint8_t param_id, uint32_t *out32, uint16_t *out16, uint8_t *out8)
{
    switch (param_id) {
        case CAT_PARAM_RX_FREQ:      *out32 = gCurrentVfo->pRX->Frequency; break;
        case CAT_PARAM_TX_FREQ:      *out32 = gCurrentVfo->pTX->Frequency; break;
        case CAT_PARAM_TX_OFFSET:    *out32 = gCurrentVfo->TX_OFFSET_FREQUENCY; break;
        case CAT_PARAM_OFFSET_DIR:   *out8  = gCurrentVfo->TX_OFFSET_FREQUENCY_DIRECTION; break;
        case CAT_PARAM_RX_TONE_TYPE: *out8  = (uint8_t)gCurrentVfo->pRX->CodeType; break;
        case CAT_PARAM_RX_TONE_CODE: *out16 = gCurrentVfo->pRX->Code; break;
        case CAT_PARAM_TX_TONE_TYPE: *out8  = (uint8_t)gCurrentVfo->pTX->CodeType; break;
        case CAT_PARAM_TX_TONE_CODE: *out16 = gCurrentVfo->pTX->Code; break;
        case CAT_PARAM_MODULATION:   *out8  = (uint8_t)gCurrentVfo->Modulation; break;
        case CAT_PARAM_TX_POWER:     *out8  = MapPowerK5toK1(gCurrentVfo->OUTPUT_POWER); break;
        case CAT_PARAM_BANDWIDTH:    *out8  = gCurrentVfo->CHANNEL_BANDWIDTH; break;
        case CAT_PARAM_SQUELCH:      *out8  = gEeprom.SQUELCH_LEVEL; break;
        case CAT_PARAM_VOX_SWITCH:   *out8  = gEeprom.VOX_SWITCH ? 1 : 0; break;
        case CAT_PARAM_VOX_LEVEL:    *out8  = gEeprom.VOX_LEVEL; break;
        case CAT_PARAM_VOX_DELAY:    *out8  = sSavedVoxDelay; break;
        case CAT_PARAM_MIC_GAIN:     *out8  = gEeprom.MIC_SENSITIVITY; break;
        case CAT_PARAM_SPEAKER_GAIN: *out8  = gEeprom.VOLUME_GAIN; break;
        case CAT_PARAM_DAC_GAIN:     *out8  = gEeprom.DAC_GAIN; break;
        case CAT_PARAM_COMPANDER:    *out8  = gCurrentVfo->Compander; break;
        case CAT_PARAM_SCRAMBLE:     *out8  = gCurrentVfo->SCRAMBLING_TYPE; break;
        case CAT_PARAM_BUSY_LOCK:    *out8  = gCurrentVfo->BUSY_CHANNEL_LOCK; break;
        case CAT_PARAM_STEP:         *out8  = (uint8_t)gCurrentVfo->STEP_SETTING; break;
        case CAT_PARAM_MIC_BAR: {
            uint16_t amp;
            BK4819_GetVoxAmp(&amp);
            *out8 = (amp > 255) ? 255 : (uint8_t)amp;
            break;
        }
        case CAT_PARAM_RSSI:
            *out16 = BK4819_GetRSSI();
            break;
        default:
            break;
    }
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
        case 4: val32 = CRead32BE(buf, buf_size, start_idx, 4); break;
        case 2: val16 = CRead16BE(buf, buf_size, start_idx, 4); break;
        case 1: val8  = CRead(buf, buf_size, start_idx, 4);     break;
    }

    WriteParam(param_id, val32, val16, val8);
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
            case 4: val32 = CRead32BE(buf, buf_size, start_idx, offset); break;
            case 2: val16 = CRead16BE(buf, buf_size, start_idx, offset); break;
            case 1: val8  = CRead(buf, buf_size, start_idx, offset);     break;
        }

        offset += psize;
        WriteParam(param_id, val32, val16, val8);
    }

    SendAck(CAT_CMD_SET_MULTI, CAT_RESULT_OK);
}

/* ------------------------------------------------------------------ */
/*  GET_PARAM / PARAM_RESP                                             */
/* ------------------------------------------------------------------ */

static void SendParamResp(uint8_t param_id)
{
    uint8_t psize = ParamSize(param_id);
    if (psize == 0)
        return;

    uint8_t frame[8];
    frame[0] = CAT_SYNC;
    frame[1] = CAT_CMD_PARAM_RESP;
    frame[2] = 1 + psize;
    frame[3] = param_id;

    uint32_t val32 = 0;
    uint16_t val16 = 0;
    uint8_t  val8  = 0;

    ReadParam(param_id, &val32, &val16, &val8);

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
    SendParamResp(CRead(buf, buf_size, start_idx, 3));
}

/* ------------------------------------------------------------------ */
/*  GET_ALL                                                            */
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
    frame[10] = 0;
    SendFrame(frame, 11);
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
    uint16_t frame_size = 3 + len + 1;

    if (available < frame_size)
        return 0;

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
                CAT_ApplyHardware();
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
            break;

        default:
            break;
    }

    return frame_size;
}

/* ------------------------------------------------------------------ */
/*  10ms poll - heartbeat watchdog                                     */
/* ------------------------------------------------------------------ */

void CAT_Poll(void)
{
    if (!gCatModeEntered)
        return;

    if (sHeartbeatCountdown > 0) {
        sHeartbeatCountdown--;
        if (sHeartbeatCountdown == 0) {
            if (gCurrentFunction == FUNCTION_TRANSMIT) {
                FUNCTION_Select(FUNCTION_FOREGROUND);
            }
#ifdef ENABLE_VOX
            BK4819_DisableVox();
#endif
            gUpdateDisplay = true;
        }
    }
}
