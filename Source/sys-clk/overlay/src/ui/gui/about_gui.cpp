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
 */


#include "about_gui.h"
#include "../format.h"
#include <tesla.hpp>
#include <string>
#include "cat.h"
#include "ult_ext.h"

tsl::elm::ListItem* SpeedoItem = NULL;
tsl::elm::ListItem* IddqItem = NULL;
tsl::elm::ListItem* sysdockStatusItem = NULL;
tsl::elm::ListItem* saltyNXStatusItem = NULL;

ImageElement* CatImage = NULL;
HideableCategoryHeader* CatHeader = NULL;
HideableCustomDrawer* CatSpacer = NULL;
int lightosClickCount = 0;

AboutGui::AboutGui()
{
    memset(strings, 0, sizeof(strings));
}

AboutGui::~AboutGui()
{
}

void AboutGui::listUI()
{
    this->listElement->addItem(
        new tsl::elm::CategoryHeader("Information")
    );

    SpeedoItem =
        new tsl::elm::ListItem("Speedo:");
    this->listElement->addItem(SpeedoItem);

    IddqItem =
        new tsl::elm::ListItem("IDDQ:");
    this->listElement->addItem(IddqItem);

    sysdockStatusItem =
        new tsl::elm::ListItem("sys-dock status:");
    this->listElement->addItem(sysdockStatusItem);

    saltyNXStatusItem =
        new tsl::elm::ListItem("SaltyNX status:");
    this->listElement->addItem(saltyNXStatusItem);

    this->listElement->addItem(
        new tsl::elm::CategoryHeader("Credits")
    );

    this->listElement->addItem(
        new tsl::elm::CategoryHeader("Developers")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Souldbminer")
    );

    // Create special clickable item for Lightos
    auto lightosItem = new tsl::elm::ListItem("Lightos_");
    lightosItem->setClickListener([this](u64 keys) -> bool {
        if (keys & HidNpadButton_A) {
            lightosClickCount++;
            if (lightosClickCount >= 10) {
                if (CatImage != NULL) CatImage->setVisible(true);
                if (CatHeader != NULL) CatHeader->setVisible(true);
                if (CatSpacer != NULL) CatSpacer->setVisible(true);
            }
            return true;
        }
        return false;
    });
    this->listElement->addItem(lightosItem);

    // ---- Contributors ----
    this->listElement->addItem(
        new tsl::elm::CategoryHeader("Contributors")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Dom")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Blaise25")
    );

    // ---- Testers ----
    this->listElement->addItem(
        new tsl::elm::CategoryHeader("Testers")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Dom")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Samybigio2011")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Delta")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Miki1305")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Happy")
    );
    
    this->listElement->addItem(
        new tsl::elm::ListItem("Flopsider")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Winnerboi77")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Blaise25")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("WE1ZARD")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Alvise")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("TDRR")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("agjeococh")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Xenshen")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("Frost")
    );

    // ---- Special Thanks ----
    this->listElement->addItem(
        new tsl::elm::CategoryHeader("Special Thanks")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("ScriesM - Atmosphere CFW")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("KazushiMe - Switch OC Suite")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("hanai3bi - Switch OC Suite & EOS")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("NaGaa95 - L4T-OC-Kernel")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("B3711 - EOS")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("RetroNX - sys-clk")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("b0rd2death - Ultrahand")
    );

    this->listElement->addItem(
        new tsl::elm::ListItem("MasaGratoR - Status Monitor")
    );

    // Create cat elements but hide them initially
    CatHeader = new HideableCategoryHeader("Cat");
    CatHeader->setVisible(false);
    this->listElement->addItem(CatHeader);
    
    CatImage = new ImageElement(CAT_DATA, CAT_WIDTH, CAT_HEIGHT);
    CatImage->setVisible(false);
    this->listElement->addItem(CatImage);

    CatSpacer = new HideableCustomDrawer(75);
    CatSpacer->setVisible(false);
    this->listElement->addItem(CatSpacer);
}

void AboutGui::update()
{
    BaseMenuGui::update();
}

void AboutGui::refresh()
{
    BaseMenuGui::refresh();
    
    if (!this->context)
        return;
    // Format strings once per refresh
    sprintf(strings[0], "%u/%u/%u", this->context->speedos[HorizonOCSpeedo_CPU], this->context->speedos[HorizonOCSpeedo_GPU], this->context->speedos[HorizonOCSpeedo_SOC]);
    sprintf(strings[1], "%u/%u/%u", this->context->iddq[HorizonOCSpeedo_CPU], this->context->iddq[HorizonOCSpeedo_GPU], this->context->iddq[HorizonOCSpeedo_SOC]);
    SpeedoItem->setValue(strings[0]);
    IddqItem->setValue(strings[1]);
    sysdockStatusItem->setValue(this->context->isSysDockInstalled ? "Installed" : "Not Installed");
    saltyNXStatusItem->setValue(this->context->isSaltyNXInstalled ? "Installed" : "Not Installed");
}