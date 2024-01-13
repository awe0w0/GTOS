#ifndef __GTOS__GUI__DESKTOP_H
#define __GTOS__GUI__DESKTOP_H

#include <drivers/mouse.h>
#include <gui/widget.h>
#include <common/types.h>
#include <common/graphicscontext.h>

namespace gtos {
    namespace gui {
        class Desktop : public CompositeWidget, public gtos::drivers::MouseEventHandler {
        protected:
            uint32_t MouseX;
            uint32_t MouseY;
        public:
            Desktop(int32_t w, int32_t h, uint8_t r, uint8_t g, uint8_t b);
            ~Desktop();

            void Draw(common::GraphicsContext* gc);

            void OnMouseDown(uint8_t button);
            void OnMouseUp(uint8_t button);
            void OnMouseMove(int8_t x, int8_t y);
        };
    }
}

#endif