#ifndef __GTOS__GUI__WIDGET_H
#define __GTOS__GUI__WIDGET_H

#include <common/types.h>
#include <common/graphicscontext.h>
#include <drivers/keyboard.h>

namespace gtos {
    namespace gui {
        class Widget : public gtos::drivers::KeyboardEventHandler {
        protected:
            Widget* parent;
            int32_t x;
            int32_t y;
            int32_t w;
            int32_t h;

            uint8_t r;
            uint8_t g;
            uint8_t b;

            bool Focussable;
        public:
            Widget( Widget* parent,
                    int32_t x,
                    int32_t y,
                    int32_t w,
                    int32_t h,

                    uint8_t r,
                    uint8_t g,
                    uint8_t b);
            ~Widget();


            virtual void GetFocus(Widget* widget);
            virtual void ModelToScreen(int32_t &x, int32_t &y);
            virtual bool ContainCoordinate(int32_t x, int32_t y); 
            
            virtual void Draw(gtos::common::GraphicsContext* gc);
            virtual void OnMouseDown(int32_t x,int32_t y, uint8_t button);
            virtual void OnMouseUp(int32_t x,int32_t y, uint8_t button);
            virtual void OnMouseMove(int32_t oldx, int32_t oldy, int32_t newx, int32_t newy);

              
        };

        class CompositeWidget : public Widget {
        private:
            Widget* children[100];
            int numChildren;
            Widget* focussedChild;

        public:
            CompositeWidget(Widget* parent,
                            int32_t x,
                            int32_t y,
                            int32_t w,
                            int32_t h,

                            uint8_t r,
                            uint8_t g,
                            uint8_t b);
            ~CompositeWidget();

            virtual void GetFocus(Widget* widget);

            virtual void Draw(gtos::common::GraphicsContext* gc);
            virtual void OnMouseDown(int32_t x,int32_t y, uint8_t button);
            virtual void OnMouseUp(int32_t x,int32_t y, uint8_t button);
            virtual void OnMouseMove(int32_t oldx, int32_t oldy, int32_t newx, int32_t newy);
            virtual bool AddChild(Widget* child);

            virtual void OnKeyDown(char);
            virtual void OnKeyUp(char); 
        };
    }
}

#endif