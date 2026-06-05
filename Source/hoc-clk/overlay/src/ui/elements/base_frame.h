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


#pragma once

#include <tesla.hpp>

class BaseGui;

static constexpr u16 HOC_HEADER_HEIGHT = 287;
// Bottom edge of the drawn box: 106 + TOP_Y_OFFSET(15) + 156 = 277
static constexpr u16 HOC_BOX_BOTTOM    = 277;

class BaseFrame : public tsl::elm::HeaderOverlayFrame
{
    public:
        BaseFrame(BaseGui* gui, u16 headerHeight = HOC_HEADER_HEIGHT) : tsl::elm::HeaderOverlayFrame(headerHeight) {
            this->gui = gui;
        }

        void draw(tsl::gfx::Renderer* renderer) override;

    protected:
        BaseGui* gui;
};
