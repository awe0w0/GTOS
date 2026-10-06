#ifndef GTOS_GUI_MODERN_DESKTOP_H
#define GTOS_GUI_MODERN_DESKTOP_H
#include <gui/modern_geometry.h>
#include <gui/modern_painter.h>
#include <gui/shell.h>
namespace gtos {
namespace gui {
class ModernDesktop : public drivers::KeyboardEventHandler,
                      public drivers::MouseEventHandler,
                      public apps::Host {
    struct Input {
        uint8_t type, code;
        int16_t x, y;
    };
    struct Theme {
        uint32_t panel, raised, text, muted, line, accent, accentText;
    };
    drivers::Framebuffer &fb;
    ModernPainter paint;
    ModernWindowManager wm;
    storage::AppStore *store;
    apps::VirtualMachine vm;
    SystemSnapshot state;
    const uint8_t *installer;
    uint32_t installerSize;
    uint8_t package[apps::PackageLimit],
        game[apps::VirtualMachine::Width * apps::VirtualMachine::Height];
    Input events[128];
    volatile uint32_t eventRead, eventWrite;
    volatile bool overflow;
    volatile int32_t mouseX, mouseY;
    bool keys[256], leftDown, needsDraw, lightTheme, launcher, confirmRemove;
    int32_t dragKind, dragX, dragY, dragWidth, dragHeight;
    bool resizing;
    uint32_t selected, lastFrame, lastStep, lastMonitor, noticeAt, lastTitleClick;
    int32_t titleClickKind, launcherSelected;
    char query[32];
    uint32_t queryLength;
    const char *notice;
    Theme Colors() const;
    void Queue(uint8_t type, uint8_t code);
    void CancelCapture();
    void Key(uint8_t code, bool down);
    void Pointer(const Input &input);
    void Open(ModernWindowKind kind);
    void Close(ModernWindowKind kind);
    void Install();
    void Remove();
    void Launch();
    void Notice(const char *text);
    uint32_t LauncherItems(uint8_t *out) const;
    void LauncherActivate(uint8_t item);
    void Draw();
    void DrawWindow(ModernWindowKind kind);
    void DrawWelcome(const ModernRect &r);
    void DrawApplications(const ModernRect &r);
    void DrawMonitor(const ModernRect &r);
    void DrawSettings(const ModernRect &r);
    void DrawGame(const ModernRect &r);
    void DrawLauncher();
    void DrawModal();
    void Button(int32_t x, int32_t y, int32_t w, const char *text, bool primary = false,
                bool danger = false);
    uint32_t HeldKeys() const;

  public:
    ModernDesktop(drivers::Framebuffer *framebuffer, storage::AppStore *store);
    void SetInstaller(const uint8_t *data, uint32_t length);
    void Update(const SystemSnapshot &snapshot);
    virtual void OnKeyDown(char c);
    virtual void OnKeyUp(char c);
    virtual void OnMouseMove(int8_t x, int8_t y);
    virtual void OnMouseDown(uint8_t button);
    virtual void OnMouseUp(uint8_t button);
    virtual void Clear(uint8_t color);
    virtual void Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color);
    virtual void Text(int32_t x, int32_t y, const char *text, uint8_t color);
    virtual void Number(int32_t x, int32_t y, int32_t value, uint8_t color);
    // Read-only state supports deterministic window/input regression tests.
    const ModernWindowManager &Windows() const { return wm; }
    bool LauncherOpen() const { return launcher; }
    bool RemovalPending() const { return confirmRemove; }
    bool LightTheme() const { return lightTheme; }
    const char *ActiveApplicationID() const {
        return wm.Window(ModernGame).open ? vm.Info().id : 0;
    }
};
} // namespace gui
} // namespace gtos
#endif
