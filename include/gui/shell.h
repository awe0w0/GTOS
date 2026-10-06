#ifndef GTOS_GUI_SHELL_H
#define GTOS_GUI_SHELL_H
#include <gui/canvas.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <apps/vm.h>
#include <storage/appstore.h>
namespace gtos {
namespace gui {
struct SystemSnapshot {
    uint32_t ramMiB, freePages, heapKiB, heapUsedKiB, logicalCPUs, onlineCPUs;
    uint32_t ticks, taskCount, contextSwitches, diskSectors, parkedAPs;
    bool pagingEnabled, writeProtectEnabled;
    bool memoryOK, schedulerOK, diskOK;
    char vendor[13];
};
class DesktopShell : public drivers::KeyboardEventHandler,
                     public drivers::MouseEventHandler,
                     public apps::Host {
    struct Event {
        uint8_t type, code;
        int16_t x, y;
    };
    Canvas canvas, gameCanvas;
    bool keyDown[256];
    volatile bool inputOverflow;
    storage::AppStore *store;
    apps::VirtualMachine vm;
    const uint8_t *installer;
    uint32_t installerSize;
    uint8_t package[apps::PackageLimit];
    volatile uint32_t readEvent, writeEvent;
    Event events[64];
    volatile int32_t mouseX, mouseY;
    uint32_t heldKeys, page, selected, lastFrame;
    bool needsDraw, gameOpen;
    const char *notice;
    SystemSnapshot state;
    void Queue(uint8_t type, uint8_t code);
    void ProcessKey(uint8_t code, bool down);
    void Click(int32_t x, int32_t y);
    void Draw();
    void DrawHome();
    void DrawHardware();
    void DrawApps();
    void Button(int32_t x, int32_t y, int32_t w, const char *label, uint8_t color);
    void Install();
    void Remove();
    void Launch();

  public:
    explicit DesktopShell(storage::AppStore *store);
    void SetInstaller(const uint8_t *data, uint32_t length);
    void Update(const SystemSnapshot &snapshot);
    virtual void OnKeyDown(char c);
    virtual void OnKeyUp(char c);
    virtual void OnMouseMove(int8_t x, int8_t y);
    virtual void OnMouseDown(uint8_t button);
    virtual void Clear(uint8_t color);
    virtual void Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color);
    virtual void Text(int32_t x, int32_t y, const char *text, uint8_t color);
    virtual void Number(int32_t x, int32_t y, int32_t value, uint8_t color);
};
} // namespace gui
} // namespace gtos
#endif
