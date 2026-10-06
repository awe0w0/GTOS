#include <gui/modern_geometry.h>
using namespace gtos::gui;
ModernWindowManager::ModernWindowManager(int32_t w, int32_t h) : width(w), height(h), focused(-1) {
    if (width < 640)
        width = 640;
    if (height < 480)
        height = 480;
    const int32_t widths[] = {602, 570, 590, 510, 594}, heights[] = {400, 412, 424, 364, 396};
    for (uint32_t i = 0; i < ModernWindowCount; ++i) {
        order[i] = i;
        ModernRect r = {(width - widths[i]) / 2 + (int32_t)i * 12 - 24, Top + 38 + (int32_t)i * 15,
                        widths[i], heights[i]};
        Clamp(r, (ModernWindowKind)i);
        windows[i].bounds = windows[i].restore = r;
        windows[i].open = windows[i].minimized = windows[i].maximized = false;
    }
}
void ModernWindowManager::Clamp(ModernRect &r, ModernWindowKind kind) const {
    ModernRect a = WorkArea();
    const int32_t minimumWidths[] = {500, 500, 540, 480, 592},
                  minimumHeights[] = {360, 350, 398, 330, 380};
    int32_t mw = minimumWidths[kind], mh = minimumHeights[kind];
    if (r.w < mw)
        r.w = mw;
    if (r.h < mh)
        r.h = mh;
    if (r.w > a.w)
        r.w = a.w;
    if (r.h > a.h)
        r.h = a.h;
    if (r.x < 0)
        r.x = 0;
    if (r.y < a.y)
        r.y = a.y;
    if (r.x > a.w - r.w)
        r.x = a.w - r.w;
    if (r.y > a.y + a.h - r.h)
        r.y = a.y + a.h - r.h;
}
void ModernWindowManager::Refocus() {
    focused = -1;
    for (int32_t i = ModernWindowCount - 1; i >= 0; --i) {
        const ModernWindow &w = windows[order[i]];
        if (w.open && !w.minimized) {
            focused = order[i];
            return;
        }
    }
}
void ModernWindowManager::Focus(ModernWindowKind k) {
    if (k < 0 || k >= ModernWindowCount || !windows[k].open)
        return;
    windows[k].minimized = false;
    uint32_t at = 0;
    while (at < ModernWindowCount && order[at] != k)
        ++at;
    for (uint32_t i = at; i + 1 < ModernWindowCount; ++i)
        order[i] = order[i + 1];
    order[ModernWindowCount - 1] = k;
    focused = k;
}
void ModernWindowManager::Open(ModernWindowKind k) {
    if (k < 0 || k >= ModernWindowCount)
        return;
    windows[k].open = true;
    Focus(k);
}
void ModernWindowManager::Close(ModernWindowKind k) {
    if (k < 0 || k >= ModernWindowCount)
        return;
    windows[k].open = false;
    windows[k].minimized = false;
    Refocus();
}
void ModernWindowManager::Minimize(ModernWindowKind k) {
    if (k < 0 || k >= ModernWindowCount || !windows[k].open)
        return;
    windows[k].minimized = true;
    Refocus();
}
void ModernWindowManager::Cycle() {
    if (focused < 0) {
        Refocus();
        return;
    }
    for (uint32_t i = 0; i < ModernWindowCount; ++i)
        if (order[i] != focused && windows[order[i]].open && !windows[order[i]].minimized) {
            Focus((ModernWindowKind)order[i]);
            return;
        }
}
void ModernWindowManager::ToggleMaximize(ModernWindowKind k) {
    if (k < 0 || k >= ModernWindowCount || !windows[k].open)
        return;
    ModernWindow &w = windows[k];
    if (w.maximized) {
        w.bounds = w.restore;
        Clamp(w.bounds, k);
        w.maximized = false;
    } else {
        w.restore = w.bounds;
        w.bounds = WorkArea();
        w.maximized = true;
    }
    Focus(k);
}
void ModernWindowManager::Move(ModernWindowKind k, int32_t x, int32_t y) {
    if (k < 0 || k >= ModernWindowCount || !windows[k].open || windows[k].maximized)
        return;
    windows[k].bounds.x = x;
    windows[k].bounds.y = y;
    Clamp(windows[k].bounds, k);
}
void ModernWindowManager::Resize(ModernWindowKind k, int32_t w, int32_t h) {
    if (k < 0 || k >= ModernWindowCount || !windows[k].open || windows[k].maximized)
        return;
    ModernRect &r = windows[k].bounds;
    int32_t x = r.x, y = r.y;
    r.w = w;
    r.h = h;
    Clamp(r, k);
    r.x = x;
    r.y = y;
    if (r.w > width - x)
        r.w = width - x;
    if (r.h > height - Bottom - y)
        r.h = height - Bottom - y;
}
int32_t ModernWindowManager::Hit(int32_t x, int32_t y) const {
    for (int32_t i = ModernWindowCount - 1; i >= 0; --i) {
        int32_t k = order[i];
        if (windows[k].open && !windows[k].minimized && windows[k].bounds.Contains(x, y))
            return k;
    }
    return -1;
}
