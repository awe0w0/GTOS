// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/public/ozone_platform.h"

#include <process/native_window_adapter.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>

#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/base/ime/input_method_minimal.h"
#include "ui/display/display_list.h"
#include "ui/display/types/native_display_delegate.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/ozone/common/bitmap_cursor_factory.h"
#include "ui/ozone/common/stub_overlay_manager.h"
#include "ui/ozone/platform/gtos/surface_factory_gtos.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/platform_screen.h"
#include "ui/ozone/public/stub_input_controller.h"
#include "ui/ozone/public/system_input_injector.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/platform_window/stub/stub_window.h"

namespace ui {
namespace {

struct GtosDesktopState {
  gfx::AcceleratedWidget widget = gfx::kNullAcceleratedWidget;
  gfx::Rect bounds;
  gfx::Point cursor;
};

gfx::Rect ClientBounds(const gfx::Size& requested) {
  return gfx::Rect(std::clamp(requested.width(), 1,
                              static_cast<int>(GTOS_WINDOW_LARGE_WIDTH)),
                   std::clamp(requested.height(), 1,
                              static_cast<int>(GTOS_WINDOW_LARGE_HEIGHT)));
}

KeyboardCode DesktopKeyCode(unsigned character) {
  if (character >= 'a' && character <= 'z') {
    return static_cast<KeyboardCode>(VKEY_A + character - 'a');
  }
  if (character >= 'A' && character <= 'Z') {
    return static_cast<KeyboardCode>(VKEY_A + character - 'A');
  }
  if (character >= '0' && character <= '9') {
    return static_cast<KeyboardCode>(VKEY_0 + character - '0');
  }
  switch (character) {
    case ' ':
      return VKEY_SPACE;
    case '\t':
      return VKEY_TAB;
    case '\n':
    case '\r':
      return VKEY_RETURN;
    case '\b':
      return VKEY_BACK;
    case '-':
    case '_':
      return VKEY_OEM_MINUS;
    case '=':
    case '+':
      return VKEY_OEM_PLUS;
    case '[':
    case '{':
      return VKEY_OEM_4;
    case ']':
    case '}':
      return VKEY_OEM_6;
    case '\\':
    case '|':
      return VKEY_OEM_5;
    case ';':
    case ':':
      return VKEY_OEM_1;
    case '\'':
    case '"':
      return VKEY_OEM_7;
    case ',':
    case '<':
      return VKEY_OEM_COMMA;
    case '.':
    case '>':
      return VKEY_OEM_PERIOD;
    case '/':
    case '?':
      return VKEY_OEM_2;
    case '`':
    case '~':
      return VKEY_OEM_3;
    case '!':
      return VKEY_1;
    case '@':
      return VKEY_2;
    case '#':
      return VKEY_3;
    case '$':
      return VKEY_4;
    case '%':
      return VKEY_5;
    case '^':
      return VKEY_6;
    case '&':
      return VKEY_7;
    case '*':
      return VKEY_8;
    case '(':
      return VKEY_9;
    case ')':
      return VKEY_0;
    case 0x81:
      return VKEY_LEFT;
    case 0x82:
      return VKEY_RIGHT;
    case 0x83:
      return VKEY_UP;
    case 0x84:
      return VKEY_DOWN;
    default:
      return VKEY_UNKNOWN;
  }
}

class GtosWindow : public StubWindow {
 public:
  GtosWindow(PlatformWindowDelegate* delegate,
             const gfx::Size& requested,
             GtosDesktopState& desktop)
      : StubWindow(ClientBounds(requested)), desktop_(desktop) {
    CHECK_EQ(desktop_->widget, gfx::kNullAcceleratedWidget);
    const gfx::Rect bounds = ClientBounds(requested);
    const int handle = window_.CreateLarge(bounds.width(), bounds.height());
    CHECK_GT(handle, 0);
    desktop_->widget = static_cast<gfx::AcceleratedWidget>(handle);
    desktop_->bounds = bounds;
    InitDelegateWithWidget(delegate, desktop_->widget);
    input_.Start(FROM_HERE, base::Milliseconds(8), this, &GtosWindow::PollInput);
  }

  ~GtosWindow() override { ReleaseWindow(); }

  void Show(bool inactive) override {
    visible_ = true;
    if (!inactive) {
      delegate()->OnActivationChanged(true);
    }
  }
  void Hide() override {
    visible_ = false;
    delegate()->OnActivationChanged(false);
  }
  bool IsVisible() const override { return visible_ && window_.valid(); }

  void Close() override {
    if (ReleaseWindow()) {
      delegate()->OnClosed();
    }
  }

  void SetBoundsInPixels(const gfx::Rect& bounds) override {
    // Window ABI v1 fixes the client size for the lifetime of the handle.
    delegate()->OnBoundsChanged({false});
  }

 private:
  bool ReleaseWindow() {
    input_.Stop();
    if (released_) {
      return false;
    }
    released_ = true;
    if (window_.valid()) {
      CHECK_EQ(window_.Close(), 0);
    }
    desktop_->widget = gfx::kNullAcceleratedWidget;
    return true;
  }

  void DispatchKey(unsigned character, bool down) {
    const KeyboardCode key_code = DesktopKeyCode(character);
    if (key_code == VKEY_UNKNOWN) {
      return;
    }
    const DomCode code = UsLayoutKeyboardCodeToDomCode(key_code);
    constexpr std::string_view kShifted = "~!@#$%^&*()_+{}|:\"<>?";
    const bool shifted = (character >= 'A' && character <= 'Z') ||
                         (character < 128 && kShifted.contains(static_cast<char>(character)));
    int flags = shifted ? EF_SHIFT_DOWN : EF_NONE;
    DomKey key;
    KeyboardCode layout_code;
    if (!DomCodeToUsLayoutDomKey(code, flags, &key, &layout_code)) {
      return;
    }
    auto& pressed = keys_[key_code];
    if (pressed && down) {
      flags |= EF_IS_REPEAT;
    }
    pressed = down ? character : 0;
    KeyEvent event(down ? EventType::kKeyPressed : EventType::kKeyReleased,
                   layout_code, code, flags, key, base::TimeTicks::Now());
    delegate()->DispatchEvent(&event);
  }

  void DispatchPointer(const GtosInputEvent& input) {
    desktop_->cursor = gfx::Point(input.x, input.y);
    const gfx::PointF location(desktop_->cursor);
    EventType type = pointer_down_ ? EventType::kMouseDragged
                                  : EventType::kMouseMoved;
    int changed = EF_NONE;
    if (input.type == GTOS_INPUT_POINTER_DOWN) {
      pointer_down_ = true;
      type = EventType::kMousePressed;
      changed = EF_LEFT_MOUSE_BUTTON;
    } else if (input.type == GTOS_INPUT_POINTER_UP) {
      pointer_down_ = false;
      type = EventType::kMouseReleased;
      changed = EF_LEFT_MOUSE_BUTTON;
    }
    const int buttons = pointer_down_ ? EF_LEFT_MOUSE_BUTTON : EF_NONE;
    MouseEvent event(type, location, location, base::TimeTicks::Now(),
                     buttons | changed, changed);
    delegate()->DispatchEvent(&event);
  }

  void ResetInput() {
    const auto weak = weak_factory_.GetWeakPtr();
    for (unsigned character : keys_) {
      if (character) {
        DispatchKey(character, false);
        if (!weak || released_) {
          return;
        }
      }
    }
    if (pointer_down_) {
      DispatchPointer({GTOS_INPUT_POINTER_UP, 1,
                       desktop_->cursor.x(), desktop_->cursor.y()});
    }
  }

  void PollInput() {
    std::array<GtosInputEvent, GTOS_INPUT_READ_LIMIT> events;
    const int count = window_.ReadInput(events.data(), events.size());
    if (count == GTOS_WINDOW_ERR_BAD_HANDLE) {
      Close();
      return;
    }
    CHECK_GE(count, 0);
    const auto weak = weak_factory_.GetWeakPtr();
    for (const auto& event : base::span(events).first(
             static_cast<size_t>(count))) {
      switch (event.type) {
        case GTOS_INPUT_CLOSE:
          input_.Stop();
          delegate()->OnCloseRequest();
          return;
        case GTOS_INPUT_RESET:
          ResetInput();
          break;
        case GTOS_INPUT_KEY_DOWN:
        case GTOS_INPUT_KEY_UP:
          if (visible_) {
            DispatchKey(event.code, event.type == GTOS_INPUT_KEY_DOWN);
          }
          break;
        case GTOS_INPUT_POINTER_DOWN:
        case GTOS_INPUT_POINTER_UP:
        case GTOS_INPUT_POINTER_MOVE:
          if (visible_) {
            DispatchPointer(event);
          }
          break;
      }
      if (!weak || released_) {
        return;
      }
    }
  }

  const raw_ref<GtosDesktopState> desktop_;
  gtos::process::NativeWindowAdapter window_;
  std::array<unsigned, 256> keys_{};
  base::RepeatingTimer input_;
  bool pointer_down_ = false;
  bool visible_ = false;
  bool released_ = false;
  base::WeakPtrFactory<GtosWindow> weak_factory_{this};
};

class GtosScreen : public PlatformScreen {
 public:
  explicit GtosScreen(const GtosDesktopState& desktop) : desktop_(desktop) {
    const gfx::Rect bounds(GTOS_WINDOW_LARGE_WIDTH, GTOS_WINDOW_LARGE_HEIGHT);
    displays_.AddDisplay(display::Display(1, bounds),
                         display::DisplayList::Type::PRIMARY);
  }

  const std::vector<display::Display>& GetAllDisplays() const override {
    return displays_.displays();
  }
  display::Display GetPrimaryDisplay() const override {
    return *displays_.GetPrimaryDisplayIterator();
  }
  display::Display GetDisplayForAcceleratedWidget(
      gfx::AcceleratedWidget widget) const override {
    return GetPrimaryDisplay();
  }
  gfx::Point GetCursorScreenPoint() const override { return desktop_->cursor; }
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point) const override {
    return desktop_->bounds.Contains(point) ? desktop_->widget
                                            : gfx::kNullAcceleratedWidget;
  }
  display::Display GetDisplayNearestPoint(
      const gfx::Point& point) const override {
    return GetPrimaryDisplay();
  }
  display::Display GetDisplayMatching(const gfx::Rect& rect) const override {
    return GetPrimaryDisplay();
  }
  void AddObserver(display::DisplayObserver* observer) override {
    displays_.AddObserver(observer);
  }
  void RemoveObserver(display::DisplayObserver* observer) override {
    displays_.RemoveObserver(observer);
  }

 private:
  const raw_ref<const GtosDesktopState> desktop_;
  display::DisplayList displays_;
};

class GtosEventSource : public PlatformEventSource {};

class OzonePlatformGtos : public OzonePlatform {
 public:
  SurfaceFactoryOzone* GetSurfaceFactoryOzone() override { return &factory_; }
  OverlayManagerOzone* GetOverlayManager() override { return &overlays_; }
  CursorFactory* GetCursorFactory() override { return &cursors_; }
  InputController* GetInputController() override { return &input_; }
  GpuPlatformSupportHost* GetGpuPlatformSupportHost() override {
    return gpu_support_.get();
  }
  std::unique_ptr<SystemInputInjector> CreateSystemInputInjector() override {
    return nullptr;
  }
  std::unique_ptr<PlatformWindow> CreatePlatformWindow(
      PlatformWindowDelegate* delegate,
      PlatformWindowInitProperties properties) override {
    return std::make_unique<GtosWindow>(delegate, properties.bounds.size(),
                                        desktop_);
  }
  std::unique_ptr<display::NativeDisplayDelegate> CreateNativeDisplayDelegate()
      override {
    return nullptr;
  }
  std::unique_ptr<PlatformScreen> CreateScreen() override {
    return std::make_unique<GtosScreen>(desktop_);
  }
  void InitScreen(PlatformScreen* screen) override {}
  std::unique_ptr<InputMethod> CreateInputMethod(
      ImeKeyEventDispatcher* dispatcher,
      gfx::AcceleratedWidget widget) override {
    return std::make_unique<InputMethodMinimal>(dispatcher);
  }
  bool IsWindowCompositingSupported() const override { return true; }

  bool InitializeUI(const InitParams& params) override {
    // A window handle belongs to the native process, including its Viz threads.
    if (!params.single_process) {
      return false;
    }
    if (!PlatformEventSource::GetInstance()) {
      event_source_ = std::make_unique<GtosEventSource>();
    }
    gpu_support_.reset(CreateStubGpuPlatformSupportHost());
    return true;
  }
  void InitializeGPU(const InitParams& params) override {
    CHECK(params.single_process);
  }

 private:
  GtosDesktopState desktop_;
  SurfaceFactoryGtos factory_;
  StubOverlayManager overlays_;
  BitmapCursorFactory cursors_;
  StubInputController input_;
  std::unique_ptr<GtosEventSource> event_source_;
  std::unique_ptr<GpuPlatformSupportHost> gpu_support_;
};

}  // namespace

OzonePlatform* CreateOzonePlatformGtos() {
  return new OzonePlatformGtos();
}

}  // namespace ui
