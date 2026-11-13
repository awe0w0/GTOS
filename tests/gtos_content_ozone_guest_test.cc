// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <process/abi.h>
#include <process/window_abi.h>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_pixmap_handle.h"
#include "ui/ozone/public/client_native_pixmap_factory_ozone.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/surface_factory_ozone.h"
#include "ui/ozone/public/surface_ozone_canvas.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_delegate.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace {

const gfx::Rect kMarker(48, 48, 48, 48);

template <unsigned Size>
void Say(const char (&message)[Size]) {
    unsigned operation = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(message), "c"(Size - 1) : "cc", "memory");
}

struct ExitMarker {
    ~ExitMarker() { Say("GTOS CRT DESTRUCTOR\n"); }
};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
ExitMarker exit_marker;
#pragma clang diagnostic pop

class WindowDelegate final : public ui::PlatformWindowDelegate {
public:
    explicit WindowDelegate(base::RepeatingClosure quit) : quit_(std::move(quit)) {}

    bool Begin(ui::SurfaceFactoryOzone& factory) {
        if (widget_ == gfx::kNullAcceleratedWidget) return false;
        surface_ = factory.CreateCanvasForWidget(widget_);
        if (!surface_) return false;
        surface_->ResizeCanvas(gfx::Size(GTOS_WINDOW_LARGE_WIDTH, GTOS_WINDOW_LARGE_HEIGHT), 1);
        if (!surface_->GetCanvas()) return false;
        surface_->GetCanvas()->clear(SkColorSetRGB(32, 32, 32));
        Paint(SkColorSetRGB(165, 43, 199));
        return true;
    }

    int result() const { return result_; }
    bool active() const { return active_; }
    bool closed() const { return closed_; }

    void OnBoundsChanged(const BoundsChange&) override {}
    void OnDamageRect(const gfx::Rect&) override {}
    void OnWindowStateChanged(ui::PlatformWindowState, ui::PlatformWindowState) override {}
    void OnLostCapture() override {}
    void OnCursorUpdate() override {}
    void OnAcceleratedWidgetAvailable(gfx::AcceleratedWidget widget) override { widget_ = widget; }
    void OnWillDestroyAcceleratedWidget() override {}
    void OnAcceleratedWidgetDestroyed() override { widget_ = gfx::kNullAcceleratedWidget; }
    void OnActivationChanged(bool active) override { active_ = active; }
    void OnCloseRequest() override { Finish(235); }
    void OnClosed() override { closed_ = true; }

    void DispatchEvent(ui::Event* event) override {
        if (result_ != 234) return;
        if (event->IsMouseEvent()) {
            const auto& mouse = *event->AsMouseEvent();
            if (mouse.type() == ui::EventType::kMouseMoved) {
                if (!reported_motion_) {
                    Say("GTOS OZONE INPUT MOTION\n");
                    reported_motion_ = true;
                }
                motion_ = mouse.location();
                return;
            }
            if (mouse.type() == ui::EventType::kMousePressed)
                Say("GTOS OZONE INPUT PRESS\n");
            if (mouse.type() == ui::EventType::kMouseReleased)
                Say("GTOS OZONE INPUT RELEASE\n");
            if (phase_ != 0 || !mouse.IsLeftMouseButton()) return;
            if (mouse.type() == ui::EventType::kMousePressed) {
                if (!kMarker.Contains(mouse.location()) || motion_ != mouse.location()) {
                    Finish(220);
                    return;
                }
                pressed_ = true;
                return;
            }
            if (mouse.type() == ui::EventType::kMouseReleased) {
                if (!pressed_ || !kMarker.Contains(mouse.location())) {
                    Finish(220);
                    return;
                }
                ++phase_;
                Paint(SkColorSetRGB(18, 168, 109));
            }
            return;
        }
        if (!event->IsKeyEvent() || !phase_) return;
        const auto& key = *event->AsKeyEvent();
        const bool select = phase_ == 1 && key.code() == ui::DomCode::US_G && key.GetCharacter() == u'g';
        const bool close = phase_ == 2 && key.code() == ui::DomCode::US_Q && key.GetCharacter() == u'q';
        if (!select && !close) return;
        if (key.type() == ui::EventType::kKeyPressed) {
            key_pressed_ = true;
        } else if (key.type() == ui::EventType::kKeyReleased && key_pressed_) {
            key_pressed_ = false;
            if (select) {
                ++phase_;
                Paint(SkColorSetRGB(244, 175, 34));
            } else {
                Finish(37);
            }
        }
    }

private:
    void Paint(SkColor color) {
        SkPaint paint;
        paint.setColor(color);
        surface_->GetCanvas()->drawRect(SkRect::MakeXYWH(48, 48, 48, 48), paint);
        surface_->PresentCanvas(gfx::Rect(GTOS_WINDOW_LARGE_WIDTH, GTOS_WINDOW_LARGE_HEIGHT));
    }

    void Finish(int result) {
        if (result_ == 234) {
            result_ = result;
            quit_.Run();
        }
    }

    base::RepeatingClosure quit_;
    std::unique_ptr<ui::SurfaceOzoneCanvas> surface_;
    gfx::AcceleratedWidget widget_ = gfx::kNullAcceleratedWidget;
    gfx::Point motion_;
    int result_ = 234;
    unsigned phase_ = 0;
    bool active_ = false;
    bool pressed_ = false;
    bool reported_motion_ = false;
    bool key_pressed_ = false;
    bool closed_ = false;
};

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
    base::AtExitManager at_exit;
    base::CommandLine::Init(argc, argv);
    base::SingleThreadTaskExecutor executor(base::MessagePumpType::UI);
    ui::OzonePlatform::InitParams params;
    if (ui::OzonePlatform::InitializeForUI(params)) return 230;
    params.single_process = true;
    if (!ui::OzonePlatform::InitializeForUI(params)) return 231;
    auto& platform = *ui::OzonePlatform::GetInstance();
    auto pixmaps = ui::CreateClientNativePixmapFactoryOzone();
    if (!pixmaps || pixmaps->ImportFromHandle(
            {}, gfx::Size(1, 1), viz::SinglePlaneFormat::kRGBA_8888,
            gfx::BufferUsage::GPU_READ)) return 237;
    base::RunLoop run;
    WindowDelegate delegate(run.QuitClosure());
    ui::PlatformWindowInitProperties properties;
    properties.bounds = gfx::Rect(800, 600);
    auto window = platform.CreatePlatformWindow(&delegate, std::move(properties));
    if (!window || window->GetBoundsInPixels().size() !=
            gfx::Size(GTOS_WINDOW_LARGE_WIDTH, GTOS_WINDOW_LARGE_HEIGHT)) return 232;
    window->Show(false);
    if (!delegate.active() || !window->IsVisible() ||
        !delegate.Begin(*platform.GetSurfaceFactoryOzone())) return 233;
    base::OneShotTimer tick, timeout;
    tick.Start(FROM_HERE, base::Milliseconds(20),
               base::BindOnce([] { Say("GTOS OZONE UI TIMER\n"); }));
    timeout.Start(FROM_HERE, base::Seconds(50), run.QuitClosure());
    Say("GTOS OZONE RUN LOOP ENTER\n");
    run.Run();
    Say("GTOS OZONE RUN LOOP RETURN\n");
    window->Close();
    if (!delegate.closed()) return 236;
    if (delegate.result() == 37) Say("GTOS CONTENT OZONE PASS\n");
    return delegate.result();
}
