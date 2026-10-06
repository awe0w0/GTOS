#include <gui/modern_desktop.h>
#include <i18n/font.h>
using namespace gtos::gui;
extern void printf(char *);
static void Trace(const char *t) {
    printf((char *)t);
}
static const char *windowTitles[] = {"Welcome", "Applications", "System monitor", "Appearance",
                                     "Catch"};
static bool EqualText(const char *a, const char *b) {
    if (!a || !b)
        return false;
    for (uint32_t i = 0; i < 256; ++i) {
        if (a[i] != b[i])
            return false;
        if (!a[i])
            return true;
    }
    return false;
}
static bool BundledCatch(const gtos::storage::AppInfo *app) {
    // Translation is bound to the shipped package bytes, never merely a name.
    return app && app->length == 824 && app->checksum == 0x733AF7F5u && EqualText(app->id, "catch");
}
ModernDesktop::ModernDesktop(gtos::drivers::Framebuffer *f, gtos::storage::AppStore *s,
                             gtos::storage::SettingsStore *settings)
    : fb(*f), paint(*f), wm(f->Width(), f->Height()), store(s), vm(this), preferences(settings),
      locale(gtos::i18n::English), pinyinInput(false), preferencesPersisted(false),
      settingsConfirmed(false), bundledGame(false), installer(0), installerSize(0), eventRead(0),
      eventWrite(0), overflow(false), mouseX(f->Width() - 30), mouseY(16), leftDown(false),
      needsDraw(true), lightTheme(false), launcher(false), confirmRemove(false), dragKind(-1),
      dragX(0), dragY(0), dragWidth(0), dragHeight(0), resizing(false), selected(0), lastFrame(0),
      lastStep(0), lastMonitor(0), noticeAt(0), lastTitleClick(0), titleClickKind(-1),
      launcherSelected(0), queryLength(0), queryCursor(0), notice("Welcome to GTOS") {
    for (uint32_t i = 0; i < 256; ++i)
        keys[i] = false;
    uint8_t *p = (uint8_t *)&state;
    for (uint32_t i = 0; i < sizeof(state); ++i)
        p[i] = 0;
    query[0] = 0;
    if (preferences) {
        locale = preferences->Current().locale == gtos::storage::SimplifiedChinese
                     ? gtos::i18n::SimplifiedChinese
                     : gtos::i18n::English;
        lightTheme = preferences->Current().theme == gtos::storage::Light;
        preferencesPersisted = preferences->HasPersistedSettings();
    }
    pinyinInput = locale == gtos::i18n::SimplifiedChinese;
    Clear(0);
    wm.Open(ModernWelcome);
    Trace(locale == gtos::i18n::SimplifiedChinese ? "UI LOCALE zh-CN\n" : "UI LOCALE en\n");
    Trace(lightTheme ? "UI THEME light\n" : "UI THEME dark\n");
}
ModernDesktop::Theme ModernDesktop::Colors() const {
    Theme dark = {0x18232F, 0x22313F, 0xF1F5F9, 0xA3B6C8, 0x354655, 0x6EDFC0, 0x092E29};
    Theme light = {0xF2F5F7, 0xE2E9EF, 0x172E3C, 0x4A6374, 0xC1CFD8, 0x067F70, 0xFFFFFF};
    return lightTheme ? light : dark;
}
const char *ModernDesktop::Label(const char *english) const {
    return gtos::i18n::Translate(locale, english);
}
const char *ModernDesktop::AppTitle(const gtos::storage::AppInfo *app) const {
    if (!app)
        return Label("Application");
    return locale == gtos::i18n::SimplifiedChinese && BundledCatch(app)
               ? gtos::i18n::Text(locale, gtos::i18n::CatchTitle)
               : app->title;
}
const char *ModernDesktop::GameTitle() const {
    return locale == gtos::i18n::SimplifiedChinese && bundledGame
               ? gtos::i18n::Text(locale, gtos::i18n::CatchTitle)
               : vm.Info().title;
}
void ModernDesktop::CancelComposition() {
    composer.Cancel();
}
void ModernDesktop::SavePreferences() {
    gtos::storage::Settings values = {locale == gtos::i18n::SimplifiedChinese
                                          ? gtos::storage::SimplifiedChinese
                                          : gtos::storage::English,
                                      lightTheme ? gtos::storage::Light : gtos::storage::Dark};
    bool saved = preferences && preferences->Writable() && preferences->Save(values);
    preferencesPersisted = saved && preferences->HasPersistedSettings();
    settingsConfirmed = saved;
    Notice(saved ? (preferencesPersisted ? "Settings saved" : "Using default settings")
                 : "Changes apply to this session only");
}
void ModernDesktop::ApplyLocale(gtos::i18n::Locale next) {
    if (locale == next)
        return;
    CancelCapture();
    CancelComposition();
    locale = next;
    pinyinInput = next == gtos::i18n::SimplifiedChinese;
    query[0] = 0;
    queryLength = 0;
    queryCursor = 0;
    launcherSelected = 0;
    SavePreferences();
    Trace("UI LANGUAGE CHANGED\n");
    Trace(locale == gtos::i18n::SimplifiedChinese ? "UI LOCALE zh-CN\n" : "UI LOCALE en\n");
}
void ModernDesktop::ApplyTheme(bool light) {
    if (lightTheme == light)
        return;
    lightTheme = light;
    SavePreferences();
    Trace("UI THEME CHANGED\n");
    Trace(lightTheme ? "UI THEME light\n" : "UI THEME dark\n");
}
void ModernDesktop::ToggleInput() {
    if (locale != gtos::i18n::SimplifiedChinese)
        return;
    CancelComposition();
    pinyinInput = !pinyinInput;
    if (locale != gtos::i18n::SimplifiedChinese)
        pinyinInput = false;
    Notice("Keyboard input changed");
}
bool ModernDesktop::AppendQuery(const char *text) {
    char next[sizeof(query)];
    for (uint32_t i = 0; i < queryCursor; ++i)
        next[i] = query[i];
    next[queryCursor] = 0;
    if (!gtos::i18n::Append(next, sizeof(next), text) ||
        !gtos::i18n::Append(next, sizeof(next), query + queryCursor)) {
        Notice("Search is full");
        return false;
    }
    queryCursor += gtos::i18n::ByteLength(text);
    gtos::i18n::Copy(query, sizeof(query), next);
    queryLength = gtos::i18n::ByteLength(query, sizeof(query));
    launcherSelected = 0;
    return true;
}
ModernRect ModernDesktop::CandidatePanel() const {
    int32_t y = (int32_t)fb.Height() - ModernWindowManager::Bottom - 368 - 144;
    if (y < ModernWindowManager::Top + 4)
        y = ModernWindowManager::Top + 4;
    ModernRect r = {14, y, 510, 138};
    return r;
}
ModernRect ModernDesktop::CandidateBox(uint32_t index) const {
    ModernRect r = CandidatePanel();
    ModernRect box = {r.x + 12 + (int32_t)(index % 3) * 162, r.y + 37 + (int32_t)(index / 3) * 26,
                      154, 24};
    return box;
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
    CancelComposition();
    wm.Open(k);
    launcher = false;
    Notice(windowTitles[k]);
    if (k == ModernMonitor)
        Trace("UI HARDWARE\n");
    if (k == ModernApplications)
        Trace("UI APPS\n");
}
void ModernDesktop::Close(ModernWindowKind k) {
    CancelComposition();
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
    CancelComposition();
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
    bundledGame = BundledCatch(a);
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
    static const char *aliases[] = {"Home", "Apps", "Monitor", "Settings"};
    for (uint32_t i = 0; i < 4; ++i)
        if (gtos::i18n::ContainsAsciiFold(Label(windowTitles[i]), query) ||
            gtos::i18n::ContainsAsciiFold(windowTitles[i], query) ||
            gtos::i18n::ContainsAsciiFold(Label(aliases[i]), query) ||
            gtos::i18n::ContainsAsciiFold(aliases[i], query))
            out[count++] = i;
    if (store)
        for (uint32_t i = 0; i < store->Count(); ++i)
            if (gtos::i18n::ContainsAsciiFold(AppTitle(store->Get(i)), query) ||
                gtos::i18n::ContainsAsciiFold(store->Get(i)->title, query))
                out[count++] = i + 4;
    return count;
}
void ModernDesktop::LauncherActivate(uint8_t item) {
    CancelComposition();
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
        if (k == '`') {
            if (!repeat)
                ToggleInput();
            return;
        }
        if (k == 0x81 || k == 0x82)
            CancelComposition();
        if (pinyinInput && locale == gtos::i18n::SimplifiedChinese) {
            char committed[gtos::i18n::PinyinComposer::CommitCapacity];
            uint32_t capacity = sizeof(query) - queryLength;
            if (capacity > sizeof(committed))
                capacity = sizeof(committed);
            gtos::i18n::PinyinComposer::FeedResult result = composer.Feed(k, committed, capacity);
            if (result == gtos::i18n::PinyinComposer::Committed) {
                AppendQuery(committed);
                return;
            }
            if (result == gtos::i18n::PinyinComposer::Rejected) {
                Notice("No matching candidate");
                return;
            }
            if (result != gtos::i18n::PinyinComposer::PassedThrough)
                return;
        }
        uint8_t items[12];
        uint32_t count = LauncherItems(items);
        if (k == 27) {
            launcher = false;
            CancelComposition();
            return;
        }
        if (k == 0x83 && launcherSelected > 0)
            --launcherSelected;
        else if (k == 0x84 && launcherSelected + 1 < (int32_t)count)
            ++launcherSelected;
        else if (k == '\n' && !repeat) {
            if (count)
                LauncherActivate(items[launcherSelected]);
        } else if (k == 0x81 || (k == '\b' && queryCursor)) {
            CancelComposition();
            uint32_t pos = 0, previous = 0;
            while (pos < queryCursor) {
                previous = pos;
                pos += gtos::i18n::Decode(query + pos, queryLength - pos).bytes;
            }
            if (k == '\b') {
                uint32_t removed = queryCursor - previous;
                for (uint32_t i = queryCursor; i <= queryLength; ++i)
                    query[i - removed] = query[i];
                queryLength -= removed;
                launcherSelected = 0;
            }
            queryCursor = previous;
        } else if (k == 0x82 && queryCursor < queryLength) {
            CancelComposition();
            queryCursor += gtos::i18n::Decode(query + queryCursor, queryLength - queryCursor).bytes;
        } else if (k >= 32 && k < 127) {
            char text[2] = {(char)k, 0};
            AppendQuery(text);
        }
        return;
    }
    if (repeat)
        return;
    if (k == '\t') {
        CancelComposition();
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
        CancelComposition();
        CancelCapture();
        launcher = true;
        queryLength = 0;
        queryCursor = 0;
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
        ApplyTheme(!lightTheme);
    } else if (k == 'c' && wm.Focused() == ModernSettings) {
        ApplyLocale(locale == gtos::i18n::English ? gtos::i18n::SimplifiedChinese
                                                  : gtos::i18n::English);
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
            CancelComposition();
            launcher = !launcher;
            queryLength = 0;
            queryCursor = 0;
            query[0] = 0;
            launcherSelected = 0;
            return;
        }
        int32_t index = (x - 68) / 118;
        if (x >= 68 && index >= 0 && index < ModernWindowCount && (x - 68) % 118 < 110 &&
            y >= (int32_t)fb.Height() - 44 && y < (int32_t)fb.Height() - 8) {
            CancelComposition();
            launcher = false;
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
        if (composer.Active()) {
            for (uint32_t i = 0; i < composer.CandidateCount(); ++i) {
                if (CandidateBox(i).Contains(x, y)) {
                    char committed[gtos::i18n::PinyinComposer::CommitCapacity];
                    uint32_t capacity = sizeof(query) - queryLength;
                    if (capacity > sizeof(committed))
                        capacity = sizeof(committed);
                    if (composer.Select(i, committed, capacity) ==
                        gtos::i18n::PinyinComposer::Committed)
                        AppendQuery(committed);
                    else
                        Notice("Search is full");
                    return;
                }
            }
            if (CandidatePanel().Contains(x, y))
                return;
        }
        int32_t ly = fb.Height() - ModernWindowManager::Bottom - 368;
        ModernRect menu = {14, ly, 330, 354};
        ModernRect inputToggle = {14 + 268, ly + 18, 42, 27};
        if (inputToggle.Contains(x, y)) {
            ToggleInput();
            return;
        }
        if (!menu.Contains(x, y)) {
            CancelComposition();
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
        ModernRect english = {r.x + 24, r.y + 114, 196, 34};
        ModernRect chinese = {r.x + 234, r.y + 114, 196, 34};
        ModernRect dark = {r.x + 24, r.y + 188, 196, 70};
        ModernRect light = {r.x + 234, r.y + 188, 196, 70};
        if (english.Contains(x, y))
            ApplyLocale(gtos::i18n::English);
        else if (chinese.Contains(x, y))
            ApplyLocale(gtos::i18n::SimplifiedChinese);
        else if (dark.Contains(x, y) || light.Contains(x, y))
            ApplyTheme(light.Contains(x, y));
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
        CancelComposition();
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
void ModernDesktop::Text(int32_t x, int32_t y, const char *text, uint8_t color) {
    if (!text)
        return;
    bool localized = false;
    if (bundledGame && locale == gtos::i18n::SimplifiedChinese) {
        if (EqualText(text, vm.Info().title)) {
            text = gtos::i18n::Text(locale, gtos::i18n::CatchTitle);
            y = 1;
            localized = true;
        } else if (EqualText(text, vm.Info().summary)) {
            text = gtos::i18n::Text(locale, gtos::i18n::CatchSummary);
            y = 109;
            localized = true;
        }
    }
    int64_t px = x;
    uint32_t offset = 0;
    for (uint32_t count = 0; count < 96 && offset < gtos::i18n::MaxTextBytes && text[offset];
         ++count) {
        gtos::i18n::DecodeResult cp =
            gtos::i18n::Decode(text + offset, gtos::i18n::MaxTextBytes - offset);
        if (!cp.bytes)
            break;
        offset += cp.bytes;
        if (px >= 272)
            break;
        if (cp.codepoint < 128) {
            if (px >= -5 && y >= -18 && y < 128) {
                const uint8_t *glyph = ModernGameGlyph((char)cp.codepoint);
                if (glyph)
                    for (int32_t row = 0; row < 7; ++row)
                        for (int32_t col = 0; col < 5; ++col)
                            if (glyph[row] & (1 << (4 - col)))
                                Rect((int32_t)px + col, y + row + (localized ? 6 : 0), 1, 1, color);
            }
            px += 6;
        } else {
            gtos::i18n::Glyph glyph = {};
            if (!gtos::i18n::LookupGlyph(cp.codepoint, 1, glyph))
                gtos::i18n::LookupGlyph(0xFFFD, 1, glyph);
            if (px >= -(int64_t)glyph.width && y >= -(int32_t)glyph.height && y < 128) {
                for (uint32_t row = 0; row < glyph.height; ++row)
                    for (uint32_t col = 0; col < glyph.width; ++col) {
                        uint32_t at = row * glyph.width + col;
                        uint8_t alpha =
                            at & 1 ? glyph.pixels[at / 2] & 15 : glyph.pixels[at / 2] >> 4;
                        if (alpha >= 7)
                            Rect((int32_t)px + col, y + row, 1, 1, color);
                    }
            }
            px += glyph.advance;
        }
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
