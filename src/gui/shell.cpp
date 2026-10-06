#include <gui/shell.h>
using namespace gtos::gui;
extern void printf(char *);
static void Trace(const char *text) {
    printf((char *)text);
}
DesktopShell::DesktopShell(storage::AppStore *s)
    : store(s), vm(this), installer(0), installerSize(0), readEvent(0), writeEvent(0), mouseX(305),
      mouseY(12), heldKeys(0), page(0), selected(0), lastFrame(0), needsDraw(true), gameOpen(false),
      notice("READY") {
    inputOverflow = false;
    for (uint32_t i = 0; i < 256; ++i)
        keyDown[i] = false;
    gameCanvas.Clear(0);
    uint8_t *p = (uint8_t *)&state;
    for (uint32_t i = 0; i < sizeof(state); ++i)
        p[i] = 0;
}
void DesktopShell::SetInstaller(const uint8_t *data, uint32_t length) {
    installer = data;
    installerSize = length;
}
void DesktopShell::Queue(uint8_t type, uint8_t code) {
    uint32_t next = (writeEvent + 1) % 64;
    if (next == readEvent) {
        inputOverflow = true;
        return;
    }
    events[writeEvent].type = type;
    events[writeEvent].code = code;
    events[writeEvent].x = mouseX;
    events[writeEvent].y = mouseY;
    asm volatile("" ::: "memory");
    writeEvent = next;
}
void DesktopShell::OnKeyDown(char c) {
    Queue(1, (uint8_t)c);
}
void DesktopShell::OnKeyUp(char c) {
    Queue(2, (uint8_t)c);
}
void DesktopShell::OnMouseDown(uint8_t b) {
    if (b == 1)
        Queue(3, b);
}
void DesktopShell::OnMouseMove(int8_t x, int8_t y) {
    int32_t nx = mouseX + x, ny = mouseY + y;
    if (nx < 0)
        nx = 0;
    if (nx > 319)
        nx = 319;
    if (ny < 0)
        ny = 0;
    if (ny > 199)
        ny = 199;
    mouseX = nx;
    mouseY = ny;
}
void DesktopShell::Install() {
    if (!installer) {
        notice = "NO INSTALLER ON BOOT MEDIA";
        return;
    }
    if (store->Install(installer, installerSize)) {
        notice = "INSTALLED - PRESS ENTER TO PLAY";
        apps::PackageInfo installed;
        selected = 0;
        if (apps::ValidatePackage(installer, installerSize, &installed) == apps::PackageOK)
            for (uint32_t i = 0; i < store->Count(); ++i) {
                const char *a = store->Get(i)->id;
                const char *b = installed.id;
                uint32_t n = 0;
                while (a[n] && a[n] == b[n])
                    ++n;
                if (a[n] == b[n]) {
                    selected = i;
                    break;
                }
            }
        Trace("APP INSTALL OK\n");
    } else {
        notice = store->StatusText();
        Trace("APP INSTALL FAILED\n");
    }
    needsDraw = true;
}
void DesktopShell::Remove() {
    const storage::AppInfo *info = store->Get(selected);
    if (!info) {
        notice = "NO APP SELECTED";
        return;
    }
    if (store->Uninstall(info->id)) {
        notice = "APP REMOVED";
        selected = 0;
        Trace("APP REMOVE OK\n");
    } else
        notice = store->StatusText();
    needsDraw = true;
}
void DesktopShell::Launch() {
    const storage::AppInfo *info = store->Get(selected);
    if (!info) {
        page = 2;
        notice = "INSTALL AN APP FIRST";
        return;
    }
    uint32_t length = 0;
    if (!store->Read(info->id, package, sizeof(package), &length)) {
        notice = store->StatusText();
        return;
    }
    if (!vm.Load(package, length)) {
        notice = vm.Fault();
        return;
    }
    gameCanvas.Clear(0);
    gameOpen = true;
    heldKeys = 0;
    notice = "ARROWS MOVE  R RESTART  ESC CLOSE";
    Trace("APP LAUNCH OK\n");
    needsDraw = true;
}
void DesktopShell::ProcessKey(uint8_t key, bool down) {
    if (key >= 'A' && key <= 'Z')
        key += 32;
    const bool repeat = down && keyDown[key];
    keyDown[key] = down;
    heldKeys = (keyDown[0x81] || keyDown['a'] ? apps::VirtualMachine::Left : 0) |
               (keyDown[0x82] || keyDown['d'] ? apps::VirtualMachine::Right : 0) |
               (keyDown[0x83] || keyDown['w'] ? apps::VirtualMachine::Up : 0) |
               (keyDown[0x84] || keyDown['s'] ? apps::VirtualMachine::Down : 0) |
               (keyDown[' '] ? apps::VirtualMachine::Action : 0);
    if (!down || repeat)
        return;
    needsDraw = true;
    if (gameOpen) {
        if (key == 27) {
            vm.Stop();
            gameOpen = false;
            heldKeys = 0;
            page = 2;
            notice = "APP CLOSED";
            Trace("APP CLOSE OK\n");
        } else if (key == 'r') {
            gameCanvas.Clear(0);
            vm.Reset();
            Trace("APP RESTART OK\n");
        }
        return;
    }
    if (key == '1' || key == 'h') {
        page = 0;
        notice = "READY";
    } else if (key == '2' || key == 'm') {
        page = 1;
        notice = "LIVE HARDWARE STATUS";
        Trace("UI HARDWARE\n");
    } else if (key == '3' || key == 'a') {
        page = 2;
        notice = "MANAGE YOUR APPLICATIONS";
        Trace("UI APPS\n");
    } else if (key == 'i') {
        page = 2;
        Install();
    } else if (key == 'u' && page == 2)
        Remove();
    else if (key == 'g' || key == '\n')
        Launch();
    else if (key == 0x84 && page == 2 && selected + 1 < store->Count())
        ++selected;
    else if (key == 0x83 && page == 2 && selected > 0)
        --selected;
}
void DesktopShell::Click(int32_t x, int32_t y) {
    if (gameOpen) {
        if (x >= 295 && y < 30) {
            ProcessKey(27, true);
            ProcessKey(27, false);
        }
        return;
    }
    if (x < 82 && y >= 50 && y < 125) {
        page = (y - 50) / 25;
        notice = "READY";
        needsDraw = true;
        return;
    }
    if (page == 0 && x >= 104 && x < 230 && y >= 129 && y < 149) {
        page = 2;
        notice = "MANAGE YOUR APPLICATIONS";
    } else if (page == 2) {
        if (y >= 133 && y <= 150) {
            if (x >= 97 && x < 161)
                Launch();
            else if (x >= 166 && x < 230)
                Install();
            else if (x >= 235)
                Remove();
        } else if (y >= 60 && y < 128) {
            uint32_t i = (selected >= 4 ? selected - 3 : 0) + (y - 60) / 17;
            if (i < store->Count()) {
                if (selected == i)
                    Launch();
                else
                    selected = i;
            }
        }
    }
    needsDraw = true;
}
void DesktopShell::Update(const SystemSnapshot &s) {
    state = s;
    while (readEvent != writeEvent) {
        uint32_t flags;
        asm volatile("pushf; pop %0; cli" : "=r"(flags)::"memory");
        Event e = events[readEvent];
        readEvent = (readEvent + 1) % 64;
        asm volatile("push %0; popf" ::"r"(flags) : "memory", "cc");
        if (e.type == 3)
            Click(e.x, e.y);
        else
            ProcessKey(e.code, e.type == 1);
    }
    if (inputOverflow) {
        inputOverflow = false;
        heldKeys = 0;
        for (uint32_t i = 0; i < 256; ++i)
            keyDown[i] = false;
        notice = "INPUT QUEUE RESET";
    }
    if ((uint32_t)(state.ticks - lastFrame) >= 5 || needsDraw) {
        lastFrame = state.ticks;
        Draw();
        needsDraw = false;
    }
}
void DesktopShell::Button(int32_t x, int32_t y, int32_t w, const char *t, uint8_t c) {
    canvas.Rect(x, y, w, 18, c);
    canvas.Text(x + 6, y + 6, t, c == 8 ? 0 : 7);
}
void DesktopShell::DrawHome() {
    canvas.Text(99, 44, "A SMALL OS. A BIG START.", 5);
    canvas.Text(99, 61, "MAKE ROOM", 7, 2);
    canvas.Text(99, 80, "FOR PLAY.", 8, 2);
    canvas.Text(99, 105, "YOUR HARDWARE. YOUR APPS.", 6);
    canvas.Text(99, 116, "ONE EXPERIMENTAL DESKTOP.", 6);
    Button(99, 133, 125, "OPEN APPLICATIONS", 8);
    canvas.Text(99, 162, "KERNEL  /  GTOS 0.2", 5);
}
void DesktopShell::DrawHardware() {
    canvas.Text(98, 42, "SYSTEM MONITOR", 8);
    canvas.Text(98, 58, "MEMORY", 5);
    canvas.Number(174, 58, state.ramMiB, 7);
    canvas.Text(202, 58, "MIB", 6);
    canvas.Text(98, 69, "FREE PAGES", 6);
    canvas.Number(174, 69, state.freePages, 7);
    canvas.Text(98, 80, "HEAP KIB", 6);
    canvas.Number(174, 80, state.heapUsedKiB, 7);
    canvas.Text(198, 80, "/", 5);
    canvas.Number(211, 80, state.heapKiB, 7);
    canvas.Rect(98, 94, 204, 1, 3);
    canvas.Text(98, 102, "CPU", 5);
    canvas.Text(123, 102, state.vendor, 7);
    canvas.Text(212, 102, "PARKED", 5);
    canvas.Number(259, 102, state.parkedAPs, 9);
    canvas.Text(98, 114, "DETECTED", 6);
    canvas.Number(160, 114, state.logicalCPUs, 7);
    canvas.Text(188, 114, "ONLINE", 6);
    canvas.Number(236, 114, state.onlineCPUs, 8);
    canvas.Text(98, 127, "TASKS", 6);
    canvas.Number(138, 127, state.taskCount, 7);
    canvas.Text(174, 127, "SWITCHES", 6);
    canvas.Number(234, 127, state.contextSwitches, 7);
    canvas.Rect(98, 139, 204, 1, 3);
    canvas.Text(98, 147, "DISK MIB", 6);
    canvas.Number(160, 147, state.diskSectors / 2048, 7);
    canvas.Text(208, 147, store->Mounted() ? "MOUNTED" : "OFFLINE", store->Mounted() ? 9 : 13);
    canvas.Text(98, 161, state.pagingEnabled ? "PAGE" : "HEAP", 5);
    canvas.Text(129, 161, state.memoryOK ? "PASS" : "FAIL", state.memoryOK ? 9 : 13);
    canvas.Text(184, 161, "SCHED", 5);
    canvas.Text(222, 161, state.schedulerOK ? "PASS" : "FAIL", state.schedulerOK ? 9 : 13);
}
void DesktopShell::DrawApps() {
    canvas.Text(98, 42, "APPLICATIONS", 8);
    canvas.Number(280, 42, store->Count(), 5);
    if (!store->Count()) {
        canvas.Rect(98, 62, 203, 58, 2);
        canvas.Text(110, 76, "YOUR APP LIST IS EMPTY", 7);
        canvas.Text(110, 91, "INSTALL CATCH TO BEGIN", 5);
    } else
        for (uint32_t row = 0; row < 4; ++row) {
            uint32_t i = (selected >= 4 ? selected - 3 : 0) + row;
            const storage::AppInfo *a = store->Get(i);
            if (!a)
                break;
            canvas.Rect(98, 60 + row * 17, 203, 16, i == selected ? 3 : 2);
            canvas.Rect(104, 64 + row * 17, 8, 8, i == selected ? 8 : 5);
            canvas.Text(120, 65 + row * 17, a->title, 7);
        }
    Button(98, 133, 64, "PLAY", 9);
    Button(167, 133, 64, "INSTALL", 8);
    Button(236, 133, 65, "REMOVE", 3);
    canvas.Text(98, 159, "I INSTALL   U REMOVE", 5);
    canvas.Text(98, 169, "ENTER PLAY  UP/DOWN SELECT", 5);
}
void DesktopShell::Draw() {
    canvas.Clear(0);
    canvas.Rect(0, 0, 320, 27, 1);
    canvas.Rect(10, 8, 10, 10, 8);
    canvas.Rect(14, 4, 2, 18, 0);
    canvas.Text(29, 9, "GTOS", 7);
    if (!gameOpen)
        canvas.Text(80, 9, "DESKTOP", 5);
    canvas.Rect(250, 8, 4, 4, state.memoryOK ? 9 : 13);
    canvas.Text(260, 9, "READY", 6);
    if (gameOpen) {
        canvas.Text(80, 9, vm.Info().title, 8);
        canvas.Text(300, 9, "X", 7);
        canvas.Rect(17, 38, 286, 139, 3);
        canvas.Text(24, 29, "R RESTART   ESC CLOSE", 5);
        bool running = vm.Step(heldKeys, state.ticks);
        canvas.Blit(gameCanvas, 0, 0, 272, 128, 24, 44);
        if (!running) {
            canvas.Rect(24, 70, 272, 65, 1);
            canvas.Text(36, 80, "APPLICATION STOPPED", 13);
            canvas.Text(36, 94, vm.Fault(), 7);
            canvas.Text(36, 110, "R RESTART OR ESC CLOSE", 5);
        }
    } else {
        canvas.Rect(0, 28, 83, 155, 1);
        canvas.Text(10, 36, "WORKSPACE", 5);
        const char *labels[] = {"HOME", "HARDWARE", "APPS"};
        for (uint32_t i = 0; i < 3; ++i) {
            if (page == i)
                canvas.Rect(5, 50 + i * 25, 74, 21, 3);
            canvas.Text(12, 57 + i * 25, labels[i], page == i ? 8 : 6);
        }
        canvas.Text(10, 155, "BSP", 5);
        canvas.Text(10, 168, "ONLINE", 9);
        if (page == 0)
            DrawHome();
        else if (page == 1)
            DrawHardware();
        else
            DrawApps();
    }
    canvas.Rect(0, 184, 320, 16, 1);
    canvas.Rect(0, 184, 320, 1, 3);
    canvas.Text(8, 190, notice, 6);
    int32_t x = mouseX, y = mouseY;
    for (int32_t j = 0; j < 9; ++j)
        for (int32_t i = 0; i <= j / 2; ++i)
            canvas.Pixel(x + i, y + j, (i == 0 || i == j / 2 || j == 8) ? 0 : 15);
    canvas.Present();
}
void DesktopShell::Clear(uint8_t c) {
    gameCanvas.Rect(0, 0, 272, 128, c);
}
void DesktopShell::Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > 272)
        w = 272 - x;
    if (y + h > 128)
        h = 128 - y;
    if (w > 0 && h > 0)
        gameCanvas.Rect(x, y, w, h, c);
}
void DesktopShell::Text(int32_t x, int32_t y, const char *t, uint8_t c) {
    if (x < 0 || y < 0 || x > 266 || y > 121)
        return;
    char clip[46];
    uint32_t i = 0, max = (272 - x) / 6;
    while (i < max && i < 45 && t[i]) {
        clip[i] = t[i];
        ++i;
    }
    clip[i] = 0;
    gameCanvas.Text(x, y, clip, c);
}
void DesktopShell::Number(int32_t x, int32_t y, int32_t v, uint8_t c) {
    char out[13], rev[11];
    uint32_t n = 0, k = 0, u = (v < 0) ? 0u - (uint32_t)v : (uint32_t)v;
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
