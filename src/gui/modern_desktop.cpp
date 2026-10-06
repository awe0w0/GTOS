#include <gui/modern_desktop.h>
using namespace gtos::gui;
extern void printf(char *);
static void Trace(const char *t) {
    printf((char *)t);
}
static const char *windowTitles[] = {"Welcome", "Applications", "System monitor", "Appearance",
                                     "Catch"};
static bool Match(const char *text, const char *query) {
    if (!query[0])
        return true;
    for (uint32_t start = 0; text[start] && start < 80; ++start) {
        uint32_t j = 0;
        while (query[j]) {
            char a = text[start + j], b = query[j];
            if (a >= 'A' && a <= 'Z')
                a += 32;
            if (b >= 'A' && b <= 'Z')
                b += 32;
            if (!a || a != b)
                break;
            ++j;
        }
        if (!query[j])
            return true;
    }
    return false;
}
ModernDesktop::ModernDesktop(gtos::drivers::Framebuffer *f, gtos::storage::AppStore *s)
    : fb(*f), paint(*f), wm(f->Width(), f->Height()), store(s), vm(this), installer(0),
      installerSize(0), eventRead(0), eventWrite(0), overflow(false), mouseX(f->Width() - 30),
      mouseY(16), leftDown(false), needsDraw(true), lightTheme(false), launcher(false),
      confirmRemove(false), dragKind(-1), dragX(0), dragY(0), dragWidth(0), dragHeight(0),
      resizing(false), selected(0), lastFrame(0), lastStep(0), lastMonitor(0), noticeAt(0),
      lastTitleClick(0), titleClickKind(-1), launcherSelected(0), queryLength(0),
      notice("Welcome to GTOS") {
    for (uint32_t i = 0; i < 256; ++i)
        keys[i] = false;
    uint8_t *p = (uint8_t *)&state;
    for (uint32_t i = 0; i < sizeof(state); ++i)
        p[i] = 0;
    query[0] = 0;
    Clear(0);
    wm.Open(ModernWelcome);
}
ModernDesktop::Theme ModernDesktop::Colors() const {
    Theme dark = {0x18232F, 0x22313F, 0xF1F5F9, 0xA3B6C8, 0x354655, 0x6EDFC0, 0x092E29};
    Theme light = {0xF2F5F7, 0xE2E9EF, 0x172E3C, 0x4A6374, 0xC1CFD8, 0x067F70, 0xFFFFFF};
    return lightTheme ? light : dark;
}
void ModernDesktop::SetInstaller(const uint8_t *d, uint32_t n) {
    installer = d;
    installerSize = n;
}
void ModernDesktop::Notice(const char *t) {
    notice = t ? t : "Unknown status";
    noticeAt = state.ticks;
    needsDraw = true;
}
void ModernDesktop::Queue(uint8_t t, uint8_t c) {
    uint32_t next = (eventWrite + 1) % 128;
    if (next == eventRead) {
        overflow = true;
        return;
    }
    events[eventWrite].type = t;
    events[eventWrite].code = c;
    events[eventWrite].x = mouseX;
    events[eventWrite].y = mouseY;
    asm volatile("" ::: "memory");
    eventWrite = next;
}
void ModernDesktop::OnKeyDown(char c) {
    Queue(1, (uint8_t)c);
}
void ModernDesktop::OnKeyUp(char c) {
    Queue(2, (uint8_t)c);
}
void ModernDesktop::OnMouseDown(uint8_t b) {
    if (b == 1)
        Queue(3, b);
}
void ModernDesktop::OnMouseUp(uint8_t b) {
    if (b == 1)
        Queue(4, b);
}
void ModernDesktop::OnMouseMove(int8_t x, int8_t y) {
    int32_t nx = mouseX + x, ny = mouseY + y;
    if (nx < 0)
        nx = 0;
    if (ny < 0)
        ny = 0;
    if (nx >= (int32_t)fb.Width())
        nx = fb.Width() - 1;
    if (ny >= (int32_t)fb.Height())
        ny = fb.Height() - 1;
    mouseX = nx;
    mouseY = ny;
    Queue(5, 0);
}
void ModernDesktop::CancelCapture() {
    leftDown = false;
    dragKind = -1;
    resizing = false;
}
void ModernDesktop::Open(ModernWindowKind k) {
    CancelCapture();
    wm.Open(k);
    launcher = false;
    Notice(windowTitles[k]);
    if (k == ModernMonitor)
        Trace("UI HARDWARE\n");
    if (k == ModernApplications)
        Trace("UI APPS\n");
}
void ModernDesktop::Close(ModernWindowKind k) {
    if (k == ModernGame) {
        vm.Stop();
        Trace("APP CLOSE OK\n");
    }
    wm.Close(k);
    dragKind = -1;
    leftDown = false;
    Notice("Window closed");
}
void ModernDesktop::Install() {
    CancelCapture();
    wm.Open(ModernApplications);
    if (!store) {
        Notice("No application store");
        Trace("APP INSTALL FAILED\n");
        return;
    }
    if (!installer) {
        Notice("No package on boot media");
        Trace("APP INSTALL FAILED\n");
        return;
    }
    if (store->Install(installer, installerSize)) {
        selected = 0;
        gtos::apps::PackageInfo info;
        if (gtos::apps::ValidatePackage(installer, installerSize, &info) == gtos::apps::PackageOK) {
            for (uint32_t i = 0; i < store->Count(); ++i) {
                const char *a = store->Get(i)->id;
                uint32_t n = 0;
                while (a[n] && a[n] == info.id[n])
                    ++n;
                if (a[n] == info.id[n]) {
                    selected = i;
                    break;
                }
            }
        }
        Notice("Installed on disk. Select the app and press Enter to play.");
        Trace("APP INSTALL OK\n");
    } else {
        Notice(store->StatusText());
        Trace("APP INSTALL FAILED\n");
    }
}
void ModernDesktop::Remove() {
    const gtos::storage::AppInfo *a = store ? store->Get(selected) : 0;
    if (!a) {
        Notice("Select an installed application first");
        return;
    }
    if (store->Uninstall(a->id)) {
        selected = 0;
        Notice("Application removed from disk");
        Trace("APP REMOVE OK\n");
    } else
        Notice(store->StatusText());
    confirmRemove = false;
}
void ModernDesktop::Launch() {
    const gtos::storage::AppInfo *a = store ? store->Get(selected) : 0;
    if (!a) {
        Open(ModernApplications);
        Notice("Install the included Catch package to begin");
        return;
    }
    uint32_t length = 0;
    if (!store->Read(a->id, package, sizeof(package), &length)) {
        Notice(store->StatusText());
        return;
    }
    if (!vm.Load(package, length)) {
        Notice(vm.Fault());
        return;
    }
    Clear(0);
    Open(ModernGame);
    for (uint32_t i = 0; i < 256; ++i)
        keys[i] = false;
    Notice("Arrow keys move. R restarts. Esc closes.");
    Trace("APP LAUNCH OK\n");
}
uint32_t ModernDesktop::HeldKeys() const {
    if (wm.Focused() != ModernGame || launcher || confirmRemove)
        return 0;
    return (keys[0x81] || keys['a'] ? gtos::apps::VirtualMachine::Left : 0) |
           (keys[0x82] || keys['d'] ? gtos::apps::VirtualMachine::Right : 0) |
           (keys[0x83] || keys['w'] ? gtos::apps::VirtualMachine::Up : 0) |
           (keys[0x84] || keys['s'] ? gtos::apps::VirtualMachine::Down : 0) |
           (keys[' '] ? gtos::apps::VirtualMachine::Action : 0);
}
uint32_t ModernDesktop::LauncherItems(uint8_t *out) const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < 4; ++i)
        if (Match(windowTitles[i], query))
            out[count++] = i;
    if (store)
        for (uint32_t i = 0; i < store->Count(); ++i)
            if (Match(store->Get(i)->title, query))
                out[count++] = i + 4;
    return count;
}
void ModernDesktop::LauncherActivate(uint8_t item) {
    launcher = false;
    if (item < 4)
        Open((ModernWindowKind)item);
    else {
        selected = item - 4;
        Launch();
    }
}
void ModernDesktop::Key(uint8_t k, bool down) {
    if (k >= 'A' && k <= 'Z')
        k += 32;
    bool repeat = down && keys[k];
    keys[k] = down;
    if (!down)
        return;
    needsDraw = true;
    if (confirmRemove) {
        if (!repeat && (k == 'y' || k == '\n'))
            Remove();
        else if (k == 27 || k == 'n') {
            confirmRemove = false;
            Notice("Removal canceled");
        }
        return;
    }
    if (launcher) {
        uint8_t items[12];
        uint32_t count = LauncherItems(items);
        if (k == 27) {
            launcher = false;
            return;
        }
        if (k == 0x83 && launcherSelected > 0)
            --launcherSelected;
        else if (k == 0x84 && launcherSelected + 1 < (int32_t)count)
            ++launcherSelected;
        else if (k == '\n' && !repeat) {
            if (count)
                LauncherActivate(items[launcherSelected]);
        } else if (k == '\b' && queryLength) {
            query[--queryLength] = 0;
            launcherSelected = 0;
        } else if (k >= 32 && k < 127 && queryLength < 31) {
            query[queryLength++] = k;
            query[queryLength] = 0;
            launcherSelected = 0;
        }
        return;
    }
    if (repeat)
        return;
    if (k == '\t') {
        CancelCapture();
        wm.Cycle();
        return;
    }
    if (k == '[' && wm.Focused() >= 0) {
        CancelCapture();
        wm.Minimize((ModernWindowKind)wm.Focused());
        return;
    }
    if (k == ']' && wm.Focused() >= 0) {
        CancelCapture();
        wm.ToggleMaximize((ModernWindowKind)wm.Focused());
        return;
    }
    if (k == 27 && wm.Focused() >= 0) {
        Close((ModernWindowKind)wm.Focused());
        return;
    }
    if (wm.Focused() == ModernGame) {
        if (k == 'r') {
            Clear(0);
            vm.Reset();
            Trace("APP RESTART OK\n");
        }
        return;
    }
    if (k == 'l') {
        CancelCapture();
        launcher = true;
        queryLength = 0;
        query[0] = 0;
        launcherSelected = 0;
        return;
    }
    if (k == '1' || k == 'h')
        Open(ModernWelcome);
    else if (k == '2' || k == 'm')
        Open(ModernMonitor);
    else if (k == '3')
        Open(ModernApplications);
    else if (k == '4')
        Open(ModernSettings);
    else if (k == 'i')
        Install();
    else if (k == 't' && wm.Focused() == ModernSettings) {
        lightTheme = !lightTheme;
        Notice(lightTheme ? "Light appearance applied for this session"
                          : "Dark appearance applied for this session");
        Trace("UI THEME CHANGED\n");
    } else if ((k == 'u') && wm.Focused() == ModernApplications) {
        if (store && store->Get(selected)) {
            CancelCapture();
            confirmRemove = true;
        } else
            Notice("No installed application to remove");
    } else if (k == '\n' || k == 'g') {
        Launch();
    } else if (k == 0x84 && wm.Focused() == ModernApplications && store &&
               selected + 1 < store->Count())
        ++selected;
    else if (k == 0x83 && wm.Focused() == ModernApplications && selected > 0)
        --selected;
}
void ModernDesktop::Pointer(const Input &e) {
    const int32_t x = e.x, y = e.y;
    needsDraw = true;
    if (e.type == 4) {
        leftDown = false;
        dragKind = -1;
        resizing = false;
        return;
    }
    if (e.type == 5) {
        if (launcher || confirmRemove) {
            CancelCapture();
            return;
        }
        if (leftDown && dragKind >= 0) {
            titleClickKind = -1;
            if (resizing)
                wm.Resize((ModernWindowKind)dragKind, dragWidth + x - dragX,
                          dragHeight + y - dragY);
            else
                wm.Move((ModernWindowKind)dragKind, x - dragX, y - dragY);
        }
        return;
    }
    if (e.type != 3)
        return;
    CancelCapture();
    leftDown = true;
    if (confirmRemove) {
        int32_t mx = ((int32_t)fb.Width() - 430) / 2, my = ((int32_t)fb.Height() - 190) / 2;
        ModernRect cancel = {mx + 150, my + 132, 114, 34}, remove = {mx + 278, my + 132, 126, 34};
        if (cancel.Contains(x, y)) {
            confirmRemove = false;
            Notice("Removal canceled");
        } else if (remove.Contains(x, y))
            Remove();
        return;
    }
    if (y >= (int32_t)fb.Height() - ModernWindowManager::Bottom) {
        if (x < 60) {
            CancelCapture();
            launcher = !launcher;
            queryLength = 0;
            query[0] = 0;
            launcherSelected = 0;
            return;
        }
        int32_t index = (x - 68) / 118;
        if (x >= 68 && index >= 0 && index < ModernWindowCount && (x - 68) % 118 < 110 &&
            y >= (int32_t)fb.Height() - 44 && y < (int32_t)fb.Height() - 8) {
            ModernWindowKind k = (ModernWindowKind)index;
            const ModernWindow &w = wm.Window(k);
            if (k == ModernGame && !w.open) {
                Launch();
                return;
            }
            if (w.open && !w.minimized && wm.Focused() == k)
                wm.Minimize(k);
            else
                Open(k);
        }
        return;
    }
    if (launcher) {
        int32_t ly = fb.Height() - ModernWindowManager::Bottom - 368;
        ModernRect menu = {14, ly, 330, 354};
        if (!menu.Contains(x, y)) {
            launcher = false;
            return;
        }
        if (y >= ly + 62 && y < ly + 314) {
            uint8_t items[12];
            uint32_t n = LauncherItems(items);
            int32_t first = launcherSelected >= 6 ? launcherSelected - 5 : 0;
            int32_t row = (y - ly - 62) / 42 + first;
            if (row >= 0 && row < (int32_t)n) {
                launcherSelected = row;
                LauncherActivate(items[row]);
            }
        }
        return;
    }
    int32_t hit = wm.Hit(x, y);
    if (hit < 0)
        return;
    ModernWindowKind k = (ModernWindowKind)hit;
    wm.Focus(k);
    ModernRect r = wm.Window(k).bounds;
    if (y < r.y + ModernWindowManager::Title) {
        if (x >= r.x + r.w - 40) {
            Close(k);
            return;
        }
        if (x >= r.x + r.w - 76) {
            wm.ToggleMaximize(k);
            Trace("UI WINDOW MAXIMIZE\n");
            return;
        }
        if (x >= r.x + r.w - 112) {
            wm.Minimize(k);
            Trace("UI WINDOW MINIMIZE\n");
            return;
        }
        if (titleClickKind == k && state.ticks - lastTitleClick < 30) {
            wm.ToggleMaximize(k);
            titleClickKind = -1;
            return;
        }
        titleClickKind = k;
        lastTitleClick = state.ticks;
        dragKind = k;
        dragX = x - r.x;
        dragY = y - r.y;
        resizing = false;
        return;
    }
    if (!wm.Window(k).maximized && x >= r.x + r.w - 20 && y >= r.y + r.h - 20) {
        dragKind = k;
        dragX = x;
        dragY = y;
        dragWidth = r.w;
        dragHeight = r.h;
        resizing = true;
        return;
    }
    if (k == ModernWelcome) {
        ModernRect apps = {r.x + 28, r.y + r.h - 78, 170, 36},
                   monitor = {r.x + 211, r.y + r.h - 78, 166, 36};
        if (apps.Contains(x, y))
            Open(ModernApplications);
        else if (monitor.Contains(x, y))
            Open(ModernMonitor);
    } else if (k == ModernApplications) {
        int32_t by = r.y + r.h - 68;
        if (y >= by && y < by + 34) {
            if (x >= r.x + 22 && x < r.x + 148)
                Launch();
            else if (x >= r.x + 158 && x < r.x + 304)
                Install();
            else if (x >= r.x + r.w - 140 && x < r.x + r.w - 20) {
                if (store && store->Get(selected)) {
                    CancelCapture();
                    confirmRemove = true;
                }
            }
        } else if (y >= r.y + 105 && y < r.y + r.h - 88 && x >= r.x + 20 && x < r.x + r.w - 20) {
            uint32_t visible = (r.h - 193) / 42;
            uint32_t first = selected >= visible ? selected - visible + 1 : 0;
            uint32_t i = first + (y - r.y - 105) / 42;
            if (store && i < store->Count())
                selected = i;
        }
    } else if (k == ModernSettings) {
        ModernRect dark = {r.x + 24, r.y + 112, 196, 84}, light = {r.x + 234, r.y + 112, 196, 84};
        if (dark.Contains(x, y) || light.Contains(x, y)) {
            lightTheme = light.Contains(x, y);
            Notice("Appearance applied for this session");
            Trace("UI THEME CHANGED\n");
        }
    } else if (k == ModernGame) {
        ModernRect restart = {r.x + r.w - 114, r.y + 48, 90, 34};
        if (restart.Contains(x, y)) {
            Clear(0);
            vm.Reset();
            Trace("APP RESTART OK\n");
        }
    }
}
void ModernDesktop::Update(const SystemSnapshot &s) {
    state = s;
    while (eventRead != eventWrite) {
#ifndef GTOS_DESKTOP_HOST_TEST
        uint32_t flags;
        asm volatile("pushf; pop %0; cli" : "=r"(flags)::"memory");
#endif
        Input e = events[eventRead];
        eventRead = (eventRead + 1) % 128;
#ifndef GTOS_DESKTOP_HOST_TEST
        asm volatile("push %0; popf" ::"r"(flags) : "memory", "cc");
#endif
        if (e.type <= 2)
            Key(e.code, e.type == 1);
        else
            Pointer(e);
    }
    if (overflow) {
        overflow = false;
        leftDown = false;
        dragKind = -1;
        for (uint32_t i = 0; i < 256; ++i)
            keys[i] = false;
        Notice("Input queue recovered; press the key again");
    }
    const ModernWindow &gameWindow = wm.Window(ModernGame);
    if (gameWindow.open && !gameWindow.minimized && state.ticks - lastStep >= 5) {
        lastStep = state.ticks;
        vm.Step(HeldKeys(), state.ticks);
        needsDraw = true;
    }
    if (state.ticks - lastMonitor >= 50) {
        lastMonitor = state.ticks;
        needsDraw = true;
    }
    if (needsDraw && (state.ticks - lastFrame >= 2 || lastFrame == 0)) {
        Draw();
        lastFrame = state.ticks;
        needsDraw = false;
    }
}
void ModernDesktop::Clear(uint8_t c) {
    for (uint32_t i = 0; i < sizeof(game); ++i)
        game[i] = c & 15;
}
void ModernDesktop::Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c) {
    if (w <= 0 || h <= 0)
        return;
    int64_t rr = (int64_t)x + w, bb = (int64_t)y + h;
    int32_t r = rr > 272 ? 272 : (rr < 0 ? 0 : (int32_t)rr),
            b = bb > 128 ? 128 : (bb < 0 ? 0 : (int32_t)bb);
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    for (int32_t j = y; j < b; ++j)
        for (int32_t i = x; i < r; ++i)
            game[j * 272 + i] = c & 15;
}
void ModernDesktop::Text(int32_t x, int32_t y, const char *t, uint8_t c) {
    if (!t)
        return;
    int64_t px = x;
    for (uint32_t n = 0; n < 96 && t[n]; ++n, px += 6) {
        if (px >= 272)
            break;
        if (px < -5 || y < -6 || y >= 128)
            continue;
        const uint8_t *g = ModernGameGlyph(t[n]);
        if (g)
            for (int32_t r = 0; r < 7; ++r)
                for (int32_t b = 0; b < 5; ++b)
                    if (g[r] & (1 << (4 - b)))
                        Rect((int32_t)px + b, y + r, 1, 1, c);
    }
}
void ModernDesktop::Number(int32_t x, int32_t y, int32_t v, uint8_t c) {
    char out[13], rev[10];
    uint32_t n = 0, k = 0, u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    do {
        rev[n++] = '0' + u % 10;
        u /= 10;
    } while (u);
    if (v < 0)
        out[k++] = '-';
    while (n)
        out[k++] = rev[--n];
    out[k] = 0;
    Text(x, y, out, c);
}
