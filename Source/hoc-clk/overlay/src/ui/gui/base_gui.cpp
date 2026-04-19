/*
 * Copyright (c) Souldbminer, Lightos_ and Horizon OC Contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

/* --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return.  - The sys-clk authors
 * --------------------------------------------------------------------------
 */

#include "base_gui.h"

#include "../elements/base_frame.h"

#include <tesla.hpp>
#include <math.h>

#define LOGO_X 20
#define LOGO_Y 50
#define LOGO_LABEL_FONT_SIZE 45

#define VERSION_X (LOGO_X + 250)
#define VERSION_Y (LOGO_Y - 40)
#define VERSION_FONT_SIZE 15

std::string getVersionString() {
    char buf[0x100] = "";
    Result rc = hocclkIpcGetVersionString(buf, sizeof(buf));
    if (R_FAILED(rc) || buf[0] == '\0') {
        return "Unknown";
    }
    return std::string(buf);
}

static constexpr tsl::Color dynamicLogoRGB1 = tsl::Color(0, 15, 3, 15);
static constexpr tsl::Color dynamicLogoRGB2 = tsl::Color(0, 8, 1, 15);
static constexpr tsl::Color STATIC_GREEN     = tsl::Color(0, 15, 0, 15);
const std::string name = "Horizon OC Gaea";

static s32 drawDynamicUltraText(
    tsl::gfx::Renderer* renderer,
    s32 startX,
    s32 y,
    u32 fontSize,
    const tsl::Color& staticColor,
    bool useNotificationMethod = false)
{
    static constexpr double cycleDuration = 1.6;

    s32 currentX = startX;

    const u64 currentTime_ns = armTicksToNs(armGetSystemTick());
    const double timeNow = static_cast<double>(currentTime_ns) / 1e9;
    const double timeBase = fmod(timeNow, cycleDuration);

    const double waveScale = 2.0 * M_PI / cycleDuration;

    for (size_t i = 0; i < name.size(); i++)
    {
        char letter = name[i];
        if (letter == '\0') break;

        double phase = waveScale * (timeBase + i * 0.12);

        double raw = cos(phase);
        double n = (raw + 1.0) * 0.5;
        double s1 = n * n * (3.0 - 2.0 * n);
        double blend = std::clamp(s1, 0.0, 1.0);

        double glow = (cos(phase * 1.5) + 1.0) * 0.5;
        double brightness = 0.75 + glow * 0.25;

        u8 r = static_cast<u8>(
            (dynamicLogoRGB1.r + (dynamicLogoRGB2.r - dynamicLogoRGB1.r) * blend) * brightness
        );
        u8 g = static_cast<u8>(
            (dynamicLogoRGB1.g + (dynamicLogoRGB2.g - dynamicLogoRGB1.g) * blend) * brightness
        );
        u8 b = static_cast<u8>(
            (dynamicLogoRGB1.b + (dynamicLogoRGB2.b - dynamicLogoRGB1.b) * blend) * brightness
        );

        r = std::clamp<u8>(r, 0, 15);
        g = std::clamp<u8>(g, 0, 15);
        b = std::clamp<u8>(b, 0, 15);

        // bool lightning = (fmod(timeNow, 5.0) < 0.15);
        // if (lightning) {
        //     r = std::min<u8>(r + 4, 15);
        //     g = std::min<u8>(g + 4, 15);
        //     b = std::min<u8>(b + 15, 15);
        // }

        tsl::Color color(r, g, b, 15);

        std::string ls(1, letter);

        if (useNotificationMethod)
            currentX += renderer->drawNotificationString(ls, false, currentX, y, fontSize, color).first;
        else
            currentX += renderer->drawString(ls, false, currentX, y, fontSize, color).first;
    }

    return currentX;
}

void BaseGui::preDraw(tsl::gfx::Renderer* renderer) {
    drawDynamicUltraText(
        renderer,
        LOGO_X,
        LOGO_Y,
        LOGO_LABEL_FONT_SIZE,
        STATIC_GREEN,
        false
    );
}

tsl::elm::Element* BaseGui::createUI()
{
    BaseFrame* rootFrame = new BaseFrame(this);
    rootFrame->setContent(this->baseUI());
    return rootFrame;
}

void BaseGui::update()
{
    this->refresh();
}