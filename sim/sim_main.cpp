// Headless captures of every badge page plus a scripted router run.
//   badge_sim --assets .cache/dev_assets --out .cache/sim
// Writes PPM frames (masked to the round glass) and router_trace.csv.
#include <M5GFX.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "ui_pages.h"
#include "ui_router.h"

namespace {

std::string g_out = ".";

bool load_layer(lgfx::LGFX_Sprite &dst, const std::string &path, int w, int h)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    dst.setColorDepth(16);
    if (!dst.createSprite(w, h)) return false;
    dst.fillSprite(TFT_BLACK);
    return dst.drawJpg(data.data(), data.size(), 0, 0, w, h);
}

void save(lgfx::LGFX_Sprite &canvas, const std::string &name, uint8_t brightness = 255)
{
    const int w = canvas.width(), h = canvas.height();
    std::string path = g_out + "/" + name + ".ppm";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row(w * 3);
    const float scale = brightness / 255.0f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = x + 0.5f - ui::kCenter, dy = y + 0.5f - ui::kCenter;
            const bool inside = dx * dx + dy * dy <= (float)ui::kRadius * ui::kRadius;
            auto c = canvas.readPixelRGB(x, y);
            row[x * 3 + 0] = inside ? (uint8_t)(c.R8() * scale) : 24;
            row[x * 3 + 1] = inside ? (uint8_t)(c.G8() * scale) : 28;
            row[x * 3 + 2] = inside ? (uint8_t)(c.B8() * scale) : 32;
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    printf("captured %s\n", path.c_str());
}

ui::Frame base_frame(ui::Page page)
{
    ui::Frame f{};
    f.page = page;
    f.slide = 1;
    f.slide_dir = 1;
    f.scan_enter = 1;
    f.countdown = 1;
    f.setup = {"M5Badge-1A2B", "k7mq3xpe", "WIFI:T:WPA;S:M5Badge-1A2B;P:k7mq3xpe;;", "192.168.4.1",
               ui::SetupStage::Waiting, 0.8f};
    return f;
}

const char *page_name(ui::Page p)
{
    switch (p) {
    case ui::Page::Card: return "card";
    case ui::Page::About: return "about";
    case ui::Page::Scan: return "scan";
    case ui::Page::Ambient: return "ambient";
    case ui::Page::Setup: return "setup";
    }
    return "?";
}

} // namespace

int main(int argc, char **argv)
{
    std::string assets = ".cache/dev_assets";
    int theme_id = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--assets")) assets = argv[i + 1];
        else if (!strcmp(argv[i], "--out")) g_out = argv[i + 1];
        else if (!strcmp(argv[i], "--theme")) theme_id = atoi(argv[i + 1]);
    }

    lgfx::LGFX_Sprite canvas, card, about, name;
    canvas.setColorDepth(16);
    canvas.createSprite(ui::kSize, ui::kSize);
    const bool has_card = load_layer(card, assets + "/card.jpg", ui::kSize, ui::kSize);
    const bool has_about = load_layer(about, assets + "/about.jpg", ui::kSize, ui::kSize);
    const bool has_name = load_layer(name, assets + "/name.jpg", 360, 96);
    printf("layers: card=%d about=%d name=%d\n", has_card, has_about, has_name);

    ui::Content content{};
    content.theme = theme_id == 1 ? ui::kThemeBrand : theme_id == 2 ? ui::kThemePaper : ui::kThemeOcean;
    content.card = has_card ? &card : nullptr;
    content.about = has_about ? &about : nullptr;
    content.name_strip = has_name ? &name : nullptr;
    content.name = "Alex Chen 陈";
    content.title = "硬件工程师";
    content.qr[0] = {"微信扫一扫", "https://u.wechat.com/EXAMPLEexample0123456789?s=2"};
    content.qr_count = 1;
    ui::Status status{82, 9.6f, false, true, 10, 24};

    ui::Renderer renderer;
    auto shot = [&](const ui::Frame &f, const char *file, const ui::Content &c, const ui::Status &s) {
        renderer.draw(canvas, f, c, s);
        const int bad = renderer.take_qr_mismatches();
        if (bad >= 0) printf("%s qr readback %s: %d modules differ\n", bad ? "FAIL" : "PASS", file, bad);
        printf("emission %s: %.3f\n", file, ui::frame_emission(canvas));
        save(canvas, file);
    };

    shot(base_frame(ui::Page::Card), "01_card", content, status);
    shot(base_frame(ui::Page::About), "02_about", content, status);
    {
        ui::Frame f = base_frame(ui::Page::About);
        f.sliding = true;
        f.from = ui::Page::Card;
        f.slide = 0.35f;
        shot(f, "03_slide_card_about", content, status);
    }
    {
        ui::Frame f = base_frame(ui::Page::Scan);
        f.scan_enter = 0.25f;
        shot(f, "04_scan_enter", content, status);
        f.scan_enter = 1;
        f.countdown = 0.62f;
        shot(f, "05_scan", content, status);
    }
    {
        ui::Content multi = content;
        multi.qr[1] = {"官网 m5stack.com", "https://m5stack.com"};
        multi.qr[2] = {"扫码保存联系方式", "MECARD:N:Alex Chen 陈;EMAIL:alex@example.com;ORG:Example;;"};
        multi.qr_count = 3;
        ui::Frame f = base_frame(ui::Page::Scan);
        f.qr_index = 2;
        f.countdown = 0.9f;
        shot(f, "06_scan_vcard", multi, status);
    }
    {
        ui::Frame f = base_frame(ui::Page::Ambient);
        f.shift_x = 8;
        f.shift_y = -8;
        ui::Status low{9, 1.1f, false, true, 16, 5};
        shot(f, "07_ambient_low_battery", content, low);
    }
    {
        ui::Frame f = base_frame(ui::Page::Setup);
        shot(f, "08_setup_waiting", content, status);
        f.setup.stage = ui::SetupStage::Connected;
        f.setup.remaining = 0.5f;
        shot(f, "09_setup_connected", content, status);
        f.setup.stage = ui::SetupStage::Saved;
        shot(f, "10_setup_saved", content, status);
    }
    {
        ui::Content bare = content;
        bare.card = bare.about = bare.name_strip = nullptr;
        ui::Status charging{64, -1, true, false, 0, 0};
        shot(base_frame(ui::Page::Card), "11_card_fallback", bare, charging);
    }
    {
        ui::Content paper = content;
        paper.theme = ui::kThemePaper;
        paper.card = nullptr;
        shot(base_frame(ui::Page::Card), "12_card_fallback_paper", paper, status);
    }

    // Scripted router run: verifies transitions, timeouts and the brightness envelope.
    ui::RouterConfig rc{};
    rc.ambient_after_ms = 45000;
    rc.scan_timeout_ms = 30000;
    rc.setup_timeout_ms = 300000;
    rc.brightness = 200;
    rc.ambient_brightness = 40;
    rc.about_enabled = true;
    rc.touch_enabled = true;
    rc.qr_count = 2;
    ui::Router router;
    router.begin(rc, false, 0);
    struct Step {
        uint32_t t;
        ui::Input in;
        const char *what;
    };
    const Step steps[] = {
        {1000, ui::Input::KeyB, "blue: card->about"},
        {1100, ui::Input::KeyB, "blue mid-slide: interrupt back to card"},
        {3000, ui::Input::KeyA, "yellow: open scan"},
        {5000, ui::Input::Tap, "tap: next qr"},
        {60000, ui::Input::KeyB, "blue: card->about"},
        {70000, ui::Input::KeyBLong, "long blue: setup"},
        {80000, ui::Input::KeyBLong, "long blue: leave setup"},
    };
    std::ofstream trace(g_out + "/router_trace.csv");
    trace << "time_ms,page,animating,brightness,qr_index,event\n";
    size_t next = 0;
    ui::Page last_page = router.page();
    int failures = 0;
    int dip_redraws = 0, scan_in_redraws = 0;
    for (uint32_t t = 0; t <= 400000; t += 20) {
        const char *event = "";
        if (next < sizeof(steps) / sizeof(steps[0]) && steps[next].t == t) {
            router.input(steps[next].in, t);
            event = steps[next].what;
            ++next;
        }
        const bool redraw = router.tick(t);
        if (redraw && t >= 35000 && t < 35400) ++dip_redraws;
        if (redraw && t >= 3000 && t < 3300) ++scan_in_redraws;
        ui::Frame f{};
        router.fill(f);
        if (*event || router.page() != last_page || (router.animating() && t % 100 == 0)) {
            trace << t << ',' << page_name(router.page()) << ',' << router.animating() << ','
                  << (int)router.brightness() << ',' << f.qr_index << ',' << event << '\n';
        }
        last_page = router.page();
        auto expect = [&](uint32_t at, ui::Page page, int brightness) {
            if (t != at) return;
            bool ok = router.page() == page && (brightness < 0 || router.brightness() == brightness);
            printf("%s t=%u expect %s/%d got %s/%d\n", ok ? "PASS" : "FAIL", at, page_name(page), brightness,
                   page_name(router.page()), router.brightness());
            failures += ok ? 0 : 1;
        };
        expect(2000, ui::Page::Card, 200);
        expect(4000, ui::Page::Scan, 255);
        expect(1600, ui::Page::Card, 200);    // interrupted slide settles back on the card
        expect(4000, ui::Page::Scan, 255);
        expect(34000, ui::Page::Scan, 255);   // the tap at 5 s restarted the 30 s timeout
        expect(36000, ui::Page::Card, 200);
        expect(59000, ui::Page::Card, 200);   // idle counts from the scan timeout, not the last key
        expect(61000, ui::Page::About, 200);
        expect(72000, ui::Page::Setup, 200);
        expect(81000, ui::Page::Card, 200);
        expect(124000, ui::Page::Card, 200);
        expect(127000, ui::Page::Ambient, 40); // 45 s after leaving setup
        expect(130000, ui::Page::Ambient, 40);
    }
    // A fade should cost one redraw at the page switch; the scan entry animates every frame.
    const bool budget_ok = dip_redraws <= 2 && scan_in_redraws >= 10;
    printf("%s redraws: scan-close fade=%d, scan-enter=%d\n", budget_ok ? "PASS" : "FAIL", dip_redraws,
           scan_in_redraws);
    failures += budget_ok ? 0 : 1;
    printf("router checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 2 : 0;
}
