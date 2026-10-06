// Deterministic real desktop/framebuffer tests, executable without libc.
#include <gui/modern_desktop.h>
#include <i18n/font.h>
using namespace gtos;
using namespace gtos::gui;
static uint32_t checks;
static void Output(const char *text) {
    uint32_t n = 0;
    while (text[n])
        ++n;
#ifdef __x86_64__
    unsigned long result;
    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(1), "D"(2), "S"(text), "d"(n)
                 : "rcx", "r11", "memory");
#else
    unsigned long result;
    asm volatile("int $0x80" : "=a"(result) : "a"(4), "b"(2), "c"(text), "d"(n) : "memory");
#endif
}
static void Exit(uint32_t n) {
#ifdef __x86_64__
    asm volatile("syscall" ::"a"(60), "D"(n) : "rcx", "r11", "memory");
#else
    asm volatile("int $0x80" ::"a"(1), "b"(n) : "memory");
#endif
    __builtin_unreachable();
}
static void Check(bool ok, const char *label) {
    ++checks;
    if (!ok) {
        Output("FAIL ");
        Output(label);
        Output("\n");
        Exit(1);
    }
}
// Test-only platform stubs: production desktop, painter, store, and VM stay real.
void printf(char *) {}
gtos::drivers::KeyboardEventHandler::KeyboardEventHandler() {}
void gtos::drivers::KeyboardEventHandler::OnKeyDown(char) {}
void gtos::drivers::KeyboardEventHandler::OnKeyUp(char) {}
gtos::drivers::MouseEventHandler::MouseEventHandler() {}
void gtos::drivers::MouseEventHandler::OnActivate() {}
void gtos::drivers::MouseEventHandler::OnMouseDown(uint8_t) {}
void gtos::drivers::MouseEventHandler::OnMouseUp(uint8_t) {}
void gtos::drivers::MouseEventHandler::OnMouseMove(int8_t, int8_t) {}

#ifndef GTOS_DESKTOP_SANITIZE
extern "C" void *memset(void *dst, int value, unsigned long count) {
    uint8_t *p = (uint8_t *)dst;
    while (count--)
        *p++ = value;
    return dst;
}
extern "C" void *memcpy(void *dst, const void *src, unsigned long count) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (count--)
        *d++ = *s++;
    return dst;
}

#endif
static const uint32_t Guard = 0xDABAD00D;
struct SmallBuffer {
    uint32_t before[16], pixels[64 * 48], after[16];
};
static SmallBuffer back;
static uint32_t video[16 + 72 * 48 + 16];
static drivers::FramebufferMode Mode(uint32_t w = 64, uint32_t h = 48, uint32_t pitch = 288) {
    drivers::FramebufferMode m = {w, h, pitch, 16, 8, 8, 8, 0, 8};
    return m;
}
static void FramebufferTests() {
    drivers::Framebuffer f;
    drivers::FramebufferMode m = Mode();
    Check(!f.Ready() && f.Width() == 0, "unconfigured framebuffer");
    f.Clear(0);
    f.Present();
    Check(drivers::Framebuffer::Validate(m), "padded 32-bit pitch supported");
    m.pitch = 255;
    Check(!drivers::Framebuffer::Validate(m), "short misaligned pitch rejected");
    m = Mode();
    m.redPosition = 8;
    Check(!drivers::Framebuffer::Validate(m), "overlapping masks rejected");
    m = Mode();
    m.blueSize = 0;
    Check(!drivers::Framebuffer::Validate(m), "empty channel rejected");
    m = Mode();
    m.width = 0;
    Check(!drivers::Framebuffer::Validate(m), "zero dimensions rejected");
    m = Mode();
    m.width = 0xFFFFFFFFu;
    Check(!drivers::Framebuffer::Validate(m), "dimension overflow rejected");
    m = Mode();
    for (uint32_t i = 0; i < sizeof(back) / 4; ++i)
        ((uint32_t *)&back)[i] = Guard;
    for (uint32_t i = 0; i < sizeof(video) / 4; ++i)
        video[i] = Guard;
    Check(!f.Bind(m, (uint8_t *)(video + 16), back.pixels, 64 * 48 - 1),
          "undersized RAM buffer rejected");
    Check(!f.Bind(m, (uint8_t *)back.pixels, back.pixels, 64 * 48),
          "overlapping front/back rejected");
    Check(f.Bind(m, (uint8_t *)(video + 16), back.pixels, 64 * 48), "valid framebuffer bound");
    f.Rect(-5, -8, 12, 11, 0xAABBCC);
    f.Rect(63, 47, 0x7FFFFFFF, 0x7FFFFFFF, 0xF0A050);
    f.Rect((int32_t)0x80000000u, 0, 0x7FFFFFFF, 2, 0xFFFFFF);
    f.Rect(0x7FFFFFFF, 0x7FFFFFFF, 1, 1, 0xFFFFFF);
    Check(f.ReadPixel(0, 0) == 0xAABBCC && f.ReadPixel(6, 2) == 0xAABBCC && f.ReadPixel(7, 2) == 0,
          "negative rectangle clipped exactly");
    Check(f.ReadPixel(63, 47) == 0xF0A050, "overflowing rectangle endpoint clipped");
    f.SetClip(10, 10, 3, 4);
    f.Clear(0xFFFFFF);
    Check(f.ReadPixel(10, 10) == 0xFFFFFF && f.ReadPixel(12, 13) == 0xFFFFFF &&
              f.ReadPixel(13, 13) == 0,
          "clear honors clip");
    f.Pixel(9, 11, 0x123456);
    Check(f.ReadPixel(9, 11) == 0, "pixel clipped");
    f.SetClip(0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF);
    f.Clear(0x998877);
    Check(f.ReadPixel(10, 10) == 0xFFFFFF, "invalid intersection draws nothing");
    f.ResetClip();
    f.Blend(20, 20, 0xFFFFFF, 7);
    Check(f.ReadPixel(20, 20) == 0x777777, "4-bit alpha blend");
    ModernPainter p(f);
    p.Text(-20, -8, "Text clip !?", 0xFFFFFF);
    p.Text(0x7FFFFFFF, 0x7FFFFFFF, "Invisible", 0xFFFFFF, 3);
    p.Rounded(0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 8, 0xFFFFFF);
    p.Rounded(-3, -8, 0x7FFFFFFF, 0x7FFFFFFF, 8, 0xDEADBE);
    p.Outline(2, 2, 0x7FFFFFFF, 0x7FFFFFFF, 0xFEEDAB);
    p.Icon(0x7FFFFFFF, 0x7FFFFFFF, 2, 0xFFFFFF, 3);
    f.Present();
    for (uint32_t i = 0; i < 16; ++i) {
        Check(back.before[i] == Guard && back.after[i] == Guard, "backbuffer canaries intact");
        Check(video[i] == Guard && video[16 + 72 * 48 + i] == Guard, "frontbuffer canaries intact");
    }
    for (uint32_t y = 0; y < 48; ++y)
        for (uint32_t x = 64; x < 72; ++x)
            Check(video[16 + y * 72 + x] == Guard, "pitch padding untouched");
    video[16 + 40 * 72 + 40] = 0xCAFEBABE;
    f.Pixel(1, 1, 0x010203);
    f.Present();
    Check(video[16 + 72 + 1] == 0x010203 && video[16 + 40 * 72 + 40] == 0xCAFEBABE,
          "single-pixel Present preserves unrelated MMIO target contents");
    f.Present();
    Check(video[16 + 40 * 72 + 40] == 0xCAFEBABE,
          "Present without damage performs no unrelated copy");
    // RGB565 in a 32-bit word is permitted; bpp itself remains exactly 32.
    m.redPosition = 11;
    m.redSize = 5;
    m.greenPosition = 5;
    m.greenSize = 6;
    m.bluePosition = 0;
    m.blueSize = 5;
    Check(f.Bind(m, (uint8_t *)(video + 16), back.pixels, 64 * 48), "non-native masks accepted");
    f.Pixel(0, 0, 0xFF8040);
    f.Present();
    Check(video[16] == 0xFC08, "hardware mask conversion");
    m = Mode();
    m.redPosition = 0;
    m.bluePosition = 16;
    Check(f.Bind(m, (uint8_t *)(video + 16), back.pixels, 64 * 48), "BGR accepted");
    f.Pixel(0, 0, 0x123456);
    f.Present();
    Check(video[16] == 0x563412, "BGR conversion exact");
    memory::MultibootInfo mbi = {};
    mbi.flags = 1u << 12;
    mbi.framebufferAddress = 0xE0000000;
    mbi.framebufferWidth = 800;
    mbi.framebufferHeight = 600;
    mbi.framebufferPitch = 3200;
    mbi.framebufferBitsPerPixel = 32;
    mbi.framebufferType = 1;
    mbi.framebufferColorInfo[0] = 16;
    mbi.framebufferColorInfo[1] = 8;
    mbi.framebufferColorInfo[2] = 8;
    mbi.framebufferColorInfo[3] = 8;
    mbi.framebufferColorInfo[5] = 8;
    Check(drivers::Framebuffer::ReadMode(&mbi, m), "Multiboot RGB32 validated");
    // Fixture encoded byte-by-byte from GRUB's actual Multiboot v1 ABI. Its
    // palette/RGB union has 4-byte alignment; RGB begins at 112, not 110.
    uint8_t raw[120] = {};
    apps::Write32(raw, 1u << 12);
    apps::Write32(raw + 88, 0xFD000000);
    apps::Write32(raw + 96, 3200);
    apps::Write32(raw + 100, 800);
    apps::Write32(raw + 104, 600);
    raw[108] = 32;
    raw[109] = 1;
    raw[112] = 16;
    raw[113] = 8;
    raw[114] = 8;
    raw[115] = 8;
    raw[116] = 0;
    raw[117] = 8;
    Check(drivers::Framebuffer::ReadMode((const memory::MultibootInfo *)raw, m) &&
              m.redPosition == 16 && m.redSize == 8 && m.greenPosition == 8 && m.greenSize == 8 &&
              m.bluePosition == 0 && m.blueSize == 8,
          "raw GRUB framebuffer ABI has RGB union at 112");

    mbi.framebufferBitsPerPixel = 24;
    Check(!drivers::Framebuffer::ReadMode(&mbi, m), "24 bpp gracefully rejected");
    mbi.framebufferBitsPerPixel = 16;
    Check(!drivers::Framebuffer::ReadMode(&mbi, m), "16 bpp gracefully rejected");
    mbi.framebufferBitsPerPixel = 32;
    mbi.framebufferAddress = 0xFFFFF000;
    Check(!drivers::Framebuffer::ReadMode(&mbi, m), "video range crossing 4GiB rejected");
    mbi.framebufferAddress = 0x100000000ULL;
    Check(!drivers::Framebuffer::ReadMode(&mbi, m), "64-bit-only video address rejected");
}
static void GeometryTests() {
    ModernRect huge = {(int32_t)0x80000000u, 0, 0x7FFFFFFF, 1};
    Check(!huge.Contains(0, 0), "rect endpoint uses wide arithmetic");
    ModernWindowManager wm(800, 600);
    Check(wm.Focused() == -1 && wm.Hit(10, 10) == -1, "desktop starts without focus");
    wm.Open(ModernWelcome);
    wm.Open(ModernApplications);
    wm.Open(ModernMonitor);
    Check(wm.Focused() == ModernMonitor, "opened window takes focus");
    wm.Minimize(ModernMonitor);
    Check(wm.Focused() == ModernApplications && wm.Window(ModernMonitor).minimized,
          "minimize restores underlying focus");
    wm.Focus(ModernMonitor);
    Check(!wm.Window(ModernMonitor).minimized, "focus restores minimized window");
    wm.Close(ModernMonitor);
    Check(wm.Focused() == ModernApplications, "close refocuses");
    wm.Cycle();
    Check(wm.Focused() == ModernWelcome, "keyboard cycles windows");
    wm.Move(ModernWelcome, 0x7FFFFFFF, (int32_t)0x80000000u);
    ModernRect r = wm.Window(ModernWelcome).bounds;
    Check(r.x + r.w <= 800 && r.y == ModernWindowManager::Top, "move clamped to workarea");
    wm.Resize(ModernWelcome, 0x7FFFFFFF, 0x7FFFFFFF);
    r = wm.Window(ModernWelcome).bounds;
    Check(r.x + r.w <= 800 && r.y + r.h <= 548, "huge resize bounded");
    ModernRect before = r;
    wm.ToggleMaximize(ModernWelcome);
    r = wm.Window(ModernWelcome).bounds;
    Check(r.x == 0 && r.y == 30 && r.w == 800 && r.h == 518, "maximize excludes topbar and dock");
    wm.Move(ModernWelcome, 30, 40);
    Check(wm.Window(ModernWelcome).bounds.x == 0, "maximized window does not drag");
    wm.ToggleMaximize(ModernWelcome);
    r = wm.Window(ModernWelcome).bounds;
    Check(r.x == before.x && r.y == before.y && r.w == before.w && r.h == before.h,
          "restore geometry exact");
    uint32_t random = 0x12345678;
    for (uint32_t n = 0; n < 4000; ++n) {
        random = random * 1664525u + 1013904223u;
        ModernWindowKind k = (ModernWindowKind)(random % 5);
        wm.Open(k);
        wm.Move(k, (int32_t)random, (int32_t)(random ^ 0xBCDEABCD));
        wm.Resize(k, (int32_t)(random >> 1), (int32_t)(random ^ 0x12345));
        r = wm.Window(k).bounds;
        Check(r.x >= 0 && r.y >= 30 && r.w > 0 && r.h > 0 && r.x + r.w <= 800 && r.y + r.h <= 548,
              "fuzzed window remains inside workarea");
    }
}
static uint32_t display[800 * 600], surface[800 * 600];
static SystemSnapshot snapshot;
static void Pump(ModernDesktop &d) {
    snapshot.ticks += 10;
    d.Update(snapshot);
}
static void Key(ModernDesktop &d, uint8_t key) {
    d.OnKeyDown(key);
    Pump(d);
    d.OnKeyUp(key);
    Pump(d);
}
static void DesktopTests() {
    drivers::Framebuffer f;
    Check(f.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "desktop framebuffer");
    ModernDesktop d(&f, 0);
    snapshot.ramMiB = 64;
    snapshot.logicalCPUs = 4;
    snapshot.onlineCPUs = 1;
    snapshot.parkedAPs = 3;
    snapshot.memoryOK = snapshot.schedulerOK = true;
    snapshot.heapKiB = 4096;
    snapshot.heapUsedKiB = 128;
    Pump(d);
    Check(d.Windows().Focused() == ModernWelcome, "desktop starts on welcome");
    Key(d, '2');
    Check(d.Windows().Focused() == ModernMonitor, "monitor keyboard shortcut");
    Key(d, '3');
    Check(d.Windows().Focused() == ModernApplications, "app keyboard shortcut");
    Key(d, '4');
    Key(d, 't');
    Check(d.LightTheme(), "theme applied");
    d.OnKeyDown('t');
    d.OnKeyDown('t');
    Pump(d);
    Check(!d.LightTheme(), "repeat key does not toggle twice");
    d.OnKeyUp('t');
    Pump(d);
    Key(d, '[');
    Check(d.Windows().Window(ModernSettings).minimized, "keyboard minimizes");
    Key(d, '4');
    Check(!d.Windows().Window(ModernSettings).minimized, "shortcut restores");
    Key(d, ']');
    Check(d.Windows().Window(ModernSettings).maximized, "keyboard maximizes");
    Key(d, ']');
    Check(!d.Windows().Window(ModernSettings).maximized, "keyboard restores geometry");
    Key(d, 'l');
    Check(d.LauncherOpen(), "launcher opens");
    Key(d, 'a');
    Key(d, 'p');
    Key(d, 'p');
    Key(d, '\n');
    Check(!d.LauncherOpen() && d.Windows().Focused() == ModernApplications,
          "launcher query and activate");
    Key(d, 'i');
    Key(d, 'u');
    Check(!d.RemovalPending(), "missing store errors are nonfatal");
    Key(d, 'l');
    Key(d, 'z');
    Key(d, '\n');
    Check(d.LauncherOpen(), "empty query result does not index missing app");
    Key(d, 27);
    // Key releases can be dropped during flood; the desktop must clear the state.
    for (uint32_t i = 0; i < 200; ++i)
        d.OnKeyDown('4');
    Pump(d);
    d.OnKeyUp('4');
    Pump(d);
    Key(d, '4');
    Check(d.Windows().Focused() == ModernSettings, "input overflow recovers");
    Key(d, 'l');
    uint32_t outsideQuery[250 * 20];
    for (uint32_t y = 0; y < 20; ++y)
        for (uint32_t x = 0; x < 250; ++x)
            outsideQuery[y * 250 + x] = display[(200 + y) * 800 + 350 + x];
    for (uint32_t i = 0; i < 31; ++i)
        Key(d, 'w');
    for (uint32_t y = 0; y < 20; ++y)
        for (uint32_t x = 0; x < 250; ++x)
            Check(outsideQuery[y * 250 + x] == display[(200 + y) * 800 + 350 + x],
                  "maximum-length launcher query cannot paint outside its panel");
    Key(d, 27);
    // Real VM Host primitives use wide clipping and cannot leave their canvas.
    d.Rect((int32_t)0x80000000u, (int32_t)0x80000000u, 0x7FFFFFFF, 0x7FFFFFFF, 4);
    d.Rect(270, 126, 0x7FFFFFFF, 0x7FFFFFFF, 8);
    d.Text(0x7FFFFFFF, 0x7FFFFFFF, "Clipped", 15);
    d.Number(-4, -4, (int32_t)0x80000000u, 15);
    Pump(d);
}

asm(".section .rodata\n.global desktop_catch_start\ndesktop_catch_start:\n.incbin "
    "\"apps/catch.gtapp\"\n.global desktop_catch_end\ndesktop_catch_end:\n.previous\n");
extern "C" const uint8_t desktop_catch_start[], desktop_catch_end[];
class DesktopDisk : public storage::BlockDevice {
    uint8_t data[storage::SettingsStore::RequiredSectors][512];

  public:
    bool failWrites, failReads;
    DesktopDisk() : failWrites(false), failReads(false) {
        for (uint32_t i = 0; i < sizeof(data); ++i)
            ((uint8_t *)data)[i] = 0;
        const char *magic = "GTSTOR1";
        for (uint32_t i = 0; i < 8; ++i)
            data[0][i] = magic[i];
        apps::Write32(data[0] + 8, 1);
        apps::Write32(data[0] + 12, 512);
        apps::Write32(data[0] + 16, 259);
        apps::Write32(data[0] + 20, 16);
        apps::Write32(data[0] + 24, 16);
        apps::Write32(data[0] + 28, 8);
        apps::Write32(data[0] + 32, 3);
        apps::Write32(data[0] + 508, apps::CRC32(data[0], 508));
        for (uint32_t s = 1; s <= 2; ++s) {
            const char *dm = "GTDIR01";
            for (uint32_t i = 0; i < 8; ++i)
                data[s][i] = dm[i];
            apps::Write32(data[s] + 508, apps::CRC32(data[s], 508));
        }
    }
    virtual bool Identify() {
        return true;
    }
    virtual uint32_t SectorCount() const {
        return storage::SettingsStore::RequiredSectors;
    }
    virtual bool ReadSector(uint32_t sector, uint8_t *out) {
        if (failReads || sector >= SectorCount())
            return false;
        for (uint32_t i = 0; i < 512; ++i)
            out[i] = data[sector][i];
        return true;
    }
    virtual bool WriteSector(uint32_t sector, const uint8_t *in) {
        if (failWrites || sector >= SectorCount())
            return false;
        for (uint32_t i = 0; i < 512; ++i)
            data[sector][i] = in[i];
        return true;
    }
    virtual bool Flush() {
        return true;
    }
};
static int32_t pointerX, pointerY;
static void MoveTo(ModernDesktop &d, int32_t x, int32_t y) {
    while (x != pointerX || y != pointerY) {
        int32_t dx = x - pointerX, dy = y - pointerY;
        if (dx > 100)
            dx = 100;
        if (dx < -100)
            dx = -100;
        if (dy > 100)
            dy = 100;
        if (dy < -100)
            dy = -100;
        d.OnMouseMove(dx, dy);
        pointerX += dx;
        pointerY += dy;
    }
    Pump(d);
}
static void Click(ModernDesktop &d, int32_t x, int32_t y) {
    MoveTo(d, x, y);
    d.OnMouseDown(1);
    Pump(d);
    d.OnMouseUp(1);
    Pump(d);
}
static int32_t Paddle(const ModernDesktop &d) {
    ModernRect r = d.Windows().Window(ModernGame).bounds;
    int32_t scale = (r.w - 48) / 272, sy = (r.h - 124) / 128;
    if (sy < scale)
        scale = sy;
    if (scale < 1)
        scale = 1;
    if (scale > 4)
        scale = 4;
    int32_t gx = r.x + (r.w - 272 * scale) / 2, gy = r.y + 89 + (r.h - 124 - 128 * scale) / 2;
    int32_t sum = 0, count = 0;
    for (int32_t y = gy + 100 * scale; y < gy + 125 * scale; ++y)
        for (int32_t x = gx; x < gx + 272 * scale; ++x)
            if (display[y * 800 + x] == 0xEEDF83) {
                sum += x;
                ++count;
            }
    return count ? sum / count : -1;
}
static void ApplicationTests() {
    DesktopDisk disk;
    storage::AppStore store(&disk);
    Check(store.Mount(), "UI in-memory disk mounted");
    drivers::Framebuffer f;
    Check(f.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600), "app framebuffer");
    ModernDesktop d(&f, &store);
    pointerX = 770;
    pointerY = 16;
    d.SetInstaller(desktop_catch_start, desktop_catch_end - desktop_catch_start);
    Pump(d);
    Key(d, 'i');
    Check(store.Count() == 1, "UI installs real Catch package");
    // A modal or launcher opened while the mouse is held must cancel capture.
    // Cover both title dragging and corner resizing, and movement before/after
    // overlay dismissal (with the physical button still down).
    for (uint32_t resize = 0; resize < 2; ++resize) {
        for (uint32_t overlay = 0; overlay < 2; ++overlay) {
            Key(d, '3');
            ModernRect original = d.Windows().Window(ModernApplications).bounds;
            MoveTo(d, original.x + (resize ? original.w - 7 : 90),
                   original.y + (resize ? original.h - 7 : 18));
            d.OnMouseDown(1);
            Pump(d);
            Key(d, overlay ? 'l' : 'u');
            Check(overlay ? d.LauncherOpen() : d.RemovalPending(),
                  "overlay opens during captured pointer operation");
            MoveTo(d, pointerX + 20, pointerY + 12);
            ModernRect current = d.Windows().Window(ModernApplications).bounds;
            Check(current.x == original.x && current.y == original.y && current.w == original.w &&
                      current.h == original.h,
                  "modal/launcher blocks underlying drag and resize");
            Key(d, 27);
            MoveTo(d, pointerX + 9, pointerY + 7);
            current = d.Windows().Window(ModernApplications).bounds;
            Check(current.x == original.x && current.y == original.y && current.w == original.w &&
                      current.h == original.h,
                  "cancel does not reactivate previous pointer capture");
            d.OnMouseUp(1);
            Pump(d);
            Check(!d.LauncherOpen() && !d.RemovalPending() && store.Count() == 1,
                  "interrupted overlay release preserves app and clears overlay");
        }
    }

    Key(d, '\n');
    Check(d.Windows().Focused() == ModernGame, "UI launches installed VM");
    int32_t before = Paddle(d);
    d.OnKeyDown((char)0x82);
    for (uint32_t i = 0; i < 5; ++i)
        Pump(d);
    d.OnKeyUp((char)0x82);
    Pump(d);
    int32_t after = Paddle(d);
    Check(before >= 0 && after > before + 20,
          "game adapter and held-key input move visible paddle");
    Key(d, 'r');
    Check(Paddle(d) < after, "game restart resets paddle");
    Key(d, 27);
    Check(!d.Windows().Window(ModernGame).open, "game closes");
    Key(d, '3');
    Key(d, 'u');
    Check(d.RemovalPending() && store.Count() == 1, "remove asks before mutating store");
    Key(d, 27);
    Check(!d.RemovalPending() && store.Count() == 1, "cancel preserves package");
    Key(d, 'u');
    Key(d, '\n');
    Check(store.Count() == 0, "confirmed removal persists");
    storage::AppStore remount(&disk);
    Check(remount.Mount() && remount.Count() == 0, "UI removal survives remount");
    Key(d, 'i');
    Key(d, '\n');
    ModernRect game = d.Windows().Window(ModernGame).bounds;
    Click(d, game.x + game.w - 95, game.y + 18);
    Check(d.Windows().Window(ModernGame).minimized, "titlebar minimizes game");
    Click(d, 68 + 4 * 118 + 50, 574);
    Check(!d.Windows().Window(ModernGame).minimized && d.Windows().Focused() == ModernGame,
          "taskbar restores game");
    game = d.Windows().Window(ModernGame).bounds;
    Click(d, game.x + game.w - 22, game.y + 18);
    Check(!d.Windows().Window(ModernGame).open, "titlebar closes game");
    Key(d, '2');
    ModernRect monitor = d.Windows().Window(ModernMonitor).bounds;
    MoveTo(d, monitor.x + 120, monitor.y + 18);
    d.OnMouseDown(1);
    Pump(d);
    MoveTo(d, monitor.x + 80, monitor.y + 2);
    d.OnMouseUp(1);
    Pump(d);
    ModernRect moved = d.Windows().Window(ModernMonitor).bounds;
    Check(moved.x == monitor.x - 40 && moved.y == monitor.y - 16, "title drag moves actual window");
    MoveTo(d, moved.x + moved.w - 7, moved.y + moved.h - 7);
    d.OnMouseDown(1);
    Pump(d);
    MoveTo(d, pointerX + 20, pointerY + 12);
    d.OnMouseUp(1);
    Pump(d);
    ModernRect resized = d.Windows().Window(ModernMonitor).bounds;
    Check(resized.x == moved.x && resized.y == moved.y && resized.w == moved.w + 20 &&
              resized.h == moved.h + 12,
          "resize keeps top-left anchored");
    Click(d, resized.x + resized.w - 61, resized.y + 18);
    Check(d.Windows().Window(ModernMonitor).maximized, "mouse maximizes");
    ModernRect max = d.Windows().Window(ModernMonitor).bounds;
    Click(d, max.x + max.w - 61, max.y + 18);
    Check(!d.Windows().Window(ModernMonitor).maximized, "mouse restores");
    // Fill eight slots to exercise scrolling and select the final entry via keyboard.
    uint8_t package[apps::PackageLimit];
    uint32_t length = desktop_catch_end - desktop_catch_start;
    for (uint32_t i = 0; i < length; ++i)
        package[i] = desktop_catch_start[i];
    for (uint32_t j = 0; j < 7; ++j) {
        package[32] = '0' + j;
        package[56] = 'A' + j;
        apps::Write32(package + 120, apps::PackageCRC(package, length));
        Check(store.Install(package, length), "populate app rows");
    }
    Check(store.Count() == 8, "eight app capacity");
    Key(d, '3');
    for (uint32_t i = 0; i < 7; ++i)
        Key(d, 0x84);
    Key(d, '\n');
    Check(d.Windows().Focused() == ModernGame, "last scrolled app launches");
    Check(d.ActiveApplicationID() && d.ActiveApplicationID()[0] == '6' &&
              d.ActiveApplicationID()[1] == 'a',
          "eighth app selection loads its exact package, not the first row");
    Key(d, 27);
    Key(d, 'l');
    Key(d, 'w');
    Key(d, 'w');
    Key(d, 'w');
    uint32_t outsideTitle[250 * 20];
    for (uint32_t y = 0; y < 20; ++y)
        for (uint32_t x = 0; x < 250; ++x)
            outsideTitle[y * 250 + x] = display[(249 + y) * 800 + 350 + x];
    package[32] = 'c'; // Replace the original Catch ID; preserve all eight slots.
    for (uint32_t i = 56; i < 79; ++i)
        package[i] = 'W';
    package[79] = 0;
    apps::Write32(package + 120, apps::PackageCRC(package, length));
    Check(store.Install(package, length), "install maximum-length app title");
    Key(d, '\b');
    Key(d, 'w');
    for (uint32_t y = 0; y < 20; ++y)
        for (uint32_t x = 0; x < 250; ++x)
            Check(outsideTitle[y * 250 + x] == display[(249 + y) * 800 + 350 + x],
                  "maximum-length launcher title cannot paint outside its panel");
    Key(d, 27);
}
static bool Same(const char *a, const char *b) {
    if (!a || !b)
        return false;
    for (uint32_t i = 0; i < 4096; ++i) {
        if (a[i] != b[i])
            return false;
        if (!a[i])
            return true;
    }
    return false;
}
static void Type(ModernDesktop &desktop, const char *ascii) {
    for (uint32_t i = 0; ascii[i]; ++i)
        Key(desktop, ascii[i]);
}
static void CheckGameHan(ModernDesktop &d, uint32_t cp, int32_t cx, int32_t cy) {
    i18n::Glyph glyph = {};
    Check(i18n::LookupGlyph(cp, 1, glyph), "Chinese game glyph available");
    ModernRect r = d.Windows().Window(ModernGame).bounds;
    int32_t scale = (r.w - 48) / 272, vertical = (r.h - 124) / 128;
    if (vertical < scale)
        scale = vertical;
    if (scale > 4)
        scale = 4;
    int32_t gx = r.x + (r.w - 272 * scale) / 2, gy = r.y + 89 + (r.h - 124 - 128 * scale) / 2;
    for (uint32_t row = 0; row < glyph.height; ++row)
        for (uint32_t col = 0; col < glyph.advance; ++col) {
            uint32_t at = row * glyph.width + col;
            uint8_t a = at & 1 ? glyph.pixels[at / 2] & 15 : glyph.pixels[at / 2] >> 4;
            uint32_t pixel = display[(gy + (cy + row) * scale) * 800 + gx + (cx + col) * scale];
            Check(pixel == (a >= 7 ? 0xFFFFFFu : 0x182633u),
                  "bundled game draws real bounded Han glyph pixels");
        }
}
static void LocalizationTests() {
    DesktopDisk disk;
    storage::AppStore store(&disk);
    Check(store.Mount(), "localized app disk");
    storage::SettingsStore settings(&disk);
    Check(settings.Load(), "localized settings disk");
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "localized framebuffer");
    ModernPainter painter(fb);
    Check(painter.TextWidth("应用", 1) == 28 && painter.TextWidth("应用", 2) == 56,
          "UTF-8 text width uses scalars and Chinese advances");
    painter.Text(-9, -3, "中文 / 应用", 0xFFFFFF);
    painter.Text(4, 4, "\xF0\x28\x8C\x28", 0xFFFFFF);
    fb.Present();
    ModernDesktop d(&fb, &store, &settings);
    pointerX = 770;
    pointerY = 16;
    d.SetInstaller(desktop_catch_start, desktop_catch_end - desktop_catch_start);
    Pump(d);
    Key(d, '4');
    Key(d, 'c');
    Check(d.CurrentLocale() == i18n::SimplifiedChinese && d.PinyinInput(),
          "language selector enables Chinese and pinyin");
    Check(settings.Current().locale == storage::SimplifiedChinese &&
              settings.HasPersistedSettings(),
          "language choice persisted");
    Key(d, 't');
    Check(d.LightTheme() && settings.Current().theme == storage::Light,
          "theme saves alongside language");
    Key(d, 'l');
    Type(d, "yingyong");
    Check(d.Composition().Active() && Same(d.Composition().Candidate(0), "应用"),
          "pinyin exposes Chinese phrase");
    Key(d, ' ');
    Check(Same(d.SearchQuery(), "应用") && d.SearchCursor() == 6,
          "candidate commits complete UTF-8 phrase");
    Key(d, 0x81);
    Check(d.SearchCursor() == 3, "left cursor moves one scalar");
    Key(d, '\b');
    Check(Same(d.SearchQuery(), "用") && d.SearchCursor() == 0,
          "backspace removes one scalar before cursor");
    Type(d, "ying");
    Key(d, '1');
    Check(Same(d.SearchQuery(), "应用") && d.SearchCursor() == 3,
          "pinyin inserts at middle cursor boundary");
    Key(d, 0x82);
    Check(d.SearchCursor() == 6, "right cursor advances complete scalar");
    Key(d, '\n');
    Check(d.Windows().Focused() == ModernApplications && !d.LauncherOpen(),
          "Chinese query opens localized application window");
    Key(d, 'l');
    Type(d, "shezhi");
    Key(d, ' ');
    Key(d, '\n');
    Check(d.Windows().Focused() == ModernSettings, "localized settings alias is searchable");
    for (uint32_t choice = 1; choice <= 9; ++choice) {
        Key(d, 'l');
        Type(d, "shi");
        Check(d.Composition().CandidateCount() == 9, "all nine pinyin choices available");
        const char *expected = d.Composition().Candidate(choice - 1);
        // All candidate cells are within the framebuffer and visibly painted.
        int32_t bx = 26 + (choice - 1) % 3 * 162, by = 73 + (choice - 1) / 3 * 26;
        Check(display[(by + 1) * 800 + bx + 2] == 0xE2E9EFu, "numbered candidate cell is rendered");
        Key(d, '0' + choice);
        Check(Same(d.SearchQuery(), expected), "number key selects exact candidate");
        Key(d, 27);
    }
    Key(d, 'l');
    Type(d, "shi");
    Click(d, 26 + 2 * 162 + 50, 73 + 2 * 26 + 12);
    Check(Same(d.SearchQuery(), "世") && !d.Composition().Active(),
          "mouse selects ninth visible candidate");
    Type(d, "ying");
    Key(d, 27);
    Check(d.LauncherOpen() && !d.Composition().Active(), "Esc cancels composition before launcher");
    Key(d, 27);
    Key(d, 'l');
    Type(d, "ying");
    Click(d, 600, 50);
    Check(!d.LauncherOpen() && !d.Composition().Active(),
          "outside click cancels preedit without committing");
    Key(d, 'l');
    Key(d, '`');
    Check(!d.PinyinInput(), "direct input can be selected");
    d.OnKeyDown('`');
    d.OnKeyDown('`');
    Pump(d);
    d.OnKeyUp('`');
    Pump(d);
    Check(d.PinyinInput() && !d.SearchQuery()[0],
          "input-mode key repeat does not insert literal backticks");
    Key(d, '`');

    for (uint32_t i = 0; i < 125; ++i)
        Key(d, 'x');
    Key(d, '`');
    Type(d, "shi");
    Key(d, ' ');
    Check(d.Composition().Active() && i18n::ByteLength(d.SearchQuery(), 128) == 125,
          "full query preserves uncommitted candidate");
    Key(d, 27);
    Key(d, 27);
    Key(d, 'i');
    Key(d, '\n');
    Check(d.Windows().Focused() == ModernGame, "Chinese bundled game launches");
    CheckGameHan(d, 0x63A5, 8, 1);
    CheckGameHan(d, 0x79FB, 32, 109);
    Key(d, 27);
    uint8_t package[apps::PackageLimit];
    uint32_t length = desktop_catch_end - desktop_catch_start;
    for (uint32_t i = 0; i < length; ++i)
        package[i] = desktop_catch_start[i];
    package[32] = 'z';
    apps::Write32(package + 120, apps::PackageCRC(package, length));
    Check(store.Install(package, length), "third-party lookalike package installed");
    Key(d, '3');
    Key(d, 0x84);
    Key(d, '\n');
    Check(d.ActiveApplicationID() && d.ActiveApplicationID()[0] == 'z',
          "arbitrary package retains original identity");
    ModernRect game = d.Windows().Window(ModernGame).bounds;
    int32_t gx = game.x + (game.w - 544) / 2, gy = game.y + 89 + (game.h - 124 - 256) / 2;
    const uint8_t *cg = ModernGameGlyph('C');
    for (int32_t row = 0; row < 7; ++row)
        for (int32_t col = 0; col < 5; ++col)
            Check(display[(gy + (7 + row) * 2) * 800 + gx + (8 + col) * 2] ==
                      ((cg[row] & (1 << (4 - col))) ? 0xFFFFFFu : 0x182633u),
                  "arbitrary app prompt is not translated");
    Key(d, 27);
    Key(d, '3');
    Key(d, 'l');
    Type(d, "shi");
    disk.failReads = true;
    Click(d, 100, 428);
    Check(!d.LauncherOpen() && !d.Composition().Active(),
          "failed app activation cancels hidden preedit");
    disk.failReads = false;
    Key(d, 'l');
    Check(!d.Composition().Active() && !d.SearchQuery()[0],
          "reopened launcher never resumes failed activation preedit");
    Key(d, 27);
    Key(d, '4');
    disk.failWrites = true;
    Key(d, 'c');
    Check(d.CurrentLocale() == i18n::English &&
              settings.Current().locale == storage::SimplifiedChinese,
          "failed save keeps session change and previous stored settings distinct");
    disk.failWrites = false;
    storage::SettingsStore reboot(&disk);
    Check(reboot.Load(), "settings remount after failed write");
    ModernDesktop restored(&fb, &store, &reboot);
    Check(restored.CurrentLocale() == i18n::SimplifiedChinese && restored.LightTheme(),
          "locale and theme restore after remount");
}
static void MinimumMonitorTests() {
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(640, 480, 2560), (uint8_t *)display, surface, 640 * 480),
          "minimum desktop framebuffer");
    ModernDesktop d(&fb, 0);
    pointerX = 610;
    pointerY = 16;
    snapshot.workerCPUs = 3;
    snapshot.onlineCPUs = 1;
    snapshot.workPoolOK = true;
    static uint32_t footer[640 * 49];
    for (uint32_t language = 0; language < 2; ++language) {
        if (language) {
            Key(d, '4');
            Key(d, 'c');
        }
        Key(d, '2');
        ModernRect r = d.Windows().Window(ModernMonitor).bounds;
        MoveTo(d, r.x + r.w - 7, r.y + r.h - 7);
        d.OnMouseDown(1);
        Pump(d);
        MoveTo(d, pointerX - 100, pointerY - 100);
        d.OnMouseUp(1);
        Pump(d);
        MoveTo(d, 610, 16);
        r = d.Windows().Window(ModernMonitor).bounds;
        Check(r.h == 398 && r.w == 540, "monitor respects its existing minimum size");
        snapshot.completedJobs = 0;
        snapshot.ticks += 100;
        Pump(d);
        uint32_t count = 0;
        for (int32_t y = r.y + r.h - 49; y < r.y + r.h; ++y)
            for (int32_t x = r.x + 24; x < r.x + r.w - 24; ++x)
                footer[count++] = display[y * 640 + x];
        snapshot.completedJobs = 0xFFFFFFFFU;
        snapshot.ticks += 100;
        Pump(d);
        count = 0;
        for (int32_t y = r.y + r.h - 49; y < r.y + r.h; ++y)
            for (int32_t x = r.x + 24; x < r.x + r.w - 24; ++x)
                Check(footer[count++] == display[y * 640 + x],
                      "live worker count never overlaps minimum-height footer in either locale");
    }
}
static void RunTests() {
    FramebufferTests();
    GeometryTests();
    DesktopTests();
    ApplicationTests();
    LocalizationTests();
    MinimumMonitorTests();
    Output("Desktop/framebuffer safety and interaction tests passed\n");
}
#ifdef GTOS_DESKTOP_SANITIZE
int main() {
    RunTests();
    return 0;
}
#else
extern "C" void _start() {
    RunTests();
    Exit(0);
}
#endif
