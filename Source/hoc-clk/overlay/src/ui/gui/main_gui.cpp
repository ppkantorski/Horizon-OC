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


#include "main_gui.h"

#include "fatal_gui.h"
#include "app_profile_gui.h"
#include "global_override_gui.h"
#include "misc_gui.h"
#include "about_gui.h"

void MainGui::listUI()
{
    // this->enabledToggle = new tsl::elm::ToggleListItem("Enable", false);
    // enabledToggle->setStateChangedListener([this](bool state) {
    //     Result rc = sysclkIpcSetEnabled(state);
    //     if(R_FAILED(rc))
    //     {
    //         FatalGui::openWithResultCode("sysclkIpcSetEnabled", rc);
    //     }

    //     this->lastContextUpdate = armGetSystemTick();
    //     this->context->enabled = state;
    // });
    // this->listElement->addItem(this->enabledToggle);

    tsl::elm::ListItem* appProfileItem = new tsl::elm::ListItem("Edit App Profile");
    appProfileItem->setClickListener([this](u64 keys) {
        if((keys & HidNpadButton_A) == HidNpadButton_A && this->context)
        {
            AppProfileGui::changeTo(this->context->applicationId);
            return true;
        }

        return false;
    });
    this->listElement->addItem(appProfileItem);


    tsl::elm::ListItem* globalProfileItem = new tsl::elm::ListItem("Edit Global Profile");
    globalProfileItem->setClickListener([this](u64 keys) {
        if((keys & HidNpadButton_A) == HidNpadButton_A && this->context)
        {
            AppProfileGui::changeTo(SYSCLK_GLOBAL_PROFILE_TID);
            return true;
        }

        return false;
    });
    this->listElement->addItem(globalProfileItem);

    tsl::elm::ListItem* globalOverrideItem = new tsl::elm::ListItem("Temporary Overrides");
    globalOverrideItem->setClickListener([this](u64 keys) {
        if((keys & HidNpadButton_A) == HidNpadButton_A && this->context)
        {
            tsl::changeTo<GlobalOverrideGui>();
            return true;
        }

        return false;
    });
    this->listElement->addItem(globalOverrideItem);

    //this->listElement->addItem(new tsl::elm::CategoryHeader("Misc"));

    tsl::elm::ListItem* miscItem = new tsl::elm::ListItem("Settings");
    miscItem->setClickListener([this](u64 keys) {
        if((keys & HidNpadButton_A) == HidNpadButton_A && this->context)
        {
            tsl::changeTo<MiscGui>();
            return true;
        }

        return false;
    });
    this->listElement->addItem(miscItem);

    tsl::elm::ListItem* aboutItem = new tsl::elm::ListItem("About");
    aboutItem->setClickListener([this](u64 keys) {
        if((keys & HidNpadButton_A) == HidNpadButton_A && this->context)
        {
            tsl::changeTo<AboutGui>();
            return true;
        }

        return false;
    });
    this->listElement->addItem(aboutItem);

}

void MainGui::refresh()
{
    BaseMenuGui::refresh();
    //if(this->context)
    //{
    //    this->enabledToggle->setState(this->context->enabled);
    //}
}
