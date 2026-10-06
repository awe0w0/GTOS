#ifndef GTOS_GUI_MODERN_GEOMETRY_H
#define GTOS_GUI_MODERN_GEOMETRY_H
#include <common/types.h>
namespace gtos {
namespace gui {
struct ModernRect {
    int32_t x, y, w, h;
    bool Contains(int32_t px, int32_t py) const {
        return w > 0 && h > 0 && px >= x && py >= y && (int64_t)px < (int64_t)x + w &&
               (int64_t)py < (int64_t)y + h;
    }
};
enum ModernWindowKind {
    ModernWelcome,
    ModernApplications,
    ModernMonitor,
    ModernSettings,
    ModernGame,
    ModernWindowCount
};
struct ModernWindow {
    ModernRect bounds, restore;
    bool open, minimized, maximized;
};
class ModernWindowManager {
    int32_t width, height, focused;
    uint8_t order[ModernWindowCount];
    ModernWindow windows[ModernWindowCount];
    void Clamp(ModernRect &r, ModernWindowKind kind) const;
    void Refocus();

  public:
    enum { Top = 30, Bottom = 52, Title = 38, MinWidth = 430, MinHeight = 300 };
    ModernWindowManager(int32_t width, int32_t height);
    void Open(ModernWindowKind kind);
    void Close(ModernWindowKind kind);
    void Minimize(ModernWindowKind kind);
    void Focus(ModernWindowKind kind);
    void Cycle();
    void ToggleMaximize(ModernWindowKind kind);
    void Move(ModernWindowKind kind, int32_t x, int32_t y);
    void Resize(ModernWindowKind kind, int32_t w, int32_t h);
    int32_t Hit(int32_t x, int32_t y) const;
    int32_t Focused() const { return focused; }
    const ModernWindow &Window(ModernWindowKind k) const { return windows[k]; }
    ModernWindowKind At(uint32_t z) const { return (ModernWindowKind)order[z]; }
    ModernRect WorkArea() const {
        ModernRect r = {0, Top, width, height - Top - Bottom};
        return r;
    }
};
} // namespace gui
} // namespace gtos
#endif
