// Copyright 2026 The Chromium Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <process/abi.h>

#include "base/at_exit.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "skia/ext/font_utils.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkFont.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkString.h"
#include "third_party/skia/include/core/SkTypeface.h"

namespace {

template <unsigned Size>
void Say(const char (&message)[Size]) {
    unsigned operation = GTOS_SYS_WRITE;
    asm volatile("int $0x80" : "+a"(operation)
                 : "b"(message), "c"(Size - 1) : "cc", "memory");
}

}  // namespace

extern "C" int GtosContentShellMain(int argc, const char** argv) {
    base::AtExitManager at_exit;
    base::CommandLine::Init(argc, argv);

    base::FilePath assets;
    if (!base::PathService::Get(base::DIR_ASSETS, &assets)) return 210;
    const bool has_font = base::PathExists(assets.AppendASCII("Roboto-Regular.ttf"));
    auto manager = skia::DefaultFontMgr();
    if (!manager || manager->matchFamilyStyle("GTOS absent font family", SkFontStyle())) return 211;
    auto roboto = manager->matchFamilyStyle("Roboto", SkFontStyle());
    auto face = manager->legacyMakeTypeface(nullptr, SkFontStyle());
    if (!face) return 212;
    if (!has_font) {
        if (roboto || face->unicharToGlyph('A') != 0) return 213;
        Say("GTOS CONTENT FONT ABSENT FALLBACK\n");
        return 45;
    }
    if (!roboto || manager->countFamilies() != 1 || face->unicharToGlyph('A') == 0)
        return 214;
    SkString family;
    face->getFamilyName(&family);
    if (!family.equals("Roboto")) return 215;

    constexpr int kWidth = 64;
    constexpr int kHeight = 40;
    SkBitmap bitmap;
    if (!bitmap.tryAllocN32Pixels(kWidth, kHeight)) return 216;
    SkCanvas canvas(bitmap);
    canvas.clear(SK_ColorWHITE);
    SkFont font(face, 24);
    font.setEdging(SkFont::Edging::kAlias);
    SkPaint paint;
    paint.setColor(SK_ColorBLACK);
    canvas.drawSimpleText("A", 1, SkTextEncoding::kUTF8, 6, 30, font, paint);

    unsigned ink = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (bitmap.getColor(x, y) == SK_ColorBLACK) {
                if (x < 5 || x > 30 || y < 4 || y >= 31) return 217;
                ++ink;
            } else if (bitmap.getColor(x, y) != SK_ColorWHITE) {
                return 218;
            }
        }
    }
    if (ink < 30 || ink > 300) return 219;
    Say("GTOS CONTENT FONT ROBOTO GLYPH PASS\n");
    return 37;
}
