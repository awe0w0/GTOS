#include <common/boot_log.h>
#include <gui/modern_desktop.h>
using namespace gtos::gui;
static const char *titles[] = {"Welcome", "Applications", "System monitor", "Appearance",
                               "Catch", "Native image"};
static const char *dockTitles[] = {"Home", "Apps", "Monitor", "Settings", "Catch"};
static const uint32_t gamePalette[] = {0x101923, 0x182633, 0x243744, 0x354D5A, 0x517080, 0x809BA9,
                                       0xBCD1D5, 0xEAF1ED, 0xEEDF83, 0x81CB9B, 0x86C5D5, 0xB7B1DA,
                                       0xEDAA79, 0xDD8290, 0xCF9CBB, 0xFFFFFF};
void ModernDesktop::Button(int32_t x, int32_t y, int32_t w, const char *t, bool primary,
                           bool danger) {
    Theme c = Colors();
    paint.Rounded(x, y, w, 34, 6, danger ? 0x803D4D : (primary ? c.accent : c.raised));
    paint.Text(x + (w - paint.TextWidth(t)) / 2, y + 8, t,
               danger ? 0xFFFFFF : (primary ? c.accentText : c.text));
}
void ModernDesktop::Draw() {
    Theme c = Colors();
    imageScale = 0;
    fb.ResetClip();
    int32_t width = fb.Width(), height = fb.Height();
    // Procedural wallpaper: no opaque image asset and no pretend live widgets.
    for (int32_t y = 0; y < height; ++y) {
        uint32_t t = (uint32_t)y * 60 / height;
        uint32_t color = lightTheme ? ((160 + t / 2) << 16) | ((188 + t / 3) << 8) | (204 + t / 3)
                                    : ((12 + t / 7) << 16) | ((27 + t / 3) << 8) | (42 + t / 2);
        fb.Rect(0, y, width, 1, color);
        int32_t start = width * 3 / 5 - y / 2, end = start + width / 3;
        if (start < 0)
            start = 0;
        if (end > width)
            end = width;
        uint32_t band = lightTheme ? 0xAFCED0 : 0x173F49;
        fb.Rect(start, y, end - start, 1, band);
    }
    paint.Text(width - 213, height - 154, "GTOS", lightTheme ? 0x52757F : 0x43636E, 3);
    paint.Text(width - 210, height - 95, Label("Built one layer at a time"),
               lightTheme ? 0x52757F : 0x6E8F99);
    fb.Rect(0, 0, width, 30, lightTheme ? 0xDBE8EC : 0x11202A);
    paint.Icon(14, 7, 0, c.accent);
    paint.Text(43, 6, "GTOS", c.text);
    paint.Text(99, 6, Label("Workspace"), c.muted);
    paint.Text(width - 230, 6, Label("Up"), c.muted);
    uint32_t seconds = state.ticks / 100;
    paint.Number(width - 187, 6, seconds / 60, c.text);
    paint.Text(width - 136, 6, Label("min"), c.muted);
    paint.Text(width - 83, 6, "BSP", c.muted);
    paint.Number(width - 46, 6, state.onlineCPUs, c.accent);
    for (uint32_t z = 0; z < ModernWindowCount; ++z) {
        ModernWindowKind k = wm.At(z);
        if (wm.Window(k).open && !wm.Window(k).minimized)
            DrawWindow(k);
    }
    fb.ResetClip();
    int32_t dy = height - ModernWindowManager::Bottom;
    fb.Rect(0, dy, width, 52, c.panel);
    fb.Rect(0, dy, width, 1, c.line);
    paint.Rounded(10, dy + 8, 42, 36, 8, launcher ? c.accent : c.raised);
    paint.Icon(23, dy + 18, 0, launcher ? c.accentText : c.text);
    for (uint32_t i = 0; i < ModernWindowManager::DockCount; ++i) {
        int32_t x = 68 + i * 118;
        bool focused = wm.Focused() == (int32_t)i && !launcher;
        if (focused)
            paint.Rounded(x, dy + 8, 110, 36, 7, c.raised);
        paint.Icon(x + 9, dy + 17, i, focused ? c.accent : c.muted);
        paint.Text(x + 35, dy + 17, Label(dockTitles[i]), focused ? c.text : c.muted);
        if (wm.Window((ModernWindowKind)i).open)
            fb.Rect(x + 46, dy + 46, 18, 2,
                    wm.Window((ModernWindowKind)i).minimized ? c.muted : c.accent);
    }
    if (width >= 800) {
        fb.Rect(width - 125, dy + 15, 1, 22, c.line);
        paint.Text(width - 112, dy + 18, Label("Experimental"), c.muted);
    }
    if (notice && state.ticks - noticeAt < 450) {
        int32_t nw = paint.TextWidth(Label(notice)) + 26;
        if (nw > width - 32)
            nw = width - 32;
        paint.Rounded((width - nw) / 2, dy - 35, nw, 27, 7, c.raised);
        fb.SetClip((width - nw) / 2 + 8, dy - 34, nw - 16, 25);
        paint.Text((width - nw) / 2 + 13, dy - 30, Label(notice), c.text);
        fb.ResetClip();
    }
    if (launcher) {
        DrawLauncher();
        if (composer.Active())
            DrawComposition();
    }
    if (confirmRemove)
        DrawModal();
    int32_t mx = mouseX, my = mouseY;
    for (int32_t y = 0; y < 17; ++y)
        for (int32_t x = 0; x <= y / 2; ++x)
            fb.Pixel(mx + x, my + y, (x == 0 || x == y / 2 || y == 16) ? 0x081119 : 0xFAFCFE);
    fb.Rect(mx + 5, my + 13, 3, 7, 0x081119);
    fb.Rect(mx + 6, my + 13, 1, 6, 0xFAFCFE);
    fb.Present();
    ReportNativeImagePresented();
}
void ModernDesktop::DrawWindow(ModernWindowKind k) {
    Theme c = Colors();
    const ModernWindow &window = wm.Window(k);
    ModernRect r = window.bounds;
    bool focus = wm.Focused() == k && !launcher;
    fb.ResetClip();
    paint.Rounded(r.x + 4, r.y + 7, r.w, r.h, 9, lightTheme ? 0x6B8895 : 0x08121B);
    paint.Rounded(r.x, r.y, r.w, r.h, 9, c.panel);
    paint.Rounded(r.x, r.y, r.w, 42, 8, focus ? c.raised : c.panel);
    fb.Rect(r.x, r.y + 30, r.w, 8, focus ? c.raised : c.panel);
    fb.Rect(r.x, r.y + 38, r.w, 1, c.line);
    paint.Icon(r.x + 14, r.y + 11, k, focus ? c.accent : c.muted);
    paint.Text(r.x + 42, r.y + 11, k == ModernGame ? GameTitle() : Label(titles[k]),
               focus ? c.text : c.muted);
    int32_t bx = r.x + r.w - 98;
    fb.Rect(bx, r.y + 21, 11, 2, c.muted);
    paint.Outline(bx + 36, r.y + 14, 10, 10, c.muted);
    for (int32_t i = 0; i < 10; ++i) {
        fb.Pixel(bx + 71 + i, r.y + 14 + i, c.muted);
        fb.Pixel(bx + 80 - i, r.y + 14 + i, c.muted);
    }
    fb.SetClip(r.x + 1, r.y + 39, r.w - 2, r.h - 40);
    if (k == ModernWelcome)
        DrawWelcome(r);
    else if (k == ModernApplications)
        DrawApplications(r);
    else if (k == ModernMonitor)
        DrawMonitor(r);
    else if (k == ModernSettings)
        DrawSettings(r);
    else if (k == ModernGame)
        DrawGame(r);
    else if (k == ModernImage)
        DrawNativeImage(r);
    fb.ResetClip();
    if (focus) {
        fb.Rect(r.x + 9, r.y, r.w - 18, 1, c.accent);
        if (!window.maximized)
            for (int32_t i = 0; i < 3; ++i)
                fb.Rect(r.x + r.w - 6 - i * 4, r.y + r.h - 5 - i * 4, 2, 2, c.muted);
    }
}
void ModernDesktop::DrawWelcome(const ModernRect &r) {
    Theme c = Colors();
    int32_t x = r.x + 28, y = r.y + 60;
    paint.Text(x, y, Label("YOUR DESKTOP, TAKING SHAPE"), c.accent);
    paint.Text(x, y + 34, Label("A quieter place"), c.text, 2);
    paint.Text(x, y + 72, Label("to build."), c.text, 2);
    paint.Text(x, y + 128,
               Label(liveSession ? "LIVE SESSION: apps and settings stay in RAM."
                                 : "A real kernel. A working app store. Room to grow."), c.muted);
    paint.Text(x, y + 151, Label("Explore your hardware, install Catch, and make it yours."),
               c.muted);
    if (r.h >= 390) {
        int32_t sy = r.y + r.h - 127;
        paint.Rounded(x, sy, r.w - 56, 32, 6, c.raised);
        fb.Rect(x + 12, sy + 12, 6, 6, state.memoryOK ? c.accent : 0xE89696);
        paint.Text(x + 27, sy + 7,
                   state.memoryOK ? Label("Memory checked") : Label("Memory needs attention"),
                   c.text);
        paint.Text(x + r.w - 222, sy + 7, "32-bit x86  /  GTOS", c.muted);
    }
    Button(x, r.y + r.h - 78, 170, Label("Open applications"), true);
    Button(x + 183, r.y + r.h - 78, 166, Label("View hardware"));
    paint.Text(x, r.y + r.h - 30,
               Label("1 Home    2 Monitor    3 Apps    4 Settings    L Launcher"), c.muted);
}
void ModernDesktop::DrawApplications(const ModernRect &r) {
    Theme c = Colors();
    int32_t x = r.x + 22;
    paint.Text(x, r.y + 58, Label("Your applications"), c.text);
    Button(r.x + r.w - 142, r.y + 44, 120, Label(liveSession ? "Reload RAM" : "Reload disk"));
    paint.Text(x, r.y + 80,
               liveSession ? Label("Live RAM apps reset at reboot. Internal disks are untouched.")
                   : (store && store->Mounted()
                          ? Label("Installed packages are saved on your dedicated app disk.")
                          : Label("App disk unavailable. Nothing will be formatted automatically.")),
               c.muted);
    uint32_t count = store ? store->Count() : 0, visible = (r.h - 193) / 42;
    if (!visible)
        visible = 1;
    uint32_t first = selected >= visible ? selected - visible + 1 : 0;
    if (!count) {
        paint.Rounded(x, r.y + 114, r.w - 44, r.h - 222, 8, c.raised);
        paint.Icon(x + 19, r.y + 135, 4, c.accent, 2);
        paint.Text(x + 72, r.y + 129, Label("Start with a little play"), c.text);
        paint.Text(x + 72, r.y + 154, Label("Install the Catch package from your boot media."),
                   c.muted);
    } else
        for (uint32_t row = 0; row < visible; ++row) {
            uint32_t i = first + row;
            const gtos::storage::AppInfo *a = store->Get(i);
            if (!a)
                break;
            int32_t y = r.y + 105 + row * 42;
            paint.Rounded(x, y, r.w - 44, 38, 5, i == selected ? c.raised : c.panel);
            if (i == selected)
                fb.Rect(x, y + 7, 3, 24, c.accent);
            paint.Icon(x + 13, y + 11, 4, c.accent);
            fb.SetClip(x + 46, y + 2, r.w - 216, 35);
            paint.Text(x + 46, y + 4, AppTitle(a), c.text);
            paint.Text(x + 46, y + 21, a->id, c.muted);
            fb.SetClip(r.x + 1, r.y + 39, r.w - 2, r.h - 40);
            paint.Number(r.x + r.w - 136, y + 12, a->length, c.muted);
            paint.Text(r.x + r.w - 84, y + 12, Label("bytes"), c.muted);
        }
    if (count > visible) {
        paint.Text(r.x + r.w - 124, r.y + r.h - 90, Label("More: Up / Down"), c.muted);
    }
    int32_t by = r.y + r.h - 68;
    Button(x, by, 126, Label("Open  /  Enter"), true);
    Button(r.x + 158, by, 146, Label("Install package"));
    Button(r.x + r.w - 140, by, 120, Label("Remove"), false, true);
    paint.Text(x, r.y + r.h - 24, Label("I Install  U Remove  R Reload  Up / Down Select"), c.muted);
}
uint32_t ModernDesktop::BootLogRows() const {
    int32_t available = wm.Window(ModernMonitor).bounds.h - 174;
    uint32_t rows = available > 20 ? (uint32_t)available / 20 : 1;
    return rows > 30 ? 30 : rows;
}
void ModernDesktop::DrawBootLog(const ModernRect &r) {
    Theme c = Colors();
    int32_t x = r.x + 24, y = r.y + 56;
    uint32_t total = gtos::common::BootLog::Lines(), rows = BootLogRows();
    uint32_t last = total > rows ? total - rows : 0;
    if (bootLogFirst > last)
        bootLogFirst = last;
    // Fixed controls use the catalog; kernel log lines retain their original text.
    paint.Text(x, y, Label("BOOT LOG / IN RAM"), c.accent);
    paint.Text(r.x + r.w - 144, y, Label("Lines"), c.muted);
    paint.Number(r.x + r.w - 88, y, total, c.text);
    paint.Text(x, y + 24, Label("RAM logs; no log disk writes. Reboot clears."), c.muted);
    paint.Rounded(x, r.y + 106, r.w - 48, r.h - 174, 5, c.raised);
    char line[96];
    for (uint32_t row = 0; row < rows && bootLogFirst + row < total; ++row) {
        if (!gtos::common::BootLog::ReadLine(bootLogFirst + row, line, sizeof(line)))
            break;
        uint32_t length = 0;
        while (length + 1 < sizeof(line) && line[length])
            ++length;
        bool shortened = false;
        while (length && paint.TextWidth(line) > r.w - 112) {
            line[--length] = 0;
            shortened = true;
        }
        if (shortened && length >= 3) {
            line[length - 3] = '.';
            line[length - 2] = '.';
            line[length - 1] = '.';
        }
        int32_t ly = r.y + 108 + (int32_t)row * 20;
        paint.Number(x + 8, ly, bootLogFirst + row + 1, c.muted);
        fb.SetClip(x + 54, ly, r.w - 106, 20);
        paint.Text(x + 54, ly, line, c.text);
        fb.SetClip(r.x + 1, r.y + 39, r.w - 2, r.h - 40);
    }
    int32_t statusY = r.y + r.h - 58;
    paint.Text(x, statusY, Label("Bytes"), c.muted);
    paint.Number(x + 48, statusY, gtos::common::BootLog::Bytes(), c.text);
    paint.Text(x + 130, statusY, Label("Dropped"), c.muted);
    paint.Number(x + 206, statusY, gtos::common::BootLog::Dropped(), c.text);
    paint.Text(r.x + r.w - 144, statusY, Label("Line"), c.muted);
    paint.Number(r.x + r.w - 88, statusY, total ? bootLogFirst + 1 : 0, c.text);
    paint.Text(x, r.y + r.h - 29, Label("Up/Down scroll  B/Esc monitor"), c.muted);
}
void ModernDesktop::DrawMonitor(const ModernRect &r) {
    if (bootLogView) {
        DrawBootLog(r);
        return;
    }
    Theme c = Colors();
    int32_t x = r.x + 24, y = r.y + 56;
    paint.Text(x, y, Label("Hardware & kernel"), c.text);
    paint.Text(r.x + r.w - 144, y, Label("B Boot log"), c.accent);
    int32_t half = (r.w - 60) / 2;
    paint.Rounded(x, y + 32, half, 84, 8, c.raised);
    paint.Rounded(x + half + 12, y + 32, half, 84, 8, c.raised);
    paint.Text(x + 14, y + 42, Label("PHYSICAL MEMORY"), c.muted);
    paint.Number(x + 14, y + 65, state.ramMiB, c.text, 2);
    paint.Text(x + 109, y + 81, "MiB", c.muted);
    paint.Text(x + half + 26, y + 42, Label("PROCESSORS"), c.muted);
    paint.Number(x + half + 26, y + 65, state.logicalCPUs, c.text, 2);
    paint.Text(x + half + 68, y + 81, Label("detected"), c.muted);
    paint.Text(x + half + 80 + paint.TextWidth(Label("detected")), y + 81, state.vendor, c.muted);
    int32_t row = y + 132;
    paint.Text(x, row, Label("Free physical pages"), c.muted);
    paint.Number(x + 192, row, state.freePages, c.text);
    paint.Text(x, row + 23, Label("Heap used / KiB"), c.muted);
    paint.Number(x + 192, row + 23, state.heapUsedKiB, c.text);
    paint.Text(x + 244, row + 23, "/", c.muted);
    paint.Number(x + 260, row + 23, state.heapKiB, c.text);
    uint32_t used =
        state.heapKiB ? (state.heapUsedKiB > state.heapKiB ? state.heapKiB : state.heapUsedKiB) : 0;
    fb.Rect(x, row + 45, r.w - 48, 5, c.raised);
    if (state.heapKiB) {
        uint64_t remaining = (uint64_t)(r.w - 48) * used;
        uint32_t length = 0;
        while (remaining >= state.heapKiB) {
            remaining -= state.heapKiB;
            ++length;
        }
        fb.Rect(x, row + 45, length, 5, c.accent);
    }
    paint.Text(x, row + 60, Label("Kernel workers"), c.muted);
    paint.Number(x + 192, row + 60, state.workerCPUs, c.text);
    paint.Text(x + 226, row + 60, Label("Busy / failed"), c.muted);
    paint.Number(x + 404, row + 60, state.busyWorkers, c.accent);
    paint.Text(x + 432, row + 60, "/", c.muted);
    paint.Number(x + 447, row + 60, state.workerFailures, state.workerFailures ? 0xF29B9B : c.text);
    paint.Text(x, row + 80, Label("Scheduling CPUs"), c.muted);
    paint.Number(x + 192, row + 80, state.onlineCPUs, c.text);
    paint.Text(x + 226, row + 80, Label("Parked APs"), c.muted);
    paint.Number(x + 404, row + 80, state.parkedAPs, c.accent);
    paint.Text(x, row + 100, Label("Tasks / switches"), c.muted);
    paint.Number(x + 192, row + 100, state.taskCount, c.text);
    paint.Text(x + 223, row + 100, "/", c.muted);
    paint.Number(x + 240, row + 100, state.contextSwitches, c.text);
    paint.Text(x, row + 120, Label(liveSession ? "Live RAM / KiB" : "App disk / MiB"), c.muted);
    paint.Number(x + 192, row + 120, liveSession ? 261 / 2 : state.diskSectors / 2048, c.text);
    paint.Text(x + 240, row + 120,
               store && store->Mounted() ? Label("Mounted") : Label("Unavailable"),
               store && store->Mounted() ? c.accent : 0xEAA2A2);
    paint.Text(x, row + 140, Label("Completed jobs"), c.muted);
    paint.Number(x + 192, row + 140, state.completedJobs, c.text);
    paint.Text(x + 338, row + 140, state.workPoolOK ? Label("Workers OK") : Label("Check workers"),
               state.workPoolOK ? c.accent : c.muted);
    fb.Rect(x, r.y + r.h - 50, r.w - 48, 1, c.line);
    paint.Text(x, r.y + r.h - 35, state.memoryOK ? Label("Heap OK") : Label("Heap FAIL"),
               state.memoryOK ? c.accent : 0xF29B9B);
    paint.Text(x + 105, r.y + r.h - 35,
               state.schedulerOK ? Label("Scheduler OK") : Label("Scheduler FAIL"),
               state.schedulerOK ? c.accent : 0xF29B9B);
    paint.Text(x + 262, r.y + r.h - 35,
               state.pagingEnabled
                   ? (state.writeProtectEnabled ? Label("Paging + WP") : Label("Paging on"))
                   : Label("Paging off"),
               c.muted);
}
void ModernDesktop::DrawSettings(const ModernRect &r) {
    Theme c = Colors();
    int32_t x = r.x + 24;
    paint.Text(x, r.y + 58, Label("Make this space yours"), c.text);
    paint.Text(x, r.y + 90, Label("Display language"), c.muted);
    Button(x, r.y + 114, 196, gtos::i18n::Text(locale, gtos::i18n::EnglishName),
           locale == gtos::i18n::English);
    Button(x + 210, r.y + 114, 196, gtos::i18n::Text(locale, gtos::i18n::SimplifiedChineseName),
           locale == gtos::i18n::SimplifiedChinese);
    paint.Text(x, r.y + 164, Label("Appearance"), c.muted);
    uint32_t backgrounds[] = {0x172B39, 0xB4D2DA}, surfaces[] = {0x243541, 0xF5F7F9};
    for (uint32_t i = 0; i < 2; ++i) {
        int32_t px = x + i * 210;
        paint.Rounded(px, r.y + 188, 196, 70, 8, backgrounds[i]);
        paint.Rounded(px + 16, r.y + 200, 122, 45, 5, surfaces[i]);
        fb.Rect(px + 27, r.y + 213, 66, 4, i ? 0x7D98A7 : 0x6EDFC0);
        fb.Rect(px + 27, r.y + 224, 85, 3, i ? 0xBBC9D0 : 0x516877);
        if (lightTheme == (i == 1))
            paint.Outline(px - 2, r.y + 186, 200, 74, c.accent);
        paint.Text(px, r.y + 266, Label(i ? "Light" : "Dark"), c.text);
    }
    paint.Text(x, r.y + 302, Label("C Language    T Theme"), c.muted);
    paint.Text(x, r.y + 326, Label("Tab Switch windows    [ Minimize    ] Maximize"), c.muted);
    const char *status = liveSession ? "Live settings stay in RAM until reboot" :
        preferencesPersisted
            ? (settingsConfirmed ? "Settings saved" : "Settings loaded")
            : (preferences && preferences->Writable() ? "Using default settings"
                                                      : "Changes apply to this session only");
    paint.Text(x, r.y + r.h - 30, Label(status), c.muted);
}
void ModernDesktop::DrawGame(const ModernRect &r) {
    Theme c = Colors();
    paint.Text(r.x + 24, r.y + 54, Label("Arrows / A D move    R restart    Esc close"), c.muted);
    Button(r.x + r.w - 114, r.y + 48, 90, Label("Restart"));
    int32_t scale = (r.w - 48) / 272, sy = (r.h - 124) / 128;
    if (sy < scale)
        scale = sy;
    if (scale < 1)
        scale = 1;
    if (scale > 4)
        scale = 4;
    int32_t gx = r.x + (r.w - 272 * scale) / 2, gy = r.y + 89 + (r.h - 124 - 128 * scale) / 2;
    paint.Outline(gx - 2, gy - 2, 272 * scale + 4, 128 * scale + 4, c.line);
    for (int32_t y = 0; y < 128; ++y) {
        for (int32_t x = 0; x < 272;) {
            uint8_t color = game[y * 272 + x];
            int32_t end = x + 1;
            while (end < 272 && game[y * 272 + end] == color)
                ++end;
            fb.Rect(gx + x * scale, gy + y * scale, (end - x) * scale, scale,
                    gamePalette[color & 15]);
            x = end;
        }
    }
    paint.Text(r.x + 24, r.y + r.h - 27,
               Label("External bytecode app  /  scaled framebuffer  /  one VM host"), c.muted);
    if (!vm.Running()) {
        paint.Rounded(gx + 14, gy + 24, 272 * scale - 28, 90, 8, c.panel);
        paint.Text(gx + 30, gy + 38, Label("Application stopped"), 0xECA0A0);
        paint.Text(gx + 30, gy + 62, Label(vm.Fault()), c.text);
        paint.Text(gx + 30, gy + 88, Label("Press R to restart or Esc to close"), c.muted);
    }
}
void ModernDesktop::DrawNativeImage(const ModernRect &r) {
    if (!image.generation || !image.width || image.width > 32 ||
        !image.height || image.height > 32)
        return;
    Theme c = Colors();
    paint.Text(r.x + 24, r.y + 54, Label("Image from native application"), c.muted);
    const int32_t availableWidth = r.w - 48, availableHeight = r.h - 124;
    int32_t scale = availableWidth / (int32_t)image.width;
    const int32_t verticalScale = availableHeight / (int32_t)image.height;
    if (verticalScale < scale)
        scale = verticalScale;
    if (scale > 6)
        scale = 6;
    if (scale <= 0)
        return;
    const int32_t width = image.width * scale, height = image.height * scale;
    imageX = r.x + (r.w - width) / 2;
    imageY = r.y + 82 + (availableHeight - height) / 2;
    imageScale = scale;
    paint.Outline(imageX - 2, imageY - 2, width + 4, height + 4, c.line);
    // Source-space checker tiles expose alpha. Source RGB is premultiplied8;
    // Framebuffer::Blend uses alpha4, so retain all eight alpha bits here.
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint32_t background = ((x / 4 + y / 4) & 1) ? 0xA7B5C2 : 0xDDE5EC;
            const uint8_t *pixel = image.rgba + (y * image.width + x) * 4;
            const uint32_t inverseAlpha = 255 - pixel[3];
            uint32_t color = 0;
            for (uint32_t channel = 0; channel < 3; ++channel) {
                const uint32_t shift = 16 - channel * 8;
                const uint32_t result = pixel[channel] +
                    (((background >> shift) & 255) * inverseAlpha + 127) / 255;
                color |= result << shift;
            }
            fb.Rect(imageX + x * scale, imageY + y * scale, scale, scale, color);
        }
    paint.Text(r.x + 24, r.y + r.h - 27, Label("5 reopens this image / Esc closes"), c.muted);
}
void ModernDesktop::DrawLauncher() {
    Theme c = Colors();
    int32_t x = 14, y = fb.Height() - ModernWindowManager::Bottom - 368;
    paint.Rounded(x + 4, y + 6, 330, 354, 9, 0x08131A);
    paint.Rounded(x, y, 330, 354, 9, c.panel);
    paint.Rounded(x + 12, y + 13, 306, 37, 6, c.raised);
    char entry[192], beforeCursor[192];
    for (uint32_t i = 0; i < queryCursor; ++i)
        entry[i] = query[i];
    entry[queryCursor] = 0;
    if (composer.Active())
        gtos::i18n::Append(entry, sizeof(entry), composer.Preedit());
    uint32_t caretBytes = gtos::i18n::ByteLength(entry, sizeof(entry));
    gtos::i18n::Append(entry, sizeof(entry), query + queryCursor);
    uint32_t firstText = 0;
    for (;;) {
        uint32_t n = 0;
        for (uint32_t i = firstText; i < caretBytes; ++i)
            beforeCursor[n++] = entry[i];
        beforeCursor[n] = 0;
        if (paint.TextWidth(beforeCursor) <= 224 || firstText >= caretBytes)
            break;
        firstText += gtos::i18n::Decode(entry + firstText, caretBytes - firstText).bytes;
    }
    fb.SetClip(x + 22, y + 18, 238, 27);
    paint.Text(x + 24, y + 23, entry[0] ? entry + firstText : Label("Type to find an app..."),
               entry[0] ? c.text : c.muted);
    fb.Rect(x + 24 + paint.TextWidth(beforeCursor), y + 21, 1, 20, c.accent);
    fb.ResetClip();
    paint.Rounded(x + 268, y + 18, 42, 27, 4, c.panel);
    paint.Text(x + 275, y + 23, pinyinInput ? Label("Pinyin") : "EN",
               pinyinInput ? c.accent : c.muted);
    uint8_t items[12];
    uint32_t count = LauncherItems(items);
    int32_t first = launcherSelected >= 6 ? launcherSelected - 5 : 0;
    if (!count)
        paint.Text(x + 24, y + 84, Label("No matching applications"), c.muted);
    for (uint32_t row = 0; row < 6; ++row) {
        uint32_t index = first + row;
        if (index >= count)
            break;
        uint8_t item = items[index];
        int32_t ry = y + 62 + row * 42;
        if (index == (uint32_t)launcherSelected)
            paint.Rounded(x + 10, ry, 310, 38, 5, c.raised);
        paint.Icon(x + 22, ry + 11, item < 4 ? item : 4, c.accent);
        const char *title = item < 4 ? Label(titles[item]) : AppTitle(store->Get(item - 4));
        fb.SetClip(x + 57, ry + 4, 251, 31);
        paint.Text(x + 57, ry + 9, title, c.text);
        fb.ResetClip();
    }
    fb.Rect(x + 14, y + 322, 302, 1, c.line);
    paint.Text(x + 20, y + 330, Label("Up / Down to choose    Enter to open"), c.muted);
}
void ModernDesktop::DrawComposition() {
    Theme c = Colors();
    ModernRect r = CandidatePanel();
    paint.Rounded(r.x + 4, r.y + 6, r.w, r.h, 8, 0x08131A);
    paint.Rounded(r.x, r.y, r.w, r.h, 8, c.panel);
    fb.SetClip(r.x + 12, r.y + 8, r.w - 196, 25);
    paint.Text(r.x + 14, r.y + 10, composer.Preedit(), c.accent);
    fb.SetClip(r.x + r.w - 174, r.y + 8, 162, 25);
    paint.Text(r.x + r.w - 160, r.y + 10, Label("Pinyin candidates"), c.muted);
    fb.ResetClip();
    uint32_t count = composer.CandidateCount();
    if (!count)
        paint.Text(r.x + 14, r.y + 44, Label("No matching candidate"), c.muted);
    for (uint32_t i = 0; i < count; ++i) {
        ModernRect b = CandidateBox(i);
        paint.Rounded(b.x, b.y, b.w, b.h, 4, c.raised);
        paint.Number(b.x + 5, b.y + 3, i + 1, c.accent);
        fb.SetClip(b.x + 23, b.y + 1, b.w - 27, b.h - 2);
        paint.Text(b.x + 23, b.y + 3, composer.Candidate(i), c.text);
        fb.ResetClip();
    }
    fb.SetClip(r.x + 12, r.y + 115, r.w - 24, 20);
    paint.Text(r.x + 14, r.y + 116, Label("Space / 1-9 select; Enter commits / opens; Esc cancels"),
               c.muted);
    fb.ResetClip();
}
void ModernDesktop::DrawModal() {
    Theme c = Colors();
    int32_t x = ((int32_t)fb.Width() - 430) / 2, y = ((int32_t)fb.Height() - 190) / 2;
    paint.Rounded(x + 5, y + 7, 430, 190, 9, 0x080F15);
    paint.Rounded(x, y, 430, 190, 9, c.panel);
    paint.Text(x + 24, y + 23, Label("Remove this application?"), c.text);
    const gtos::storage::AppInfo *a = store ? store->Get(selected) : 0;
    paint.Text(x + 24, y + 58, AppTitle(a), c.accent);
    paint.Text(x + 24, y + 84,
               Label(liveSession ? "This removes the package from live RAM for this session."
                                 : "This removes the installed package from your app disk."),
               c.muted);
    Button(x + 150, y + 132, 114, Label("Cancel / Esc"));
    Button(x + 278, y + 132, 126, Label("Remove / Enter"), false, true);
}
