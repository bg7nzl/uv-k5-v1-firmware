/* Copyright 2023 Dual Tachyon
 * Minimal FM screen: fixed 64–108 MHz label + large frequency.
 */

#ifdef ENABLE_FMRADIO

#include "external/printf/printf.h"

#include "driver/st7565.h"
#include "settings.h"
#include "ui/fmradio.h"
#include "ui/helper.h"
#include "ui/ui.h"

void UI_DisplayFM(void)
{
	char String[20];

	UI_DisplayClear();

	UI_PrintString("FM", 2, 0, 0, 8);

	UI_PrintStringSmallNormal("64-108MHz", 1, 0, 6);

	sprintf(String, "%3u.%u",
		(unsigned)(gEeprom.FM_FrequencyPlaying / 10),
		(unsigned)(gEeprom.FM_FrequencyPlaying % 10));
	UI_DisplayFrequency(String, 36, 1, true);

	ST7565_BlitFullScreen();
}

#endif
