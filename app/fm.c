/* Copyright 2023 Dual Tachyon
 * Minimal FM: fixed 64.0–108.0 MHz (640–1080 × 0.1 MHz), UP/DOWN hold = step,
 * EXIT = leave. BK1080 band auto: <76 MHz → band 3, else → band 1.
 * Enter FM via F+0 only (side key ACTION_OPT_FM is NOP).
 */

#ifdef ENABLE_FMRADIO

#include "app/action.h"
#include "app/fm.h"
#include "audio.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/bk1080.h"
#include "driver/gpio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

/* Whole broadcast span in firmware units (0.1 MHz). */
#define FM_FREQ_MIN 640u
#define FM_FREQ_MAX 1080u

uint16_t          gFM_Channels[20];
bool              gFmRadioMode;
uint8_t           gFmRadioCountdown_500ms;
volatile uint16_t gFmPlayCountdown_10ms;
volatile int8_t   gFM_ScanState;
bool              gFM_AutoScan;
uint8_t           gFM_ChannelPosition;
bool              gFM_FoundFrequency;
uint16_t          gFM_RestoreCountdown_10ms;

static const uint8_t BUTTON_STATE_PRESSED = 1u << 0;
static const uint8_t BUTTON_STATE_HELD    = 1u << 1;
#define BUTTON_EVENT_PRESSED (BUTTON_STATE_PRESSED)
#define BUTTON_EVENT_HELD    (BUTTON_STATE_PRESSED | BUTTON_STATE_HELD)
#define BUTTON_EVENT_SHORT   0

/* BK1080 channel math is per-band; 64–76 MHz uses band 3, 76–108 MHz uses band 1. */
static uint8_t fm_hw_band(uint16_t f_mhz10)
{
	return (f_mhz10 < 760u) ? 3u : 1u;
}

static void clamp_fm_frequency(void)
{
	uint16_t f = gEeprom.FM_SelectedFrequency;

	if (f < FM_FREQ_MIN)
		f = FM_FREQ_MIN;
	else if (f > FM_FREQ_MAX)
		f = FM_FREQ_MAX;

	gEeprom.FM_Band               = fm_hw_band(f);
	gEeprom.FM_SelectedFrequency  = f;
	gEeprom.FM_FrequencyPlaying     = f;
}

int FM_ConfigureChannelState(void)
{
	gEeprom.FM_IsMrMode = false;
	gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
	clamp_fm_frequency();
	return 0;
}

void FM_TurnOff(void)
{
	gFmRadioMode              = false;
	gFM_ScanState             = FM_SCAN_OFF;
	gFM_RestoreCountdown_10ms = 0;
	gScheduleFM               = false;
	gFM_AutoScan              = false;

	AUDIO_AudioPathOff();
	gEnableSpeaker = false;

	BK1080_Init0();

	gUpdateStatus = true;
}

void FM_EraseChannels(void)
{
}

void FM_PlayAndUpdate(void)
{
	gFM_ScanState     = FM_SCAN_OFF;
	gScheduleFM       = false;
	gFM_AutoScan      = false;
	FM_ConfigureChannelState();
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band);
	gFmPlayCountdown_10ms = 0;
	gAskToSave            = false;
	gAskToDelete          = false;
	AUDIO_AudioPathOn();
	gEnableSpeaker        = true;
}

void FM_Play(void)
{
}

void FM_Start(void)
{
	FM_ConfigureChannelState();
	gDualWatchActive            = false;
	gFmRadioMode                = true;
	gFM_ScanState               = FM_SCAN_OFF;
	gFM_RestoreCountdown_10ms   = 0;
	gFM_AutoScan                = false;
	gScheduleFM                 = false;
	gFmPlayCountdown_10ms       = 0;
	gInputBoxIndex              = 0;
	gAskToSave                  = false;
	gAskToDelete                = false;

	BK1080_Init(gEeprom.FM_FrequencyPlaying, gEeprom.FM_Band);

	AUDIO_AudioPathOn();
	gEnableSpeaker = true;
	gUpdateStatus  = true;
}

static void fm_step_frequency(int8_t step)
{
	uint16_t f = gEeprom.FM_SelectedFrequency + (int16_t)step;

	if (f < FM_FREQ_MIN)
		f = FM_FREQ_MAX;
	else if (f > FM_FREQ_MAX)
		f = FM_FREQ_MIN;

	gEeprom.FM_Band              = fm_hw_band(f);
	gEeprom.FM_SelectedFrequency = f;
	gEeprom.FM_FrequencyPlaying  = f;
	BK1080_SetFrequency(f, gEeprom.FM_Band);
	gRequestSaveFM               = true;
	gRequestDisplayScreen        = DISPLAY_FM;
	gBeepToPlay                  = BEEP_1KHZ_60MS_OPTIONAL;
}

static void Key_UP_DOWN(uint8_t state, int8_t step)
{
	if (state == BUTTON_EVENT_PRESSED) {
		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
		return;
	}
	if (state != BUTTON_EVENT_HELD)
		return;
	fm_step_frequency(step);
}

static void Key_EXIT(uint8_t state)
{
	if (state != BUTTON_EVENT_SHORT)
		return;
	gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
	ACTION_FM();
}

void FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	const uint8_t state = (uint8_t)(bKeyPressed + 2u * bKeyHeld);

	switch (Key) {
	case KEY_UP:
		Key_UP_DOWN(state, 1);
		break;
	case KEY_DOWN:
		Key_UP_DOWN(state, -1);
		break;
	case KEY_EXIT:
		Key_EXIT(state);
		break;
	default:
		/* PTT / F / side / digits / * / MENU: ignored (also blocked in app.c) */
		break;
	}
}

#endif
