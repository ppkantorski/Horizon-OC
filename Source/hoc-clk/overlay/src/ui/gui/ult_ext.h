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

#pragma once
#include <tesla.hpp>

class ImageElement : public tsl::elm::ListItem {
private:
    const uint8_t* imgData;
    uint32_t imgWidth, imgHeight;
    bool visible;
    
public:
    ImageElement(const uint8_t* data, uint32_t w, uint32_t h) 
        : tsl::elm::ListItem(""), imgData(data), imgWidth(w), imgHeight(h), visible(true) {}
    
    void setVisible(bool v) {
        visible = v;
    }
    
    virtual void draw(tsl::gfx::Renderer *renderer) override {
        if (!visible) return;
        
        // Draw image centered horizontally
        u16 centerX = this->getX() + (this->getWidth() - imgWidth) / 2;
        renderer->drawBitmap(
            centerX, 
            this->getY() + 10, 
            imgWidth, 
            imgHeight, 
            imgData
        );
    }
    
    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        if (!visible) {
            // Take up no space when hidden
            this->setBoundaries(parentX, parentY, 0, 0);
        } else {
            // Normal layout when visible
            tsl::elm::ListItem::layout(parentX, parentY, parentWidth, parentHeight);
        }
    }
    
    virtual void drawHighlight(tsl::gfx::Renderer *renderer) override {
        // Do nothing - no highlight
    }
    
    virtual bool onClick(u64 keys) override {
        return false; // Non-clickable
    }
    
    virtual Element* requestFocus(Element *oldFocus, tsl::FocusDirection direction) override {
        return nullptr; // Make it non-focusable
    }
};

class HideableCategoryHeader : public tsl::elm::CategoryHeader {
private:
    bool visible;
    
public:
    HideableCategoryHeader(const std::string& title) 
        : tsl::elm::CategoryHeader(title), visible(true) {}
    
    void setVisible(bool v) {
        visible = v;
    }
    
    virtual void draw(tsl::gfx::Renderer *renderer) override {
        if (!visible) return;
        tsl::elm::CategoryHeader::draw(renderer);
    }
    
    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        if (!visible) {
            this->setBoundaries(parentX, parentY, 0, 0);
        } else {
            tsl::elm::CategoryHeader::layout(parentX, parentY, parentWidth, parentHeight);
        }
    }
};

class HideableCustomDrawer : public tsl::elm::Element {
private:
    bool visible;
    u32 height;
    
public:
    HideableCustomDrawer(u32 h)
        : Element(), visible(true), height(h) {}
    
    void setVisible(bool v) {
        visible = v;
    }
    
    virtual void draw(tsl::gfx::Renderer *renderer) override {
        // Empty drawer - just for spacing
    }
    
    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        if (!visible) {
            this->setBoundaries(parentX, parentY, 0, 0);
        } else {
            this->setBoundaries(parentX, parentY, parentWidth, height);
        }
    }
    
    virtual Element* requestFocus(Element *oldFocus, tsl::FocusDirection direction) override {
        return nullptr;
    }
};