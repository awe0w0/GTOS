// Deterministic real desktop/framebuffer tests, executable without libc.
#include <gui/modern_desktop.h>
#include <process/native_windows.h>
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
static uint32_t imagePresented, imageClosed;
static bool TraceStarts(const char *text, const char *prefix) {
    for (uint32_t i = 0; prefix[i]; ++i)
        if (text[i] != prefix[i])
            return false;
    return true;
}
void printf(char *text) {
    if (TraceStarts(text, "GTOS NATIVE IMAGE PRESENTED V1 "))
        ++imagePresented;
    if (TraceStarts(text, "GTOS NATIVE IMAGE CLOSED V1 "))
        ++imageClosed;
}
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
        ModernWindowKind k = (ModernWindowKind)(random % ModernWindowCount);
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
    Key(d, 'r');
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
    bool failWrites, failReads, failIdentify;
    uint32_t reads, writes, flushes;
    DesktopDisk() : failWrites(false), failReads(false), failIdentify(false),
                    reads(0), writes(0), flushes(0) {
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
        return !failIdentify;
    }
    virtual uint32_t SectorCount() const {
        return storage::SettingsStore::RequiredSectors;
    }
    virtual bool ReadSector(uint32_t sector, uint8_t *out) {
        ++reads;
        if (failReads || sector >= SectorCount())
            return false;
        for (uint32_t i = 0; i < 512; ++i)
            out[i] = data[sector][i];
        return true;
    }
    virtual bool WriteSector(uint32_t sector, const uint8_t *in) {
        ++writes;
        if (failWrites || sector >= SectorCount())
            return false;
        for (uint32_t i = 0; i < 512; ++i)
            data[sector][i] = in[i];
        return true;
    }
    virtual bool Flush() {
        ++flushes;
        return true;
    }
    void CorruptHeader(bool corrupt) { data[0][0] = corrupt ? 'X' : 'G'; }
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
    Key(d, '3');
    Key(d, 'r');
    Key(d, '\n');
    Check(d.ActiveApplicationID() && d.ActiveApplicationID()[0] == '6',
          "reload preserves eighth application selection by ID");
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
static void DiskReloadTests() {
    DesktopDisk disk;
    storage::AppStore store(&disk);
    Check(store.Mount(), "reload seed disk mounted");
    Check(store.Install(desktop_catch_start, desktop_catch_end - desktop_catch_start),
          "reload seed real package installed");
    drivers::Framebuffer f;
    Check(f.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "reload framebuffer bound");
    ModernDesktop d(&f, &store);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    Key(d, '3');
    uint32_t writes = disk.writes, flushes = disk.flushes, generation = store.Generation();
    disk.failReads = true;
    Key(d, 'r');
    Check(!store.Mounted() && !store.Count() && store.Status() == storage::AppStore::IOFailure,
          "desktop reload exposes I/O failure and removes stale listing");
    disk.failReads = false;
    ModernRect r = d.Windows().Window(ModernApplications).bounds;
    Click(d, r.x + r.w - 82, r.y + 61);
    Check(store.Mounted() && store.Count() == 1 && store.Generation() == generation,
          "mouse reload recovers original directory without restart");
    Key(d, '\n');
    Check(d.ActiveApplicationID() && Same(d.ActiveApplicationID(), "catch"),
          "recovered disk package launches through real VM");
    uint32_t reads = disk.reads;
    Key(d, 'r');
    Check(disk.reads == reads, "game R remains restart and never remounts disk");
    Key(d, 27);
    Key(d, '3');
    Key(d, 'u');
    Check(d.RemovalPending(), "reload modal guard seed");
    Key(d, 'r');
    Check(d.RemovalPending() && disk.reads == reads, "removal modal consumes R without disk access");
    Key(d, 27);
    Key(d, 'l');
    Key(d, 'r');
    Check(d.LauncherOpen() && Same(d.SearchQuery(), "r") && disk.reads == reads,
          "launcher R stays search input");
    Key(d, 27);
    disk.failIdentify = true;
    Key(d, 'r');
    Check(!store.Mounted() && store.Status() == storage::AppStore::NoDisk, "reload reports missing disk");
    disk.failIdentify = false;
    disk.CorruptHeader(true);
    Key(d, 'r');
    Check(!store.Mounted() && store.Status() == storage::AppStore::NotFormatted,
          "reload refuses foreign media without formatting");
    disk.CorruptHeader(false);
    Key(d, 'r');
    Check(store.Mounted() && store.Count() == 1, "read-only recovery preserves acknowledged package");
    Check(disk.writes == writes && disk.flushes == flushes && store.Generation() == generation,
          "all reload paths perform zero writes, flushes or generation updates");
    // R outside the Applications window must not initiate disk I/O.
    reads = disk.reads;
    Key(d, '4');
    Key(d, 'r');
    Check(disk.reads == reads, "settings R does not reload disk");
    Key(d, 'c');
    Key(d, '3');
    Click(d, r.x + r.w - 82, r.y + 61);
    Check(d.CurrentLocale() == i18n::SimplifiedChinese && store.Mounted(),
          "Chinese mouse reload uses the same real disk path");
    Check(Same(i18n::Translate(i18n::SimplifiedChinese, "Reload disk"), "读取磁盘"),
          "reload button has Chinese text");
}

class DesktopImageSource : public NativeImageProvider {
  public:
    NativeImageSnapshot frame;
    bool available, repeat;
    mutable uint32_t known;
    DesktopImageSource() : frame(), available(false), repeat(false), known(0) {}
    void Publish(uint32_t generation, uint32_t width = 32, uint32_t height = 32) {
        frame.generation = generation;
        frame.width = width;
        frame.height = height;
        for (uint32_t i = 0; i < NativeImageMaximumBytes / 4; ++i) {
            uint8_t alpha = i & 255;
            frame.rgba[i * 4] = alpha;
            frame.rgba[i * 4 + 1] = alpha / 2;
            frame.rgba[i * 4 + 2] = 0;
            frame.rgba[i * 4 + 3] = alpha;
        }
        available = true;
    }
    virtual bool CopyLatest(unsigned int generation, NativeImageSnapshot &out) const {
        known = generation;
        if (!available || (!repeat && generation == frame.generation))
            return false;
        out = frame;
        return true;
    }
};
static void CheckImagePixels(const ModernDesktop &d, const NativeImageSnapshot &image,
                             uint32_t stride = 800) {
    ModernRect r = d.Windows().Window(ModernImage).bounds;
    int32_t scale = (r.w - 48) / image.width, vertical = (r.h - 124) / image.height;
    if (vertical < scale)
        scale = vertical;
    if (scale > 6)
        scale = 6;
    int32_t gx = r.x + (r.w - (int32_t)image.width * scale) / 2,
            gy = r.y + 82 + (r.h - 124 - (int32_t)image.height * scale) / 2;
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint32_t background = ((x / 4 + y / 4) & 1) ? 0xA7B5C2 : 0xDDE5EC;
            const uint8_t *pixel = image.rgba + (y * image.width + x) * 4;
            uint32_t expected = 0;
            for (uint32_t channel = 0; channel < 3; ++channel) {
                uint32_t shift = 16 - channel * 8;
                expected |= ((pixel[channel] * 255 + ((background >> shift) & 255) *
                            (255 - pixel[3]) + 127) / 255) << shift;
            }
            for (int32_t dy = 0; dy < scale; ++dy)
                for (int32_t dx = 0; dx < scale; ++dx) {
                    uint32_t at = (gy + y * scale + dy) * stride + gx + x * scale + dx;
                    Check(display[at] == expected && surface[at] == expected,
                          "native RGBA8 alpha and canonical RGB framebuffer pixels are exact");
                }
        }
}
static void NativeImageTests() {
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "native image framebuffer");
    DesktopImageSource source;
    ModernDesktop d(&fb, 0, 0, true, &source);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    Key(d, '5');
    Check(!d.Windows().Window(ModernImage).open && d.Windows().Focused() == ModernWelcome,
          "image shortcut without a published frame leaves desktop usable");
    source.Publish(1, 0, 32);
    Pump(d);
    Key(d, '5');
    Check(!d.Windows().Window(ModernImage).open && source.known == 1,
          "invalid first frame is rejected and its generation is consumed");
    uint32_t receipts = imagePresented, closures = imageClosed;
    source.Publish(2);
    d.Update(snapshot);
    Check(imagePresented == receipts,
          "a copied image is not reported as presented before framebuffer redraw");
    Pump(d);
    Check(imagePresented == receipts + 1,
          "native image presentation receipt follows actual framebuffer redraw");
    Check(d.Windows().Window(ModernImage).open && d.Windows().Focused() == ModernImage,
          "new valid provider snapshot opens native image window");
    NativeImageSnapshot saved = source.frame;
    CheckImagePixels(d, saved);
    source.available = false;
    for (uint32_t i = 0; i < sizeof(source.frame.rgba); ++i)
        source.frame.rgba[i] = 0;
    Key(d, ']');
    CheckImagePixels(d, saved);
    Key(d, ']');
    CheckImagePixels(d, saved);
    Check(d.Windows().Window(ModernImage).open,
          "desktop snapshot survives producer release and source buffer overwrite");
    source.available = source.repeat = true;
    Key(d, '2');
    Pump(d);
    Check(d.Windows().Focused() == ModernMonitor,
          "unchanged generation never steals focus even if provider repeats it");
    Key(d, '5');
    CheckImagePixels(d, saved);
    Key(d, '[');
    for (uint32_t i = 0; i < 8; ++i)
        Pump(d);
    Check(d.Windows().Window(ModernImage).minimized && d.Windows().Focused() != ModernImage,
          "unchanged generation never restores minimized image");
    Key(d, '5');
    Check(!d.Windows().Window(ModernImage).minimized && d.Windows().Focused() == ModernImage,
          "5 explicitly restores minimized native image");
    Key(d, 27);
    receipts = imagePresented;
    for (uint32_t i = 0; i < 8; ++i)
        Pump(d);
    Check(!d.Windows().Window(ModernImage).open && imagePresented == receipts &&
              imageClosed == closures + 1,
          "Esc closes image and repeated generation cannot reopen or report it");
    int32_t focus = d.Windows().Focused();
    Click(d, 68 + 5 * 118 + 50, 574);
    Check(!d.Windows().Window(ModernImage).open && d.Windows().Focused() == focus,
          "sixth window creates no extra dock hit target");
    Key(d, '5');
    CheckImagePixels(d, saved);
    ModernRect r = d.Windows().Window(ModernImage).bounds;
    Click(d, r.x + r.w - 22, r.y + 18);
    Pump(d);
    Check(!d.Windows().Window(ModernImage).open && imageClosed == closures + 2,
          "titlebar image close persists for unchanged generation");
    source.Publish(3, 1, 1);
    source.frame.rgba[0] = 0x34;
    source.frame.rgba[1] = 0x56;
    source.frame.rgba[2] = 0x78;
    source.frame.rgba[3] = 255;
    Pump(d);
    saved = source.frame;
    Check(d.Windows().Focused() == ModernImage,
          "a newly published generation reopens a previously closed image");
    CheckImagePixels(d, saved);
    const uint32_t badWidths[] = {0, 33, 32, 32, 0xFFFFFFFFu, 1},
                   badHeights[] = {1, 1, 0, 33, 0xFFFFFFFFu, 0xFFFFFFFFu};
    for (uint32_t i = 0; i < 6; ++i) {
        source.Publish(4 + i, badWidths[i], badHeights[i]);
        Pump(d);
        CheckImagePixels(d, saved);
        Check(source.known == 3 + i,
              "provider polling uses the previous observed generation");
        Pump(d);
        Check(source.known == 4 + i,
              "invalid dimensions consume only one generation without retrying forever");
    }
    for (uint32_t channel = 0; channel < 3; ++channel) {
        source.Publish(10 + channel, 1, 1);
        source.frame.rgba[channel] = 1; // alpha is zero at the first pixel.
        Pump(d);
        CheckImagePixels(d, saved);
    }
    Key(d, 27);
    source.Publish(13, 33, 33);
    Pump(d);
    Check(!d.Windows().Window(ModernImage).open,
          "invalid new frame cannot reopen a closed valid image");
    Key(d, '5');
    CheckImagePixels(d, saved);
    Key(d, '4');
    Key(d, 'c');
    Key(d, '5');
    Check(d.CurrentLocale() == i18n::SimplifiedChinese,
          "native image keeps live desktop language selection");
    Check(Same(i18n::Translate(i18n::SimplifiedChinese, "Native image"), "原生 PNG") &&
          Same(i18n::Translate(i18n::SimplifiedChinese, "Image from native application"),
               "原生应用的 PNG"), "native image text has Chinese catalog mappings");
    CheckImagePixels(d, saved);
    i18n::Glyph glyph = {};
    Check(i18n::LookupGlyph(0x539F, 1, glyph), "native image Chinese glyph is covered");
    r = d.Windows().Window(ModernImage).bounds;
    for (uint32_t y = 0; y < glyph.height; ++y)
        for (uint32_t x = 0; x < glyph.advance; ++x) {
            uint32_t at = y * glyph.width + x;
            uint32_t alpha = at & 1 ? glyph.pixels[at / 2] & 15 : glyph.pixels[at / 2] >> 4;
            uint32_t expected = 0;
            for (uint32_t shift = 0; shift <= 16; shift += 8)
                expected |= ((((0xA3B6C8u >> shift) & 255) * alpha +
                              ((0x18232Fu >> shift) & 255) * (15 - alpha) + 7) / 15) << shift;
            Check(display[(r.y + 54 + y) * 800 + r.x + 24 + x] == expected,
                  "Chinese native image header draws its real atlas glyph");
        }
    // The minimum desktop must also keep the full maximum source inside its window.
    drivers::Framebuffer minimum;
    Check(minimum.Bind(Mode(640, 480, 2560), (uint8_t *)display, surface, 640 * 480),
          "minimum native image framebuffer");
    DesktopImageSource smallSource;
    smallSource.Publish(1);
    ModernDesktop small(&minimum, 0, 0, false, &smallSource);
    pointerX = 610;
    pointerY = 16;
    Pump(small);
    CheckImagePixels(small, smallSource.frame, 640);
    MoveTo(small, small.Windows().Window(ModernImage).bounds.x + 353,
                  small.Windows().Window(ModernImage).bounds.y + 293);
    small.OnMouseDown(1);
    Pump(small);
    MoveTo(small, pointerX - 100, pointerY - 100);
    small.OnMouseUp(1);
    Pump(small);
    MoveTo(small, 610, 16);
    r = small.Windows().Window(ModernImage).bounds;
    Check(r.w == 320 && r.h == 240, "native image window honors minimum dimensions");
    CheckImagePixels(small, smallSource.frame, 640);
    Output("Native image snapshot, alpha8, close/reopen and bilingual glyph tests passed\n");
}

// Host memory standing in for user pages, so the tests read the real bank queue
// through the real syscall-shaped Call() rather than peeking at internals.
struct DesktopArena : process::UserMemory {
    uint8_t bytes[8192];
    DesktopArena() { for (uint32_t i = 0; i < sizeof(bytes); ++i) bytes[i] = 0; }
    bool CopyFromUser(void *destination, unsigned source, unsigned length) {
        if (source > sizeof(bytes) || length > sizeof(bytes) - source) return false;
        const uint8_t *from = bytes + source;
        uint8_t *to = (uint8_t *)destination;
        for (unsigned i = 0; i < length; ++i) to[i] = from[i];
        return true;
    }
    bool CopyToUser(unsigned destination, const void *source, unsigned length) {
        if (destination > sizeof(bytes) || length > sizeof(bytes) - destination)
            return false;
        const uint8_t *from = (const uint8_t *)source;
        uint8_t *to = bytes + destination;
        for (unsigned i = 0; i < length; ++i) to[i] = from[i];
        return true;
    }
};

// Drain one window's queue through the production syscall entry. A negative
// result is a real error and is reported as a failure: it is never treated as
// "the queue was empty".
static int DrainEvents(process::NativeWindows &bank, uint32_t owner, uint32_t handle,
                       GtosInputEvent *out, uint32_t capacity, const char *label) {
    DesktopArena arena;
    GtosInputReadRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.handle = handle;
    request.count = capacity < GTOS_INPUT_READ_LIMIT ? capacity : GTOS_INPUT_READ_LIMIT;
    request.destination = 4096;
    const uint8_t *raw = (const uint8_t *)&request;
    for (uint32_t i = 0; i < sizeof(request); ++i) arena.bytes[64 + i] = raw[i];
    const int delivered = bank.Call(owner, arena, GTOS_SYS_INPUT_READ, 64, sizeof(request));
    Check(delivered >= 0, label);
    if (delivered <= 0) return 0;
    const GtosInputEvent *events = (const GtosInputEvent *)(arena.bytes + 4096);
    for (int i = 0; i < delivered; ++i) out[i] = events[i];
    return delivered;
}

// WINDOW_INFO through the same entry: the only trustworthy way to know the real
// queue depth before claiming a boundary.
static int QueryInfo(process::NativeWindows &bank, uint32_t owner, uint32_t handle,
                     GtosWindowInfoResponse &info) {
    DesktopArena arena;
    GtosWindowInfoRequest request;
    request.version = GTOS_WINDOW_ABI_VERSION;
    request.handle = handle;
    request.result_va = 2048;
    const uint8_t *raw = (const uint8_t *)&request;
    for (uint32_t i = 0; i < sizeof(request); ++i) arena.bytes[64 + i] = raw[i];
    const int result = bank.Call(owner, arena, GTOS_SYS_WINDOW_INFO, 64, sizeof(request));
    if (result < 0) return result;
    info = *(const GtosWindowInfoResponse *)(arena.bytes + 2048);
    return result;
}


// The same content rectangle the desktop uses, recomputed from the published
// view so the test checks real framebuffer pixels.
static ModernRect NativeContentOf(const ModernDesktop &d, uint32_t slot,
                                  const NativeWindowView &view) {
    ModernRect r = d.Windows().Window((ModernWindowKind)(ModernNative0 + slot)).bounds;
    ModernRect out = {0, 0, 0, 0};
    if (!view.width || !view.height) return out;
    const int32_t aw = r.w - 48, ah = r.h - 124;
    int32_t scale = aw / (int32_t)view.width;
    const int32_t vertical = ah / (int32_t)view.height;
    if (vertical < scale) scale = vertical;
    if (scale > 6) scale = 6;
    if (scale <= 0) return out;
    const int32_t w = (int32_t)view.width * scale, h = (int32_t)view.height * scale;
    out.x = r.x + (r.w - w) / 2;
    out.y = r.y + 82 + (ah - h) / 2;
    out.w = w;
    out.h = h;
    return out;
}

static uint32_t ExpectedNativePixel(const NativeImageSnapshot &frame, uint32_t x, uint32_t y) {
    const uint32_t background = ((x / 4 + y / 4) & 1) ? 0xA7B5C2 : 0xDDE5EC;
    const uint8_t *pixel = frame.rgba + (y * frame.width + x) * 4;
    uint32_t expected = 0;
    for (uint32_t channel = 0; channel < 3; ++channel) {
        const uint32_t shift = 16 - channel * 8;
        expected |= ((pixel[channel] * 255 + ((background >> shift) & 255) *
                      (255 - pixel[3]) + 127) / 255) << shift;
    }
    return expected;
}

// Queue events without draining: the IRQ side only enqueues.
static void QueueKeyDown(ModernDesktop &d, uint8_t key) { d.OnKeyDown((char)key); }
static void QueueDown(ModernDesktop &d) { d.OnMouseDown(1); }
static void QueueUp(ModernDesktop &d) { d.OnMouseUp(1); }
static void QueueMoveTo(ModernDesktop &d, int32_t x, int32_t y) {
    while (x != pointerX || y != pointerY) {
        int32_t dx = x - pointerX, dy = y - pointerY;
        if (dx > 100) dx = 100;
        if (dx < -100) dx = -100;
        if (dy > 100) dy = 100;
        if (dy < -100) dy = -100;
        pointerX += dx;
        pointerY += dy;
        d.OnMouseMove((int8_t)dx, (int8_t)dy);
    }
}
// One update only. Ticks advance by the minimum the 2-tick draw throttle needs,
// and the whole test stays well below the 50-tick monitor refresh so a redraw
// can only come from the native synchronisation itself.
static void OneUpdate(ModernDesktop &d) {
    snapshot.ticks += 3;
    d.Update(snapshot);
}

// A real press that stays held. Unlike Click() this never sends the release, so a
// capture is genuinely active for the steps that follow.
static void BeginPress(ModernDesktop &d, int32_t x, int32_t y) {
    MoveTo(d, x, y); // hover first so no window is dragged on the way
    d.OnMouseDown(1);
    Pump(d);
}
static void EndPress(ModernDesktop &d, int32_t x, int32_t y) {
    MoveTo(d, x, y);
    d.OnMouseUp(1);
    Pump(d);
}
// Press without moving first, for cases where the pointer must stay exactly where
// the previous step left it.
static void PressHere(ModernDesktop &d) {
    d.OnMouseDown(1);
    Pump(d);
}
// One hover: bounded steps, each a real pointer move with the button up.
static void HoverStep(ModernDesktop &d, int32_t x, int32_t y) {
    while (x != pointerX || y != pointerY) {
        int32_t dx = x - pointerX, dy = y - pointerY;
        if (dx > 100) dx = 100;
        if (dx < -100) dx = -100;
        if (dy > 100) dy = 100;
        if (dy < -100) dy = -100;
        pointerX += dx;
        pointerY += dy;
        d.OnMouseMove((int8_t)dx, (int8_t)dy);
        Pump(d);
    }
}
// Bring the window's normal queue to exactly `target` entries, then prove the
// depth with WINDOW_INFO. The starting depth is measured, never assumed, and
// every fill step is one pixel inside the content so it routes exactly one move.
static bool FillQueueTo(ModernDesktop &d, process::NativeWindows &bank, uint32_t owner,
                        uint32_t handle, const ModernRect &content, uint32_t target) {
    if (content.w < 4 || content.h < 4) return false;
    const int32_t cx = content.x + content.w / 2;
    const int32_t cy = content.y + content.h / 2;
    MoveTo(d, cx, cy); // approach in bounded steps
    GtosWindowInfoResponse info;
    if (QueryInfo(bank, owner, handle, info) < 0) return false;
    bool flip = false;
    while (info.queue_depth < target) {
        // Two interior points one pixel apart: every step is a real move and
        // stays inside the content, so it routes exactly one event.
        HoverStep(d, flip ? cx + 1 : cx, cy);
        flip = !flip;
        if (QueryInfo(bank, owner, handle, info) < 0) return false;
    }
    return info.queue_depth == target;
}

static void NativeWindowRedrawTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "redraw framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    // Start the tick counter low so every update below stays far from the
    // 50-tick monitor refresh.
    snapshot.ticks = 0;
    Pump(d);
    OneUpdate(d);

    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    int handle = bank.Create(1, create);
    Check(handle > 0, "redraw create");
    // CREATE alone must repaint; no input is injected anywhere in this test.
    OneUpdate(d);
    Check(d.Windows().Window(ModernNative0).open, "create opens the window");
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "redraw provider slot");
    ModernRect content = NativeContentOf(d, 0, view);
    Check(content.w > 0 && content.h > 0, "redraw content rectangle");
    const uint32_t emptyPixel = display[(content.y + 2) * 800 + content.x + 2];

    unsigned char chunk[GTOS_WINDOW_WRITE_LIMIT];
    GtosWindowSubmitRequest submit;
    submit.version = GTOS_WINDOW_ABI_VERSION;
    submit.handle = (unsigned)handle;
    submit.source = 0;
    submit.length = 64;
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;

    // First frame, colour A.
    for (unsigned i = 0; i < sizeof(chunk); i += 4) {
        chunk[i] = 0x00; chunk[i + 1] = 0xC0; chunk[i + 2] = 0x00; chunk[i + 3] = 0xFF;
    }
    for (unsigned offset = 0; offset < 256; offset += 64) {
        submit.offset = offset;
        Check(bank.Submit(1, submit, chunk) == 64, "redraw submit frame 1");
    }
    Check(bank.Present(1, control) == 0, "redraw present frame 1");
    OneUpdate(d);
    Check(bank.CopySlot(0, view, frame), "redraw slot after frame 1");
    const uint32_t firstPixel = display[(content.y + 2) * 800 + content.x + 2];
    Check(firstPixel != emptyPixel, "first frame repaints without input");
    Check(firstPixel == ExpectedNativePixel(frame, 0, 0), "first frame pixel is exact");
    const ModernRect boundsAfterFirst = d.Windows().Window(ModernNative0).bounds;

    // Second frame, different colour, same handle and rectangle.
    for (unsigned i = 0; i < sizeof(chunk); i += 4) {
        chunk[i] = 0xC0; chunk[i + 1] = 0x00; chunk[i + 2] = 0x00; chunk[i + 3] = 0xFF;
    }
    for (unsigned offset = 0; offset < 256; offset += 64) {
        submit.offset = offset;
        Check(bank.Submit(1, submit, chunk) == 64, "redraw submit frame 2");
    }
    Check(bank.Present(1, control) == 0, "redraw present frame 2");
    OneUpdate(d);
    Check(bank.CopySlot(0, view, frame), "redraw slot after frame 2");
    const uint32_t secondPixel = display[(content.y + 2) * 800 + content.x + 2];
    Check(secondPixel != firstPixel, "second frame repaints without input");
    Check(secondPixel == ExpectedNativePixel(frame, 0, 0), "second frame pixel is exact");
    // The rectangle is unchanged field by field against the value saved before
    // the second frame, not by a tautology.
    const ModernRect afterSecond = d.Windows().Window(ModernNative0).bounds;
    Check(afterSecond.x == boundsAfterFirst.x && afterSecond.y == boundsAfterFirst.y &&
              afterSecond.w == boundsAfterFirst.w && afterSecond.h == boundsAfterFirst.h,
          "second frame keeps the window rectangle");

    // Destroy removes the window and repaints the background.
    Check(bank.Close(1, control) == 0, "redraw destroy");
    OneUpdate(d);
    Check(!d.Windows().Window(ModernNative0).open, "destroy removes the window");
    Check(display[(content.y + 2) * 800 + content.x + 2] != secondPixel,
          "destroy repaints the desktop without input");
    bank.SetAvailable(false);
}

static void NativeWindowFocusBatchTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "focus framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);

    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int a = bank.Create(1, create);
    const int b = bank.Create(2, create);
    Check(a > 0 && b > 0, "focus creates two windows");
    Pump(d);
    Check(d.Windows().Window(ModernNative0).open && d.Windows().Window(ModernNative1).open,
          "both native windows opened");

    // Focus A by its title bar, then let A see a key press.
    Click(d, 212, 162);
    Check(d.Windows().Focused() == ModernNative0, "title click focuses A");
    Key(d, 'x');
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];
    uint32_t count = (uint32_t)DrainEvents(bank, 1, (uint32_t)a, events,
                                           GTOS_INPUT_READ_LIMIT, "focus read accepted");
    Check(count == 2 && events[0].type == GTOS_INPUT_KEY_DOWN &&
          events[1].type == GTOS_INPUT_KEY_UP, "A saw its key pair");

    // A batch that visits B and comes back to A without any update in between.
    // The pointer moves first, so no window is dragged while the button is held.
    QueueKeyDown(d, 'y');
    QueueMoveTo(d, 695, 180);
    QueueDown(d);
    QueueUp(d);
    QueueMoveTo(d, 212, 162);
    QueueDown(d);
    QueueUp(d);
    OneUpdate(d);
    Check(d.Windows().Focused() == ModernNative0, "batch ends focused on A");
    count = (uint32_t)DrainEvents(bank, 1, (uint32_t)a, events, GTOS_INPUT_READ_LIMIT,
                                  "focus batch read accepted");
    Check(count >= 1, "A received something after the batch");
    bool reset = false, staleDown = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (events[i].type == GTOS_INPUT_RESET) reset = true;
        if (events[i].type == GTOS_INPUT_KEY_DOWN && events[i].code == 'y') staleDown = true;
    }
    Check(reset, "A gets RESET after losing focus inside the batch");
    Check(!staleDown, "A does not keep a press from before the focus loss");
    bank.Close(1, (GtosWindowControlRequest){GTOS_WINDOW_ABI_VERSION, (unsigned)a});
    bank.Close(2, (GtosWindowControlRequest){GTOS_WINDOW_ABI_VERSION, (unsigned)b});
    bank.SetAvailable(false);
}

static void NativeWindowCaptureTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "capture framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int handle = bank.Create(1, create);
    Check(handle > 0, "capture create");
    Pump(d);
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "capture slot");
    const ModernRect content = NativeContentOf(d, 0, view);
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];

    // Start from an empty queue so the chain below is exactly this case's own.
    while (DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                       "capture pre-drain accepted") > 0) {}

    // The press really stays held across the PRESENT.
    BeginPress(d, content.x + content.w / 2, content.y + content.h / 2);
    // The approach hovers into the window, so the first batch is those moves and
    // ends with the down. Read it instead of discarding it.
    int count = DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                            "capture press read accepted");
    Check(count >= 1, "the press produced records");
    bool pressOk = count >= 1;
    for (int i = 0; i < count; ++i) {
        const bool last = i == count - 1;
        if (last) {
            if (events[i].type != GTOS_INPUT_POINTER_DOWN || events[i].code != 1)
                pressOk = false;
        } else if (events[i].type != GTOS_INPUT_POINTER_MOVE || events[i].code != 0) {
            pressOk = false;
        }
    }
    Check(pressOk, "the press batch is moves followed by one down carrying code 1");

    unsigned char chunk[GTOS_WINDOW_WRITE_LIMIT];
    for (unsigned i = 0; i < sizeof(chunk); i += 4) {
        chunk[i] = 0x10; chunk[i + 1] = 0x20; chunk[i + 2] = 0x30; chunk[i + 3] = 0xFF;
    }
    GtosWindowSubmitRequest submit;
    submit.version = GTOS_WINDOW_ABI_VERSION;
    submit.handle = (unsigned)handle;
    submit.source = 0;
    submit.length = 64;
    for (unsigned offset = 0; offset < 256; offset += 64) {
        submit.offset = offset;
        Check(bank.Submit(1, submit, chunk) == 64, "capture submit");
    }
    Check(bank.Present(1, control) == 0, "capture present");
    Pump(d);
    // Leave the window entirely, then release out there.
    EndPress(d, 5, 5);

    count = DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                        "capture release read accepted");
    Check(count >= 2, "the release batch has moves and one up");
    bool releaseOk = count >= 2;
    bool sawNegative = false;
    for (int i = 0; i < count; ++i) {
        const bool last = i == count - 1;
        if (last) {
            if (events[i].type != GTOS_INPUT_POINTER_UP || events[i].code != 1)
                releaseOk = false;
        } else {
            if (events[i].type != GTOS_INPUT_POINTER_MOVE || events[i].code != 0)
                releaseOk = false;
            if (events[i].x < 0 && events[i].y < 0) sawNegative = true;
        }
    }
    Check(releaseOk, "the release batch is captured moves followed by one up carrying code 1");
    Check(sawNegative,
          "a captured move outside the window reports signed negative coordinates");
    Check(DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                      "capture tail read") == 0, "nothing else follows the release");
    bank.Close(1, control);
    bank.SetAvailable(false);
}

static void NativeWindowOverflowRouteTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "overflow framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int handle = bank.Create(1, create);
    Check(handle > 0, "overflow create");
    Pump(d);
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "overflow slot");
    const ModernRect content = NativeContentOf(d, 0, view);

    // A held press, then fill to 125 proved by WINDOW_INFO, then two keys: the
    // first reaches exactly 126, the second is the first real overflow.
    BeginPress(d, content.x + content.w / 2, content.y + content.h / 2);
    Check(FillQueueTo(d, bank, 1, (unsigned)handle, content, 125),
          "queue reaches exactly 125 by captured moves");
    QueueKeyDown(d, 0x20);
    OneUpdate(d);
    GtosWindowInfoResponse info;
    Check(QueryInfo(bank, 1, (unsigned)handle, info) >= 0 && info.queue_depth == 126,
          "queue reaches exactly 126 with one key");
    QueueKeyDown(d, 0x21);
    OneUpdate(d);
    Check(QueryInfo(bank, 1, (unsigned)handle, info) >= 0 && info.queue_depth == 2,
          "key overflow leaves RESET plus the new key");
    // dropped counts every unread normal entry the overflow discarded, which was
    // exactly the full queue of 126.
    Check(info.dropped == GTOS_INPUT_NORMAL_ENTRIES,
          "key overflow discarded exactly the full queue");

    // Read the whole batch, not just its head: RESET first, then the new key.
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];
    int count = DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                            "key overflow read accepted");
    Check(count == 2, "key overflow batch has exactly two records");
    if (count == 2) {
        Check(events[0].type == GTOS_INPUT_RESET, "key overflow reports RESET first");
        Check(events[1].type == GTOS_INPUT_KEY_DOWN && events[1].code == 0x21,
              "the key that caused the overflow is still delivered");
    }
    // The reset cancelled the capture: the release outside is not delivered.
    EndPress(d, 5, 5);
    Check(DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                      "key overflow tail read") == 0,
          "key overflow cancelled the capture, so no outside release arrives");
    bank.Close(1, control);
    bank.SetAvailable(false);
}

// Hover alone reaches the boundary and its overflow cancels a capture that a
// different press established.
static void NativeWindowHoverOverflowTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "hover overflow framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int handle = bank.Create(1, create);
    Check(handle > 0, "hover overflow create");
    Pump(d);
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "hover overflow slot");
    const ModernRect content = NativeContentOf(d, 0, view);
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];

    BeginPress(d, content.x + content.w / 2, content.y + content.h / 2);
    Check(FillQueueTo(d, bank, 1, (unsigned)handle, content, 125),
          "hover path reaches 125");
    // Two more hovers: 126, then overflow.
    HoverStep(d, content.x + 1, content.y + 1);
    GtosWindowInfoResponse info;
    Check(QueryInfo(bank, 1, (unsigned)handle, info) >= 0 && info.queue_depth == 126,
          "hover reaches exactly 126");
    HoverStep(d, content.x + 2, content.y + 1);
    Check(QueryInfo(bank, 1, (unsigned)handle, info) >= 0 && info.queue_depth == 2,
          "hover overflow leaves RESET plus the new move");
    const int count = DrainEvents(bank, 1, (unsigned)handle, events,
                                  GTOS_INPUT_READ_LIMIT, "hover overflow read accepted");
    Check(count == 2 && events[0].type == GTOS_INPUT_RESET,
          "hover overflow reports RESET first");
    Check(count == 2 && events[1].type == GTOS_INPUT_POINTER_MOVE,
          "the hover that caused the overflow is still delivered");
    EndPress(d, 5, 5);
    Check(DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                      "hover overflow tail read") == 0,
          "hover overflow cancelled the capture");
    bank.Close(1, control);
    bank.SetAvailable(false);
}

// The same 126 -> overflow transition reached with the button UP, so no capture
// exists and the hover branch is the one under test. The captured-move variant
// above stays as it is; this group proves the hover path on its own.
static void NativeWindowHoverFreeOverflowTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "free hover overflow framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int handle = bank.Create(1, create);
    Check(handle > 0, "free hover overflow create");
    Pump(d);
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "free hover overflow slot");
    const ModernRect content = NativeContentOf(d, 0, view);
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];

    // Fill to exactly 126 with the button up. FillQueueTo only ever moves, so no
    // press happens anywhere in this group.
    Check(FillQueueTo(d, bank, 1, (unsigned)handle, content, 126),
          "free hover reaches exactly 126");
    // Focus must not move on a hover: the pointer is over the native window and
    // nothing else was pressed, so the focus is still whatever it was.
    const int32_t focusedBefore = d.Windows().Focused();
    Check(focusedBefore == ModernNative0, "free hover starts focused on the native window");

    // One more hover overflows: RESET plus this move, and nothing else.
    HoverStep(d, content.x + 1, content.y + 1);
    GtosWindowInfoResponse info;
    Check(QueryInfo(bank, 1, (unsigned)handle, info) >= 0 && info.queue_depth == 2,
          "free hover overflow leaves RESET plus the new move");
    Check(d.Windows().Focused() == focusedBefore, "free hover overflow never changes focus");
    const int count = DrainEvents(bank, 1, (unsigned)handle, events,
                                  GTOS_INPUT_READ_LIMIT, "free hover overflow read accepted");
    Check(count == 2 && events[0].type == GTOS_INPUT_RESET,
          "free hover overflow reports RESET first");
    Check(count == 2 && events[1].type == GTOS_INPUT_POINTER_MOVE,
          "the free hover that caused the overflow is still delivered");
    Check(count == 2 && events[1].code == 0,
          "a hover move carries no button number");
    Check(count == 2 && events[1].x >= 0 && events[1].y >= 0 &&
              events[1].x < (int)view.width && events[1].y < (int)view.height,
          "the free hover move is inside the client content");

    // With no capture held, moving the pointer away must deliver nothing at all:
    // that is the observable difference between this group and the captured one.
    HoverStep(d, 5, 5);
    Check(DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                      "free hover leaving the window read") == 0,
          "no capture means leaving the window delivers nothing");
    Check(QueryInfo(bank, 1, (unsigned)handle, info) >= 0 && info.queue_depth == 0,
          "the queue is empty after the free hover group");
    bank.Close(1, control);
    bank.SetAvailable(false);
}

// A new down that overflows still establishes a fresh capture, so the matching
// release outside the window is delivered.
static void NativeWindowDownOverflowTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "down overflow framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int handle = bank.Create(1, create);
    Check(handle > 0, "down overflow create");
    Pump(d);
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "down overflow slot");
    const ModernRect content = NativeContentOf(d, 0, view);
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];

    // No capture yet: fill to 126 by hover alone.
    Check(FillQueueTo(d, bank, 1, (unsigned)handle, content, 126),
          "queue reaches exactly 126 without a capture");
    // This press overflows on its own down and must still become a capture. The
    // pointer must not move first, or the hover would overflow instead.
    PressHere(d);
    GtosWindowInfoResponse info;
    Check(QueryInfo(bank, 1, (unsigned)handle, info) >= 0 && info.queue_depth == 2,
          "the overflowing down leaves RESET plus the down");
    int count = DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                            "down overflow read accepted");
    Check(count == 2 && events[0].type == GTOS_INPUT_RESET,
          "down overflow reports RESET first");
    Check(count == 2 && events[1].type == GTOS_INPUT_POINTER_DOWN,
          "the overflowing down is still delivered");
    // The capture was rebuilt after the reset, so the outside release arrives.
    EndPress(d, 5, 5);
    count = DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                        "down overflow release read");
    Check(count >= 2, "rebuilt capture delivers moves then a release");
    bool releaseOk = count >= 2;
    for (int i = 0; i < count; ++i) {
        const bool last = i == count - 1;
        if (last) {
            if (events[i].type != GTOS_INPUT_POINTER_UP || events[i].code != 1)
                releaseOk = false;
        } else if (events[i].type != GTOS_INPUT_POINTER_MOVE || events[i].code != 0) {
            releaseOk = false;
        }
    }
    Check(releaseOk, "rebuilt capture moves carry code 0 and the release code 1");
    bank.Close(1, control);
    bank.SetAvailable(false);
}

// A refusal (negative result) cancels the capture held for that window. The bank
// is put into closing through its own API so the refusal is real.
static void NativeWindowRefusedCaptureTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "refused capture framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int handle = bank.Create(1, create);
    Check(handle > 0, "refused capture create");
    Pump(d);
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "refused capture slot");
    const ModernRect content = NativeContentOf(d, 0, view);
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];

    BeginPress(d, content.x + content.w / 2, content.y + content.h / 2);
    // Read the press batch now, so what follows proves only the refusal's effect.
    int pressCount = DrainEvents(bank, 1, (unsigned)handle, events,
                                 GTOS_INPUT_READ_LIMIT, "refused capture press read");
    Check(pressCount >= 1 &&
              events[pressCount - 1].type == GTOS_INPUT_POINTER_DOWN,
          "the press that starts the capture is delivered");
    // The application closes the object while the press is still held: routing
    // now refuses, so the desktop must drop the capture.
    bank.RequestClose(1, (unsigned)handle);
    HoverStep(d, content.x + 1, content.y + 1);
    EndPress(d, 5, 5);
    const int count = DrainEvents(bank, 1, (unsigned)handle, events,
                                  GTOS_INPUT_READ_LIMIT, "refused capture read accepted");
    bool anyPointer = false;
    int closes = 0;
    for (int i = 0; i < count; ++i) {
        if (events[i].type == GTOS_INPUT_CLOSE)
            ++closes;
        else if (events[i].type != GTOS_INPUT_RESET)
            anyPointer = true;
    }
    Check(!anyPointer, "a refused route leaves no pointer records for this window");
    // The CLOSE that closing queued is the only thing waiting, and it survives.
    GtosWindowInfoResponse info;
    Check(QueryInfo(bank, 1, (unsigned)handle, info) >= 0 &&
              info.lifecycle == GTOS_WINDOW_CLOSING,
          "the window is still closing, not destroyed by the desktop");
    Check(closes == 1, "the pending CLOSE survives a refused route");
    Check(DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                      "refused capture tail read") == 0,
          "the CLOSE is delivered only once");
    bank.Close(1, control);
    bank.SetAvailable(false);
}

static void NativeWindowLifecycleTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "lifecycle framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    int handle = bank.Create(1, create);
    Check(handle > 0, "lifecycle create");
    Pump(d);
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;
    unsigned char chunk[GTOS_WINDOW_WRITE_LIMIT];
    for (unsigned i = 0; i < sizeof(chunk); i += 4) {
        chunk[i] = 0x30; chunk[i + 1] = 0x60; chunk[i + 2] = 0x90; chunk[i + 3] = 0xFF;
    }
    GtosWindowSubmitRequest submit;
    submit.version = GTOS_WINDOW_ABI_VERSION;
    submit.handle = (unsigned)handle;
    submit.source = 0;
    submit.length = 64;
    for (unsigned offset = 0; offset < 256; offset += 64) {
        submit.offset = offset;
        Check(bank.Submit(1, submit, chunk) == 64, "lifecycle submit");
    }
    Check(bank.Present(1, control) == 0, "lifecycle present");
    Pump(d);
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "lifecycle slot");
    const ModernRect content = NativeContentOf(d, 0, view);
    const uint32_t shown = display[(content.y + 2) * 800 + content.x + 2];

    // Request-close keeps the frame and yields exactly one CLOSE.
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];
    // Empty the queue first, so the CLOSE counted below is the only one possible.
    while (DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                       "lifecycle pre-drain accepted") > 0) {}
    Click(d, content.x + content.w / 2, content.y + 10);
    Check(d.Windows().Focused() == ModernNative0, "lifecycle focus");
    Key(d, 27);
    Check(d.Windows().Window(ModernNative0).open, "request-close keeps the window");
    Check(display[(content.y + 2) * 800 + content.x + 2] == shown,
          "request-close keeps the last frame pixels");
    uint32_t count = (uint32_t)DrainEvents(bank, 1, (unsigned)handle, events,
                                           GTOS_INPUT_READ_LIMIT, "lifecycle close read");
    uint32_t closes = 0;
    for (uint32_t i = 0; i < count; ++i) if (events[i].type == GTOS_INPUT_CLOSE) ++closes;
    Check(closes == 1, "request-close yields exactly one CLOSE");
    count = (uint32_t)DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                                  "lifecycle close tail read");
    Check(count == 0, "CLOSE is delivered only once");

    // Real destroy removes the window on the next update.
    Check(bank.Close(1, control) == 0, "lifecycle destroy");
    OneUpdate(d);
    Check(!d.Windows().Window(ModernNative0).open, "destroy removes the window");
    Check(display[(content.y + 2) * 800 + content.x + 2] != shown,
          "destroy repaints the desktop");

    // A new handle in the same slot must not inherit the old binding.
    int fresh = bank.Create(1, create);
    Check(fresh > handle, "reuse gives a new handle");
    Pump(d);
    Check(d.Windows().Window(ModernNative0).open, "reused slot opens a window");
    Check(bank.RouteInput((unsigned)handle, (GtosInputEvent){1, 0, 0, 0}) ==
          GTOS_WINDOW_ERR_BAD_HANDLE, "stale handle is rejected");
    Check(bank.ContainsWindow((unsigned)fresh), "new handle is live");
    bank.Close(1, (GtosWindowControlRequest){GTOS_WINDOW_ABI_VERSION, (unsigned)fresh});
    bank.SetAvailable(false);
}

static void NativeWindowIrqOverflowTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "irq overflow framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    const int handle = bank.Create(1, create);
    Check(handle > 0, "irq overflow create");
    Pump(d);
    // A second window on another owner is put into closing so its CLOSE is
    // waiting when the IRQ ring is lost.
    const int closing = bank.Create(2, create);
    Check(closing > 0, "irq overflow closing create");
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "irq overflow slot");
    const ModernRect content = NativeContentOf(d, 0, view);
    GtosInputEvent events[GTOS_INPUT_READ_LIMIT];
    while (DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                       "irq overflow pre-drain accepted") > 0) {}
    // A real held press, so the reset has a live capture to cancel.
    BeginPress(d, content.x + content.w / 2, content.y + content.h / 2);
    bank.RequestClose(2, (unsigned)closing);
    // Overflow the desktop's own IRQ ring: 127 usable slots, so 140 raw events.
    // This is the deferred path, not IRQ context.
    for (uint32_t i = 0; i < 140; ++i) QueueKeyDown(d, 'a');
    OneUpdate(d);
    int count = DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                            "irq overflow read accepted");
    bool reset = false;
    for (int i = 0; i < count; ++i)
        if (events[i].type == GTOS_INPUT_RESET) reset = true;
    Check(reset, "IRQ overflow resets a bound native window");
    while (DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                       "irq overflow drain accepted") > 0) {}
    // The capture was cancelled with the reset, so the release is not delivered.
    EndPress(d, 5, 5);
    count = DrainEvents(bank, 1, (unsigned)handle, events, GTOS_INPUT_READ_LIMIT,
                        "irq overflow tail read");
    Check(count == 0, "IRQ overflow cleared the native capture");
    // The closing window's CLOSE was not lost by the recovery.
    GtosWindowInfoResponse info;
    Check(QueryInfo(bank, 2, (unsigned)closing, info) >= 0 &&
              info.lifecycle == GTOS_WINDOW_CLOSING,
          "the closing window is still closing after the IRQ recovery");
    count = DrainEvents(bank, 2, (unsigned)closing, events, GTOS_INPUT_READ_LIMIT,
                        "irq overflow closing read");
    Check(count == 1 && events[0].type == GTOS_INPUT_CLOSE,
          "IRQ recovery keeps the pending CLOSE");
    bank.Close(1, (GtosWindowControlRequest){GTOS_WINDOW_ABI_VERSION, (unsigned)handle});
    bank.Close(2, (GtosWindowControlRequest){GTOS_WINDOW_ABI_VERSION, (unsigned)closing});
    bank.SetAvailable(false);
}

// The slot changes owner between two updates: the desktop never observes an
// empty slot. The new object must get its own frame, size and coordinates, and
// must not inherit the old object's drag, resize or capture.
static void NativeWindowHandleSwapTests() {
    process::NativeWindows &bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "swap framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    int first = bank.Create(1, create);
    Check(first > 0, "swap first create");
    Pump(d);
    const ModernRect defaultBounds = d.Windows().Window(ModernNative0).bounds;

    // Publish a frame for the first object, then start dragging its title bar.
    unsigned char chunk[GTOS_WINDOW_WRITE_LIMIT];
    for (unsigned i = 0; i < sizeof(chunk); i += 4) {
        chunk[i] = 0x00; chunk[i + 1] = 0x00; chunk[i + 2] = 0xFF; chunk[i + 3] = 0xFF;
    }
    GtosWindowSubmitRequest submit;
    submit.version = GTOS_WINDOW_ABI_VERSION;
    submit.handle = (unsigned)first;
    submit.source = 0;
    submit.length = 64;
    for (unsigned offset = 0; offset < 256; offset += 64) {
        submit.offset = offset;
        Check(bank.Submit(1, submit, chunk) == 64, "swap submit first");
    }
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)first;
    Check(bank.Present(1, control) == 0, "swap present first");
    Pump(d);

    BeginPress(d, defaultBounds.x + 60, defaultBounds.y + 10);
    HoverStep(d, defaultBounds.x + 20, defaultBounds.y + 40);
    const ModernRect dragged = d.Windows().Window(ModernNative0).bounds;
    Check(dragged.x != defaultBounds.x || dragged.y != defaultBounds.y,
          "the first object is being dragged");

    // Replace the owner with no update in between: the desktop never sees a gap.
    Check(bank.Close(1, control) == 0, "swap destroy first");
    int second = bank.Create(1, create);
    Check(second > first, "swap second handle is new");
    for (unsigned i = 0; i < sizeof(chunk); i += 4) {
        chunk[i] = 0x00; chunk[i + 1] = 0xFF; chunk[i + 2] = 0x00; chunk[i + 3] = 0xFF;
    }
    submit.handle = (unsigned)second;
    for (unsigned offset = 0; offset < 256; offset += 64) {
        submit.offset = offset;
        Check(bank.Submit(1, submit, chunk) == 64, "swap submit second");
    }
    control.handle = (unsigned)second;
    Check(bank.Present(1, control) == 0, "swap present second");
    OneUpdate(d);

    // The very first update must already carry the new object's frame and size.
    NativeWindowView view;
    NativeImageSnapshot frame;
    Check(bank.CopySlot(0, view, frame), "swap provider slot");
    Check(view.handle == (unsigned)second, "swap bound the new handle");
    Check(view.width == 8 && view.height == 8, "swap kept the new view size");
    const ModernRect content = NativeContentOf(d, 0, view);
    Check(content.w > 0 && content.h > 0, "swap produced a content rectangle");
    Check(display[(content.y + 2) * 800 + content.x + 2] == ExpectedNativePixel(frame, 0, 0),
          "swap draws the new frame on the first update");
    // The desktop window keeps the place the user put it; what must NOT carry
    // over is the live drag, which is proven by the next check.
    const ModernRect afterSwap = d.Windows().Window(ModernNative0).bounds;
    Check(afterSwap.x == dragged.x && afterSwap.y == dragged.y &&
              afterSwap.w == dragged.w && afterSwap.h == dragged.h,
          "the desktop window keeps its place across the object swap");
    // A stale handle is refused by the bank itself.
    Check(bank.RouteInput((unsigned)first, (GtosInputEvent){GTOS_INPUT_KEY_DOWN, 0x41, 0, 0}) ==
              GTOS_WINDOW_ERR_BAD_HANDLE,
          "swap rejects the stale handle");
    // And the drag is really cancelled: a further move does not move the window.
    HoverStep(d, afterSwap.x + 90, afterSwap.y + 70);
    const ModernRect afterMove = d.Windows().Window(ModernNative0).bounds;
    Check(afterMove.x == afterSwap.x && afterMove.y == afterSwap.y,
          "the cancelled drag does not act on the new object");
    EndPress(d, 5, 5);
    bank.Close(1, control);
    bank.SetAvailable(false);
}

static void NativeWindowTests() {
    // Real geometry/render/desktop plus the real bank as provider.
    process::NativeWindows& bank = process::NativeWindows::Instance();
    bank.SetAvailable(true);
    drivers::Framebuffer fb;
    Check(fb.Bind(Mode(800, 600, 3200), (uint8_t *)display, surface, 800 * 600),
          "native window framebuffer");
    ModernDesktop d(&fb, 0, 0, true, 0, &bank);
    pointerX = 770;
    pointerY = 16;
    Pump(d);
    Check(!d.Windows().Window(ModernNative0).open,
          "no native window before the application creates one");

    GtosWindowCreateRequest create;
    create.version = GTOS_WINDOW_ABI_VERSION;
    create.width = 8;
    create.height = 8;
    create.stride = 32;
    create.format = GTOS_WINDOW_FORMAT_RGBA8_PREMULTIPLIED;
    int handle = bank.Create(1, create);
    Check(handle > 0, "application creates a window");
    Pump(d);
    Check(d.Windows().Window(ModernNative0).open,
          "unpublished native object opens an empty window");
    const ModernRect bounds = d.Windows().Window(ModernNative0).bounds;
    Check(bounds.w > 0 && bounds.h > 0, "native window has a real rectangle");

    unsigned char chunk[GTOS_WINDOW_WRITE_LIMIT];
    for (unsigned i = 0; i < sizeof(chunk); i += 4) {
        chunk[i] = 0x20; chunk[i + 1] = 0x40; chunk[i + 2] = 0x60; chunk[i + 3] = 0xFF;
    }
    GtosWindowSubmitRequest submit;
    submit.version = GTOS_WINDOW_ABI_VERSION;
    submit.handle = (unsigned)handle;
    submit.source = 0;
    submit.length = 64;
    for (unsigned offset = 0; offset < 256; offset += 64) {
        submit.offset = offset;
        Check(bank.Submit(1, submit, chunk) == 64, "desktop submits a chunk");
    }
    GtosWindowControlRequest control;
    control.version = GTOS_WINDOW_ABI_VERSION;
    control.handle = (unsigned)handle;
    Check(bank.Present(1, control) == 0, "desktop presents a frame");
    Pump(d);
    Check(d.Windows().Window(ModernNative0).open, "publishing keeps the window open");
    Check(d.Windows().Window(ModernNative0).bounds.w == bounds.w,
          "publishing does not resize the window");

    // Desktop request-close keeps the window visible with its last frame. The
    // desktop path is the real one: click the window, then press Esc.
    Click(d, bounds.x + bounds.w / 2, bounds.y + 10);
    Check(d.Windows().Focused() == ModernNative0, "click focuses the native window");
    Key(d, 27);
    Check(d.Windows().Window(ModernNative0).open,
          "request-close keeps the window visible");
    Check(bank.ContainsWindow((unsigned)handle), "request-close does not destroy");
    Check(bank.Close(1, control) == 0, "application destroys the window");
    Pump(d);
    Check(!d.Windows().Window(ModernNative0).open,
          "destroyed native window is hidden on the next update");
    Check(!bank.ContainsWindow((unsigned)handle), "destroyed handle is gone");
    Check(!d.Windows().Window(ModernImage).open, "image path untouched");
    bank.SetAvailable(false);
}

// Native window groups, run together in the full pass and individually through
// the optional command-line selector so a cheap targeted run can name one.
struct NativeGroup {
    const char *name;
    void (*run)();
};
static const NativeGroup nativeGroups[] = {
    {"basic", NativeWindowTests},
    {"redraw", NativeWindowRedrawTests},
    {"focus", NativeWindowFocusBatchTests},
    {"capture", NativeWindowCaptureTests},
    {"overflow", NativeWindowOverflowRouteTests},
    {"hoveroverflow", NativeWindowHoverOverflowTests},
    {"hoverfree", NativeWindowHoverFreeOverflowTests},
    {"downoverflow", NativeWindowDownOverflowTests},
    {"refused", NativeWindowRefusedCaptureTests},
    {"lifecycle", NativeWindowLifecycleTests},
    {"irqoverflow", NativeWindowIrqOverflowTests},
    {"swap", NativeWindowHandleSwapTests},
};
#ifdef GTOS_DESKTOP_NATIVE_GROUP
static bool SameName(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}
#endif
static void RunNativeWindows() {
#ifdef GTOS_DESKTOP_NATIVE_GROUP
    // Cheap targeted run: -DGTOS_DESKTOP_NATIVE_GROUP=\"capture\" builds a binary
    // that exercises exactly one group.
    for (uint32_t i = 0; i < sizeof(nativeGroups) / sizeof(nativeGroups[0]); ++i)
        if (SameName(GTOS_DESKTOP_NATIVE_GROUP, nativeGroups[i].name)) {
            nativeGroups[i].run();
            Output("native window group passed\n");
            return;
        }
    Output("native window group name not found\n");
    Check(false, "targeted native window group exists");
    return;
#endif
    for (uint32_t i = 0; i < sizeof(nativeGroups) / sizeof(nativeGroups[0]); ++i)
        nativeGroups[i].run();
}

static void RunTests() {
    FramebufferTests();
    GeometryTests();
    DesktopTests();
    ApplicationTests();
    DiskReloadTests();
    LocalizationTests();
    MinimumMonitorTests();
    NativeImageTests();
    RunNativeWindows();
    Output("Desktop/framebuffer safety and interaction tests passed\n");
}
#ifdef GTOS_DESKTOP_SANITIZE
int main() {
    RunTests();
    return 0;
}
#else
extern "C" void _start() {
    // Static constructors are not run by the kernel entry, so any namespace-scope
    // object with a vtable must be initialised here before the tests use it.
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);
    for (void (**ctor)(void) = __init_array_start; ctor != __init_array_end; ++ctor)
        (*ctor)();
    RunTests();
    Exit(0);
}
#endif
