// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/shell_platform_delegate.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "components/url_formatter/url_fixer.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/shell/browser/shell.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/event_target.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/controls/webview/web_contents_set_background_color.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/test/desktop_test_views_delegate.h"
#include "ui/views/widget/desktop_aura/desktop_screen.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/wm/core/wm_state.h"
#include "url/third_party/mozilla/url_parse.h"
#include "url/url_constants.h"

namespace content {
namespace {

class GtosBrowserViewsDelegate : public views::DesktopTestViewsDelegate {
 public:
  void OnBeforeWidgetInit(
      views::Widget::InitParams* params,
      views::internal::NativeWidgetDelegate* delegate) override {
    using InitParams = views::Widget::InitParams;
    if (!params->native_widget && (params->context || params->parent) &&
        (params->type == InitParams::TYPE_TOOLTIP ||
         params->type == InitParams::TYPE_MENU ||
         params->type == InitParams::TYPE_POPUP)) {
      // Browser-owned transient widgets live in the existing Aura host.
      params->native_widget = new views::NativeWidgetAura(delegate);
    }
    DesktopTestViewsDelegate::OnBeforeWidgetInit(params, delegate);
  }
};

// Each Shell owns a WebContents. A single widget owns the tab strip and the
// WebView used to display the selected WebContents.
class BrowserView : public views::BoxLayoutView,
                    public views::TextfieldController,
                    public views::WidgetObserver {
 public:
  BrowserView() {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetBackground(views::CreateSolidBackground(ui::kColorWindowBackground));

    if (!Shell::ShouldHideToolbar()) {
      auto* header = AddChildView(std::make_unique<views::BoxLayoutView>());
      header->SetBetweenChildSpacing(3);
      auto* scroll = header->AddChildView(std::make_unique<views::ScrollView>());
      scroll->SetHorizontalScrollBarMode(
          views::ScrollView::ScrollBarMode::kEnabled);
      scroll->SetVerticalScrollBarMode(
          views::ScrollView::ScrollBarMode::kDisabled);
      scroll->ClipHeightTo(40, 40);
      header->SetFlexForView(scroll, 1);
      auto tabs = std::make_unique<views::BoxLayoutView>();
      tabs->SetBetweenChildSpacing(2);
      tabs_view_ = scroll->SetContents(std::move(tabs));
      AddButton(header, u"+", u"New tab (Ctrl+T)",
                base::BindRepeating(&BrowserView::NewTab,
                                    base::Unretained(this)));

      auto* toolbar = AddChildView(std::make_unique<views::BoxLayoutView>());
      toolbar->SetBetweenChildSpacing(2);
      back_ = AddButton(toolbar, u"Back", u"Back (Alt+Left)",
                        base::BindRepeating(&BrowserView::Go,
                                            base::Unretained(this), -1));
      forward_ = AddButton(toolbar, u"Next", u"Forward (Alt+Right)",
                           base::BindRepeating(&BrowserView::Go,
                                               base::Unretained(this), 1));
      AddButton(toolbar, u"Reload", u"Reload (Ctrl+R)",
                base::BindRepeating(&BrowserView::Reload,
                                    base::Unretained(this)));
      stop_ = AddButton(toolbar, u"Stop", u"Stop loading",
                        base::BindRepeating(&BrowserView::Stop,
                                            base::Unretained(this)));
      address_ = toolbar->AddChildView(std::make_unique<views::Textfield>());
      address_->SetAccessibleName(u"Address");
      address_->SetTextInputType(ui::TEXT_INPUT_TYPE_URL);
      address_->set_controller(this);
      toolbar->SetFlexForView(address_, 1);
    }

    page_ = AddChildView(std::make_unique<views::WebView>());
    page_->set_allow_accelerators(true);
    SetFlexForView(page_, 1);
  }

  void AddTab(Shell* shell, bool foreground) {
    Tab tab{shell};
    if (tabs_view_) {
      tab.controls =
          tabs_view_->AddChildView(std::make_unique<views::BoxLayoutView>());
      tab.select = AddButton(tab.controls, u"New tab", u"Switch to tab",
                             base::BindRepeating(&BrowserView::Select,
                                                 base::Unretained(this), shell));
      tab.select->SetPreferredSize(gfx::Size(108, 28));
      auto* close = AddButton(
          tab.controls, u"x", u"Close tab",
          base::BindRepeating(&BrowserView::CloseTab, base::Unretained(this),
                              shell));
      close->SetPreferredSize(gfx::Size(28, 28));
      close->SetCustomPadding(gfx::Insets::VH(0, 4));
    }
    tabs_.push_back(tab);
    UpdateTabTitle(shell);
    if (foreground || !active_) {
      Select(shell);
    } else {
      shell->web_contents()->WasHidden();
    }
  }

  void RemoveTab(Shell* shell) {
    auto tab = FindTab(shell);
    if (tab == tabs_.end()) {
      return;
    }
    const size_t index = tab - tabs_.begin();
    if (active_ == shell) {
      page_->SetWebContents(nullptr);
      active_ = nullptr;
    }
    if (!closing_ && tab->controls) {
      tab->select = nullptr;
      tabs_view_->RemoveChildViewT(tab->controls.ExtractAsDangling().get());
    }
    tabs_.erase(tab);
    if (!closing_ && !active_ && !tabs_.empty()) {
      // Closing the selected tab prefers its right neighbor.
      Select(tabs_[std::min(index, tabs_.size() - 1)].shell);
    }
  }

  void UpdateTabTitle(Shell* shell) {
    auto tab = FindTab(shell);
    if (tab == tabs_.end()) {
      return;
    }
    WebContents* contents = shell->web_contents();
    std::u16string title = contents->GetTitle();
    if (title.empty()) {
      title = base::UTF8ToUTF16(contents->GetVisibleURL().host());
    }
    if (title.empty()) {
      title = u"New tab";
    }
    if (tab->select) {
      tab->select->SetText(title);
      tab->select->SetTooltipText(title);
    }
    if (active_ == shell) {
      GetWidget()->widget_delegate()->SetTitle(title);
    }
  }

  void UpdateLocation(Shell* shell) {
    if (shell && shell == active_ && address_ && !editing_address_) {
      address_->SetText(
          base::UTF8ToUTF16(shell->web_contents()->GetVisibleURL().spec()));
    }
  }

  void UpdateNavigation(Shell* shell) {
    if (shell == active_ && address_) {
      auto& controller = shell->web_contents()->GetController();
      back_->SetEnabled(controller.CanGoBack());
      forward_->SetEnabled(controller.CanGoForward());
      stop_->SetEnabled(shell->web_contents()->IsLoading());
    }
  }

  bool closing() const { return closing_; }

 private:
  class ShortcutHandler final : public ui::EventHandler {
   public:
    explicit ShortcutHandler(BrowserView* browser) : browser_(browser) {}

    void OnKeyEvent(ui::KeyEvent* event) override {
      if (!browser_->closing_ && event->type() == ui::EventType::kKeyPressed &&
          browser_->RunShortcut(*event)) {
        event->StopPropagation();
      }
    }

   private:
    const raw_ptr<BrowserView> browser_;
  };

  struct Tab {
    raw_ptr<Shell> shell;
    raw_ptr<views::BoxLayoutView> controls = nullptr;
    raw_ptr<views::MdTextButton> select = nullptr;
  };

  std::vector<Tab>::iterator FindTab(Shell* shell) {
    return std::ranges::find_if(
        tabs_, [shell](const Tab& tab) { return tab.shell == shell; });
  }

  static views::MdTextButton* AddButton(views::BoxLayoutView* parent,
                                       std::u16string_view label,
                                       const std::u16string& accessible_name,
                                       base::RepeatingClosure action) {
    auto* button = parent->AddChildView(
        std::make_unique<views::MdTextButton>(std::move(action), label));
    button->SetMinSize(gfx::Size(24, 26));
    button->SetAccessibleName(accessible_name);
    button->SetTooltipText(accessible_name);
    return button;
  }

  void Select(Shell* shell) {
    auto next = FindTab(shell);
    if (closing_ || next == tabs_.end()) {
      return;
    }
    if (shell != active_) {
      if (active_) {
        active_->web_contents()->WasHidden();
      }
      active_ = shell;
      WebContents* contents = shell->web_contents();
      page_->SetWebContents(contents);
      contents->WasShown();
      for (const auto& tab : tabs_) {
        if (tab.select) {
          tab.select->SetStyle(tab.shell == shell
                                   ? ui::ButtonStyle::kProminent
                                   : ui::ButtonStyle::kDefault);
        }
      }
      if (next->controls) {
        next->controls->ScrollViewToVisible();
      }
      editing_address_ = false;
      UpdateLocation(shell);
      UpdateNavigation(shell);
      UpdateTabTitle(shell);
    }
    page_->RequestFocus();
  }

  void SelectRelative(int offset) {
    auto current = FindTab(active_);
    if (current == tabs_.end()) {
      return;
    }
    const size_t index = current - tabs_.begin();
    const size_t count = tabs_.size();
    Select(tabs_[(index + count + offset) % count].shell);
  }

  void CloseTab(Shell* shell) {
    shell->web_contents()->ClosePage();
  }

  void NewTab() {
    if (!active_) {
      return;
    }
    Shell::CreateNewWindow(active_->web_contents()->GetBrowserContext(),
                           GURL(url::kAboutBlankURL), nullptr, gfx::Size());
    FocusAddress();
  }

  void FocusAddress() {
    if (address_) {
      address_->RequestFocus();
      address_->SelectAll(false);
    }
  }

  void Go(int offset) {
    if (active_ &&
        active_->web_contents()->GetController().CanGoToOffset(offset)) {
      active_->GoBackOrForward(offset);
    }
  }

  void Reload() {
    if (active_) {
      active_->Reload();
    }
  }

  void Stop() {
    if (active_) {
      active_->Stop();
    }
  }

  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& contents) override {
    editing_address_ = true;
  }

  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed) {
      return false;
    }
    if (event.key_code() == ui::VKEY_ESCAPE) {
      editing_address_ = false;
      UpdateLocation(active_);
      return true;
    }
    if (event.key_code() != ui::VKEY_RETURN || !active_) {
      return false;
    }
    const auto text = base::UTF16ToUTF8(sender->GetText());
    url::Parsed parts;
    url_formatter::SegmentURL(text, &parts);
    GURL url = url_formatter::FixupURL(text);
    if (!parts.scheme.is_valid() && url.SchemeIs(url::kHttpScheme) &&
        !url.HostIsIPAddress() && !url.DomainIs("localhost") && !url.has_port()) {
      GURL::Replacements replacements;
      replacements.SetSchemeStr(url::kHttpsScheme);
      url = url.ReplaceComponents(replacements);
    }
    if (url.is_valid()) {
      editing_address_ = false;
      active_->LoadURL(url);
      page_->RequestFocus();
    }
    return true;
  }

  void AddedToWidget() override {
    widget_observation_.Observe(GetWidget());
    // Native WebContents children can own the Aura focus.
    input_root_ = GetWidget()->GetNativeWindow()->GetRootWindow();
    input_root_->AddPreTargetHandler(&shortcut_handler_,
                                     ui::EventTarget::Priority::kSystem);
  }

  bool RunShortcut(const ui::KeyEvent& key) {
    constexpr int kModifiers = ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN |
                               ui::EF_ALT_DOWN | ui::EF_ALTGR_DOWN |
                               ui::EF_COMMAND_DOWN;
    const int modifiers = key.flags() & kModifiers;
    const ui::KeyboardCode code = key.key_code();
    if (modifiers == ui::EF_NONE && code == ui::VKEY_F5) {
      Reload();
      return true;
    }
    if (modifiers == ui::EF_ALT_DOWN &&
        (code == ui::VKEY_LEFT || code == ui::VKEY_RIGHT)) {
      Go(code == ui::VKEY_LEFT ? -1 : 1);
      return true;
    }
    if (!address_ ||
        (modifiers != ui::EF_CONTROL_DOWN &&
         !(code == ui::VKEY_TAB &&
           modifiers == (ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN)))) {
      return false;
    }
    switch (code) {
      case ui::VKEY_T:
        NewTab();
        return true;
      case ui::VKEY_W:
        if (active_) {
          CloseTab(active_);
        }
        return true;
      case ui::VKEY_TAB:
        SelectRelative(key.IsShiftDown() ? -1 : 1);
        return true;
      case ui::VKEY_L:
        FocusAddress();
        return true;
      case ui::VKEY_R:
        Reload();
        return true;
      case ui::VKEY_9:
        if (!tabs_.empty()) {
          Select(tabs_.back().shell);
        }
        return true;
      default: {
        if (code < ui::VKEY_1 || code > ui::VKEY_8) {
          return false;
        }
        const size_t index = code - ui::VKEY_1;
        if (index < tabs_.size()) {
          Select(tabs_[index].shell);
        }
        return true;
      }
    }
  }

  void OnWidgetDestroying(views::Widget*) override {
    widget_observation_.Reset();
    input_root_->RemovePreTargetHandler(&shortcut_handler_);
    input_root_ = nullptr;
    closing_ = true;
    page_->SetWebContents(nullptr);
    active_ = nullptr;
    while (!tabs_.empty()) {
      tabs_.back().shell->Close();
    }
  }

  std::vector<Tab> tabs_;
  raw_ptr<Shell> active_ = nullptr;
  raw_ptr<views::BoxLayoutView> tabs_view_ = nullptr;
  raw_ptr<views::Textfield> address_ = nullptr;
  raw_ptr<views::MdTextButton> back_ = nullptr;
  raw_ptr<views::MdTextButton> forward_ = nullptr;
  raw_ptr<views::MdTextButton> stop_ = nullptr;
  raw_ptr<views::WebView> page_ = nullptr;
  raw_ptr<aura::Window> input_root_ = nullptr;
  bool editing_address_ = false;
  bool closing_ = false;
  ShortcutHandler shortcut_handler_{this};
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace

struct ShellPlatformDelegate::ShellData {};

struct ShellPlatformDelegate::PlatformData {
  std::unique_ptr<wm::WMState> wm_state;
  std::unique_ptr<display::Screen> screen;
  std::unique_ptr<views::ViewsDelegate> views_delegate;
  raw_ptr<BrowserView> browser = nullptr;
};

ShellPlatformDelegate::ShellPlatformDelegate() = default;
ShellPlatformDelegate::~ShellPlatformDelegate() = default;

void ShellPlatformDelegate::Initialize(const gfx::Size&) {
  platform_ = std::make_unique<PlatformData>();
  platform_->wm_state = std::make_unique<wm::WMState>();
  if (!display::Screen::HasScreen()) {
    platform_->screen = views::CreateDesktopScreen();
  }
  platform_->views_delegate = std::make_unique<GtosBrowserViewsDelegate>();
}

void ShellPlatformDelegate::CreatePlatformWindow(Shell* shell,
                                                 const gfx::Size& size) {
  shell_data_map_.try_emplace(shell);
  if (platform_->browser) {
    return;
  }
  auto delegate = std::make_unique<views::WidgetDelegate>();
  auto browser = std::make_unique<BrowserView>();
  platform_->browser = browser.get();
  delegate->SetContentsView(std::move(browser));
  delegate->SetHasWindowSizeControls(true);
  delegate->SetOwnedByWidget(views::WidgetDelegate::OwnedByWidgetPassKey());
  auto* widget = new views::Widget();
  views::Widget::InitParams params(
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  params.bounds = gfx::Rect(size);
  params.delegate = delegate.release();
  widget->Init(std::move(params));
}

gfx::NativeWindow ShellPlatformDelegate::GetNativeWindow(Shell*) {
  return platform_->browser->GetWidget()->GetNativeWindow();
}

void ShellPlatformDelegate::CleanUp(Shell* shell) {
  shell_data_map_.erase(shell);
  BrowserView* browser = platform_->browser;
  browser->RemoveTab(shell);
  if (shell_data_map_.empty()) {
    platform_->browser = nullptr;
    if (!browser->closing()) {
      browser->GetWidget()->CloseNow();
    }
  }
}

void ShellPlatformDelegate::SetContents(Shell* shell) {
  SetTabContents(shell, true);
}

void ShellPlatformDelegate::SetTabContents(Shell* shell, bool foreground) {
  auto* widget = platform_->browser->GetWidget();
  views::WebContentsSetBackgroundColor::CreateForWebContentsWithColor(
      shell->web_contents(),
      widget->GetColorProvider()->GetColor(ui::kColorWindowBackground));
  if (!widget->IsVisible()) {
    widget->GetNativeWindow()->GetHost()->Show();
    widget->Show();
  }
  platform_->browser->AddTab(shell, foreground);
}

void ShellPlatformDelegate::ResizeWebContent(Shell* shell,
                                             const gfx::Size& size) {
  shell->web_contents()->Resize(gfx::Rect(size));
}

void ShellPlatformDelegate::EnableUIControl(Shell* shell, UIControl, bool) {
  platform_->browser->UpdateNavigation(shell);
}

void ShellPlatformDelegate::SetAddressBarURL(Shell* shell, const GURL&) {
  auto* browser = platform_->browser.get();
  browser->UpdateLocation(shell);
  browser->UpdateNavigation(shell);
  // A committed history entry can change without a new TitleWasSet event.
  browser->UpdateTabTitle(shell);
}

void ShellPlatformDelegate::SetIsLoading(Shell* shell, bool is_loading) {
  auto* browser = platform_->browser.get();
  browser->UpdateNavigation(shell);
  if (!is_loading) {
    // An untitled document can finish without a TitleWasSet notification.
    browser->UpdateTabTitle(shell);
  }
}

void ShellPlatformDelegate::SetTitle(Shell* shell, const std::u16string&) {
  platform_->browser->UpdateTabTitle(shell);
}

void ShellPlatformDelegate::MainFrameCreated(Shell*, RenderFrameHost*) {}

bool ShellPlatformDelegate::DestroyShell(Shell*) {
  return false;
}

}  // namespace content
