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
 * 128x64 LCD, 7 rows of 8px each + 1 status row.
 *
 * Layout:
 *   Row 0: mode indicator + status
 *   Row 1-2: RX frequency (large)
 *   Row 3: TX offset + tone info
 *   Row 4: power / VOX / bandwidth
 *   Row 5: S-meter / RSSI
 *   Row 6: heartbeat status
 */

static const char *PowerStr(uint8_t level)
{
    switch (level) {
        case 0: return "LOW1";
        case 1: return "LOW2";
        case 2: return "LOW3";
        case 3: return "LOW4";
        case 4: return "LOW5";
        case 5: return "MID";
        case 6: return "HIGH";
        default: return "?";
    }
}

static const char *ModStr(uint8_t mod)
{
    switch (mod) {
        case 0: return "FM";
        case 1: return "AM";
        case 2: return "USB";
        default: return "?";
    }
}

void UI_DisplayCatmode(void)
{
    char buf[22];

    UI_DisplayClear();

    const CatDisplayState_t *d = &gCatDisplay;

    /* Row 0: mode + TX/RX indicator */
    if (d->tx_active)
        UI_PrintStringSmallBold("CAT TX", 0, 0, 0);
    else if (d->rx_active)
        UI_PrintStringSmallBold("CAT RX", 0, 0, 0);
    else
        UI_PrintStringSmallNormal("CAT", 0, 0, 0);

    if (!d->heartbeat_ok)
        UI_PrintStringSmallNormal("NO LINK", 70, 0, 0);

    /* Row 1-2: RX frequency (large font) */
    {
        uint32_t f = d->rx_freq;
        uint32_t mhz  = f / 100000;
        uint32_t frac = f % 100000;
        sprintf(buf, "%3u.%05u", (unsigned)mhz, (unsigned)frac);
        UI_DisplayFrequency(buf, 16, 1, false);
    }

    /* Row 3: offset + tone */
    {
        char off_str[12] = "";
        if (d->offset_dir == 1)
            sprintf(off_str, "+%u.%03u",
                    (unsigned)(d->tx_offset / 100000),
                    (unsigned)((d->tx_offset % 100000) / 100));
        else if (d->offset_dir == 2)
            sprintf(off_str, "-%u.%03u",
                    (unsigned)(d->tx_offset / 100000),
                    (unsigned)((d->tx_offset % 100000) / 100));

        char tone_str[10] = "";
        if (d->tx_tone_type == 1)
            sprintf(tone_str, "T:%u", (unsigned)d->tx_tone_code);
        else if (d->tx_tone_type == 2)
            sprintf(tone_str, "D:%03u", (unsigned)d->tx_tone_code);

        sprintf(buf, "T:%s %s", off_str, tone_str);
        UI_PrintStringSmallNormal(buf, 0, 0, 4);
    }

    /* Row 4: power / VOX / BW */
    sprintf(buf, "%s V:%s%u BW:%s",
            PowerStr(d->tx_power),
            d->vox_switch ? "" : "X",
            d->vox_switch ? d->vox_level : 0,
            d->bandwidth ? "N" : "W");
    UI_PrintStringSmallNormal(buf, 0, 0, 5);

    /* Row 5: RSSI + modulation */
    sprintf(buf, "RSSI:%u  %s  SQ:%u",
            (unsigned)d->rssi,
            ModStr(d->modulation),
            (unsigned)d->squelch_level);
    UI_PrintStringSmallNormal(buf, 0, 0, 6);

    ST7565_BlitFullScreen();
}
