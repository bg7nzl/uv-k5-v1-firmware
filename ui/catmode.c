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

#include "ui/catmode.h"
#include "app/catmode.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "ui/helper.h"
#include "ui/ui.h"

/*
 * Compact CAT status screen (flash-sized); power/mod are numeric codes.
 */

void UI_DisplayCatmode(void)
{
	char buf[32];

	UI_DisplayClear();

	const CatDisplayState_t *d = &gCatDisplay;

	if (d->tx_active)
		UI_PrintStringSmallBold("CAT TX", 0, 0, 0);
	else if (d->rx_active)
		UI_PrintStringSmallBold("CAT RX", 0, 0, 0);
	else
		UI_PrintStringSmallNormal("CAT", 0, 0, 0);

	if (!d->heartbeat_ok)
		UI_PrintStringSmallNormal("!", 120, 0, 0);

	{
		uint32_t f = d->rx_freq;
		uint32_t mhz  = f / 100000;
		uint32_t frac = f % 100000;
		sprintf(buf, "%3u.%05u", (unsigned)mhz, (unsigned)frac);
		UI_DisplayFrequency(buf, 16, 1, false);
	}

	{
		char off_str[14] = "";
		if (d->offset_dir == 1)
			sprintf(off_str, "+%u.%03u",
				(unsigned)(d->tx_offset / 100000),
				(unsigned)((d->tx_offset % 100000) / 100));
		else if (d->offset_dir == 2)
			sprintf(off_str, "-%u.%03u",
				(unsigned)(d->tx_offset / 100000),
				(unsigned)((d->tx_offset % 100000) / 100));

		char tone_str[12] = "";
		if (d->tx_tone_type == 1)
			sprintf(tone_str, "T:%u", (unsigned)d->tx_tone_code);
		else if (d->tx_tone_type == 2)
			sprintf(tone_str, "D:%03u", (unsigned)d->tx_tone_code);

		sprintf(buf, "%s %s", off_str, tone_str);
		UI_PrintStringSmallNormal(buf, 0, 0, 4);
	}

	sprintf(buf, "P%u V:%s%u BW:%c",
		(unsigned)d->tx_power,
		d->vox_switch ? "" : "X",
		d->vox_switch ? d->vox_level : 0,
		d->bandwidth ? 'N' : 'W');
	UI_PrintStringSmallNormal(buf, 0, 0, 5);

	sprintf(buf, "R%u M%u SQ%u",
		(unsigned)d->rssi,
		(unsigned)d->modulation,
		(unsigned)d->squelch_level);
	UI_PrintStringSmallNormal(buf, 0, 0, 6);

	ST7565_BlitFullScreen();
}
