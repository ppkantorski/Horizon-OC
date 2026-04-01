/*
 * Copyright (c) Souldbminer, Lightos_ and Horizon OC Contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
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


#include "fatal_gui.h"

FatalGui::FatalGui(const std::string message, const std::string info)
{
    this->message = message;
    this->info = info;
}

void FatalGui::openWithResultCode(std::string tag, Result rc)
{
    char rcStr[32];
    std::string info = tag;
    info.append(rcStr, snprintf(rcStr, sizeof(rcStr), "\n\n[0x%x] %04d-%04d", rc, R_MODULE(rc), R_DESCRIPTION(rc)));

    tsl::changeTo<FatalGui>(
        "Could not connect to hoc-clk sysmodule.\n\n"
        "\n"
        "Please make sure everything is\n\n"
        "correctly installed and enabled.",
        info
    );
}

tsl::elm::Element* FatalGui::baseUI()
{
    tsl::elm::CustomDrawer* drawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* renderer, u16 x, u16 y, u16 w, u16 h) {
        renderer->drawString("\uE150", false, 40, 210, 40, TEXT_COLOR);
        renderer->drawString("Fatal error", false, 100, 210, 30, TEXT_COLOR);

        std::uint32_t txtY = 255;
        if(!this->message.empty())
        {
            txtY += renderer->drawString(this->message.c_str(), false, 40, txtY, 23, TEXT_COLOR).second;
            txtY += 55;
        }

        if(!this->info.empty())
        {
            renderer->drawString(this->info.c_str(), false, 40, txtY, 18, DESC_COLOR);
        }
    });

    return drawer;
}

bool FatalGui::handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight)
{
    if((keysDown & HidNpadButton_A) == HidNpadButton_A || (keysDown & HidNpadButton_B) == HidNpadButton_B)
    {
        while(tsl::Overlay::get()->getCurrentGui() != nullptr) {
            tsl::goBack();
        }
        return true;
    }

    return false;
}
