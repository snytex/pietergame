#include "game/ModeArt.hpp"

#include "engine/Engine.hpp"
#include "engine/Fx.hpp"
#include "engine/Ui.hpp"
#include "game/Art.hpp"
#include "game/Profile.hpp"

#include <rlgl.h>

namespace pg::art {

void rocket(Vector2 c, float s, float rotDeg, float flame, float t) {
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    rlRotatef(rotDeg, 0, 0, 1);
    // flame
    if (flame > 0) {
        fx::beginAdd();
        for (int i = 0; i < 4; i++) {
            float k = std::fmod(t * 6 + i * 0.25f, 1.0f);
            fx::glow({-s * (0.9f + k * 1.4f), std::sin(t * 30 + i) * s * 0.08f}, s * (0.5f - k * 0.3f), alpha(mix(col::gold, col::red, k), flame * 0.8f));
        }
        fx::endAdd();
        DrawTriangle({-s * 0.75f, -s * 0.18f}, {-s * (1.3f + std::sin(t * 40) * 0.12f) * flame - s * 0.75f, 0}, {-s * 0.75f, s * 0.18f}, col::orange);
        DrawTriangle({-s * 0.75f, -s * 0.18f}, {-s * 0.75f, s * 0.18f}, {-s * (1.3f + std::sin(t * 40) * 0.12f) * flame - s * 0.75f, 0}, col::orange);
        DrawTriangle({-s * 0.75f, -s * 0.1f}, {-s * 0.75f - s * 0.6f * flame, 0}, {-s * 0.75f, s * 0.1f}, rgba(0xfff3a0ff));
        DrawTriangle({-s * 0.75f, -s * 0.1f}, {-s * 0.75f, s * 0.1f}, {-s * 0.75f - s * 0.6f * flame, 0}, rgba(0xfff3a0ff));
    }
    // fins
    for (int k : {-1, 1}) {
        Vector2 a{-s * 0.55f, k * s * 0.22f}, b{-s * 0.85f, k * s * 0.55f}, d{-s * 0.2f, k * s * 0.22f};
        DrawTriangle(a, k > 0 ? b : d, k > 0 ? d : b, col::outline);
        DrawTriangle(Vector2Lerp(a, d, 0.1f), k > 0 ? Vector2Lerp(b, a, 0.12f) : Vector2Lerp(d, a, 0.0f), k > 0 ? Vector2Lerp(d, a, 0.0f) : Vector2Lerp(b, a, 0.12f), col::red);
    }
    // body: a boba cup turned into a rocket
    DrawRectangleRounded({-s * 0.8f - 2.5f, -s * 0.28f - 2.5f, s * 1.5f + 5, s * 0.56f + 5}, 1, 12, col::outline);
    fx::gradRoundRect({-s * 0.8f, -s * 0.28f, s * 1.5f, s * 0.56f}, 1, rgba(0xf4f0ffff), rgba(0xb8b0d0ff));
    DrawTriangle({s * 0.55f, -s * 0.26f}, {s * 1.05f, 0}, {s * 0.55f, s * 0.26f}, col::red);
    DrawTriangle({s * 0.55f, -s * 0.26f}, {s * 0.55f, s * 0.26f}, {s * 1.05f, 0}, col::red);
    DrawRectangleRec({-s * 0.35f, -s * 0.27f, s * 0.12f, s * 0.54f}, col::violet);
    // window with Pieter
    DrawCircleV({s * 0.22f, 0}, s * 0.2f, col::outline);
    DrawCircleV({s * 0.22f, 0}, s * 0.16f, rgba(0x8fd0ffff));
    rlPopMatrix();
    // the face stays upright
    Vector2 wc = c + Vector2Rotate({s * 0.22f, 0}, rotDeg * DEG2RAD);
    portrait("pieter", wc, s * 0.15f, BLANK);
}

void bike(Vector2 wb, float s, float pedal, Color frame) {
    // Dutch omafiets: big wheels, upright frame, basket at the front
    Vector2 wf{wb.x + s * 1.05f, wb.y};
    auto wheel = [&](Vector2 c) {
        DrawRing(c, s * 0.36f, s * 0.42f, 0, 360, 36, col::outline);
        DrawRing(c, s * 0.37f, s * 0.41f, 0, 360, 36, rgba(0x2a2534ff));
        for (int i = 0; i < 8; i++) {
            Vector2 d = fromAngle(pedal * 2 + i * PI / 4, s * 0.36f);
            DrawLineEx(c, c + d, 1.2f, alpha(rgba(0xc8c0d8ff), 0.7f));
        }
        DrawCircleV(c, s * 0.05f, rgba(0xc8c0d8ff));
    };
    wheel(wb);
    wheel(wf);
    Vector2 crank{wb.x + s * 0.45f, wb.y - s * 0.02f}, seat{wb.x + s * 0.3f, wb.y - s * 0.62f}, head{wf.x - s * 0.12f, wb.y - s * 0.72f};
    float w = std::max(2.0f, s * 0.07f);
    for (int pass = 0; pass < 2; pass++) {
        Color c = pass == 0 ? col::outline : frame;
        float ww = pass == 0 ? w + 3 : w;
        DrawLineEx(wb, crank, ww, c);
        DrawLineEx(wb, seat, ww, c);
        DrawLineEx(seat, crank, ww, c);
        DrawLineEx(crank, head, ww, c);
        DrawLineEx({seat.x, seat.y + s * 0.1f}, {head.x, head.y + s * 0.2f}, ww, c);
        DrawLineEx(head, wf, ww, c);
    }
    // chain guard, saddle, handlebar, basket
    DrawRectangleRounded({wb.x - s * 0.02f, wb.y - s * 0.08f, s * 0.5f, s * 0.12f}, 1, 6, darken(frame, 0.3f));
    DrawRectangleRounded({seat.x - s * 0.14f, seat.y - s * 0.08f, s * 0.28f, s * 0.1f}, 1, 6, rgba(0x3a2418ff));
    DrawLineEx(head, {head.x - s * 0.18f, head.y - s * 0.1f}, w, rgba(0xc8c0d8ff));
    Rectangle basket{head.x + s * 0.02f, head.y - s * 0.02f, s * 0.36f, s * 0.24f};
    DrawRectangleRounded(basket, 0.2f, 6, col::outline);
    DrawRectangleRounded(shrink(basket, 2), 0.2f, 6, rgba(0xb8864aff));
    for (int i = 1; i < 4; i++) DrawLineEx({basket.x + i * basket.width / 4, basket.y + 2}, {basket.x + i * basket.width / 4, basket.y + basket.height - 2}, 1.2f, rgba(0x7a5428ff));
    // pedals
    Vector2 pd = fromAngle(pedal, s * 0.16f);
    DrawLineEx(crank - pd, crank + pd, w, rgba(0xc8c0d8ff));
    DrawCircleV(crank, s * 0.07f, rgba(0x6a6278ff));
}

namespace {

void notes(Vector2 c, float s, float t, Color k) {
    for (int i = 0; i < 3; i++) {
        float y = std::fmod(t * 30 + i * 30, 90.0f);
        Vector2 p{c.x + (i - 1) * s * 0.5f, c.y - y * s / 60};
        Color cc = alpha(k, 1 - y / 90);
        DrawEllipse((int)p.x, (int)p.y, s * 0.12f, s * 0.09f, cc);
        DrawLineEx({p.x + s * 0.11f, p.y}, {p.x + s * 0.11f, p.y - s * 0.4f}, 3, cc);
    }
}

}  // namespace

void modeArt(const std::string& id, Rectangle r, float t, float hover) {
    Vector2 c = center(r);
    float s = std::min(r.width, r.height);
    float bob = std::sin(t * 2.2f) * 4 * (0.4f + hover);
    if (id == "classic") {
        for (int i = 0; i < 6; i++) {
            Vector2 p = c + fromAngle(t * 0.8f + i * PI / 3, s * 0.36f);
            fx::beginAdd();
            fx::glow(p, 18, alpha(i % 2 ? col::pink : col::orange, 0.6f));
            fx::endAdd();
            DrawCircleV(p, 7, i % 2 ? col::pink : col::orange);
        }
        fx::beginAdd();
        fx::glow(c, s * 0.5f, alpha(col::violet, 0.5f));
        fx::endAdd();
        pieter({c.x, c.y + bob}, s * 0.22f, P().hat);
        ui::heart({c.x + s * 0.3f, c.y - s * 0.3f}, 26, col::red);
    } else if (id == "room") {
        DrawRectangleRounded({c.x - s * 0.42f, c.y - s * 0.32f, s * 0.84f, s * 0.6f}, 0.1f, 8, rgba(0x3d2676ff));
        DrawRectangleRec({c.x - s * 0.42f, c.y + s * 0.12f, s * 0.84f, s * 0.16f}, rgba(0x5e3a24ff));
        DrawRectangleRec({c.x + s * 0.12f, c.y - s * 0.26f, s * 0.22f, s * 0.18f}, rgba(0x6ec6ffff));
        fx::beginAdd();
        fx::glow({c.x - s * 0.22f, c.y - s * 0.2f}, s * 0.25f, alpha(rgba(0xff3d6aff), 0.5f));
        fx::endAdd();
        ui::text("BEC IT", c.x - s * 0.22f, c.y - s * 0.2f, s * 0.08f, rgba(0xff8aa0ff), ui::Center, true);
        pieter({c.x, c.y + s * 0.02f + bob}, s * 0.2f, P().hat);
    } else if (id == "beats") {
        float rot = t * (1.5f + hover * 3);
        DrawCircleV(c, s * 0.38f, rgba(0x0e0b14ff));
        for (float k = 0.45f; k < 0.97f; k += 0.08f) DrawRing(c, s * 0.38f * k, s * 0.38f * k + 1, 0, 360, 48, alpha(WHITE, 0.06f));
        DrawRing(c, s * 0.15f, s * 0.36f, rot * RAD2DEG, rot * RAD2DEG + 30, 16, alpha(WHITE, 0.08f));
        DrawCircleV(c, s * 0.15f, col::pink);
        portrait("pieter", c, s * 0.12f, BLANK, rot * RAD2DEG);
        notes({c.x + s * 0.3f, c.y - s * 0.1f}, s * 0.6f, t, col::cyan);
    } else if (id == "horses") {
        horse({c.x - s * 0.1f, c.y + s * 0.36f}, s / 330.0f, t * (8 + hover * 8), rgba(0x8b5a2bff), rgba(0x3a2010ff), col::violet, "pieter");
    } else if (id == "stroop") {
        float cw = s * 0.62f;
        DrawRectangleRounded({c.x - cw / 2, c.y + s * 0.08f, cw, s * 0.3f}, 0.3f, 8, col::outline);
        DrawRectangleRounded({c.x - cw / 2 + 3, c.y + s * 0.08f + 3, cw - 6, s * 0.3f - 6}, 0.3f, 8, rgba(0xfdfbffff));
        DrawRectangleRec({c.x - cw / 2 + 3, c.y + s * 0.18f, cw - 6, s * 0.06f}, col::purple);
        for (int i = 0; i < 4; i++) {
            float off = std::sin(t * 2 + i) * 4 * (i == 3 ? 1 + hover * 3 : 0.4f);
            stroopwafel({c.x + off, c.y + s * 0.03f - i * s * 0.09f + (i == 3 ? bob - 10 : 0)}, cw * (0.95f - i * 0.04f), s * 0.085f);
        }
    } else if (id == "job") {
        // briefcase + a tie on Pieter
        Rectangle bc{c.x - s * 0.36f, c.y - s * 0.05f, s * 0.72f, s * 0.42f};
        DrawRectangleRounded({bc.x + s * 0.24f, bc.y - s * 0.1f, s * 0.24f, s * 0.14f}, 0.4f, 6, col::outline);
        DrawRectangleRounded({bc.x + s * 0.27f, bc.y - s * 0.07f, s * 0.18f, s * 0.1f}, 0.4f, 6, rgba(0x3a2418ff));
        DrawRectangleRounded(shrink(bc, -3), 0.15f, 8, col::outline);
        fx::gradRoundRect(bc, 0.15f, rgba(0x8a5a32ff), rgba(0x5a3418ff));
        DrawRectangleRec({bc.x, bc.y + bc.height * 0.35f, bc.width, 4}, rgba(0x3a2010ff));
        DrawRectangleRounded({c.x - 10, bc.y + bc.height * 0.3f, 20, 14}, 0.3f, 4, col::gold);
        pieter({c.x, c.y - s * 0.22f + bob}, s * 0.17f, "");
        ui::textShadow("GET A JOB", c.x, bc.y + bc.height * 0.72f, s * 0.09f, col::gold, ui::Center, true, 2);
    } else if (id == "fiets") {
        for (int i = 0; i < 4; i++) {
            Texture2D& h = E().tex(TextFormat("spr/house%d", i));
            float span = r.width - 40;
            float x = r.x + 20 + std::fmod(std::fmod(i * 70 - t * 20, span) + span, span);
            fx::sprite(h, {x, c.y - s * 0.02f}, s / 900);
        }
        DrawRectangleGradientV((int)r.x, (int)(c.y + s * 0.18f), (int)r.width, (int)(r.y + r.height - c.y - s * 0.18f), rgba(0x2a5aa8ff), rgba(0x183a78ff));
        for (int i = 0; i < 6; i++) {
            float x = r.x + std::fmod(std::fmod(i * 60 - t * 80, r.width - 20) + r.width - 20, r.width - 20);
            DrawLineEx({x, c.y + s * 0.3f}, {x + 18, c.y + s * 0.3f}, 2, alpha(WHITE, 0.4f));
        }
        float ph = t * (8 + hover * 10);
        float by = c.y + s * 0.16f + std::abs(std::sin(ph)) * -2;
        bike({c.x - s * 0.32f, by}, s * 0.55f, ph, rgba(0x1a1a24ff));
        pieter({c.x - s * 0.05f, by - s * 0.55f}, s * 0.13f, P().hat);
    } else if (id == "casino") {
        const Color cc[] = {col::red, rgba(0x2a9a5aff), rgba(0x1d1a24ff), col::violet};
        for (int i = 0; i < 4; i++)
            for (int k = 0; k < 5; k++) {
                Vector2 p{c.x - s * 0.3f + i * s * 0.2f, c.y + s * 0.25f - k * 7.0f - (i % 2) * 10};
                DrawEllipse((int)p.x, (int)p.y + 3, s * 0.09f, s * 0.035f, darken(cc[i], 0.4f));
                DrawEllipse((int)p.x, (int)p.y, s * 0.09f, s * 0.035f, cc[i]);
            }
        card({c.x - s * 0.3f, c.y - s * 0.32f, s * 0.26f, s * 0.36f}, 1, 0, true);
        card({c.x + s * 0.02f, c.y - s * 0.36f + bob, s * 0.26f, s * 0.36f}, 13, 1, true);
    } else if (id == "fnap") {
        DrawCircleV(c, s * 0.4f, rgba(0x0a0612ff));
        fx::beginAdd();
        fx::glow({c.x - s * 0.1f, c.y - s * 0.05f}, s * 0.12f, alpha(col::red, 0.9f));
        fx::glow({c.x + s * 0.1f, c.y - s * 0.05f}, s * 0.12f, alpha(col::red, 0.9f));
        fx::endAdd();
        DrawCircleV({c.x - s * 0.1f, c.y - s * 0.05f}, 3, WHITE);
        DrawCircleV({c.x + s * 0.1f, c.y - s * 0.05f}, 3, WHITE);
        for (int i = 0; i < 6; i++)
            DrawTriangle({c.x - s * 0.15f + i * s * 0.05f, c.y + s * 0.1f}, {c.x - s * 0.125f + i * s * 0.05f, c.y + s * 0.16f}, {c.x - s * 0.1f + i * s * 0.05f, c.y + s * 0.1f}, rgba(0xe8dcc0ff));
        // pumpkin
        Vector2 p{c.x + s * 0.3f, c.y + s * 0.28f};
        DrawEllipse((int)p.x, (int)p.y, s * 0.14f, s * 0.11f, rgba(0xff7a1aff));
        DrawRectangleRec({p.x - 3, p.y - s * 0.14f, 6, 10}, rgba(0x3a6a2aff));
        DrawTriangle({p.x - 9, p.y - 2}, {p.x - 3, p.y - 2}, {p.x - 6, p.y - 9}, rgba(0xffd84aff));
        DrawTriangle({p.x + 3, p.y - 2}, {p.x + 9, p.y - 2}, {p.x + 6, p.y - 9}, rgba(0xffd84aff));
    } else if (id == "roulette") {
        for (int i = 0; i < 18; i++)
            DrawCircleSector(c, s * 0.38f, t * 40 + i * 20, t * 40 + (i + 1) * 20, 4, i == 0 ? col::green : (i % 2 ? col::red : rgba(0x1d1a24ff)));
        DrawCircleV(c, s * 0.23f, rgba(0x6b3a1aff));
        DrawCircleV(c, s * 0.08f, col::gold);
        DrawCircleV(c + fromAngle(-t * 3, s * 0.32f), 6, WHITE);
    } else if (id == "blackjack") {
        card({c.x - s * 0.36f, c.y - s * 0.26f, s * 0.36f, s * 0.5f}, 1, 0, true);
        card({c.x - s * 0.04f, c.y - s * 0.32f + bob, s * 0.36f, s * 0.5f}, 13, 1, true);
    } else if (id == "mines") {
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++) {
                Vector2 p{c.x + (x - 1) * s * 0.26f, c.y + (y - 1) * s * 0.26f};
                DrawRectangleRounded({p.x - s * 0.11f, p.y - s * 0.11f, s * 0.22f, s * 0.22f}, 0.25f, 6, rgba(0x5a3aa8ff));
                if (x == 0 && y == 0) stroopwafelTop(p, s * 0.08f);
                if (x == 2 && y == 0) bomb(p, s * 0.06f, t);
                if (x == 1 && y == 2) gem(p, s * 0.07f, col::cyan);
            }
    } else if (id == "slots") {
        for (int i = 0; i < 3; i++) portrait("pieter", {c.x + (i - 1) * s * 0.28f, c.y - s * 0.04f + (i == 1 ? bob : 0)}, s * 0.13f);
        ui::textShadow("777", c.x, c.y + s * 0.28f, s * 0.14f, col::gold, ui::Center, true, 2);
    } else if (id == "tower") {
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 3; k++) {
                Rectangle tl{c.x - s * 0.33f + k * s * 0.23f, c.y + s * 0.22f - row * s * 0.15f, s * 0.2f, s * 0.12f};
                DrawRectangleRounded(tl, 0.3f, 6, row == 1 ? rgba(0x3a8a4aff) : rgba(0x4a4258ff));
            }
        DrawEllipse((int)(c.x - s * 0.0f), (int)(c.y + s * 0.13f), 6, 8, rgba(0xffd860ff));
        DrawCircleV({c.x, c.y - s * 0.3f + bob}, s * 0.13f, rgba(0x2f9a4aff));
        DrawCircleV({c.x - s * 0.05f, c.y - s * 0.32f + bob}, 4, rgba(0xffd030ff));
        DrawCircleV({c.x + s * 0.05f, c.y - s * 0.32f + bob}, 4, rgba(0xffd030ff));
    } else if (id == "crash") {
        // rising curve + rocket
        Vector2 o{r.x + 16, r.y + r.height - 16};
        float w = r.width - 50, h = r.height - 50;
        Vector2 prev = o;
        for (int i = 1; i <= 30; i++) {
            float u = i / 30.0f;
            Vector2 p{o.x + u * w, o.y - (std::exp(u * 2.4f) - 1) / (std::exp(2.4f) - 1) * h};
            DrawLineEx(prev, p, 4, col::green);
            prev = p;
        }
        fx::beginAdd();
        fx::glow({prev.x - 20, prev.y + 24}, 30, alpha(col::orange, 0.8f));
        fx::endAdd();
        fx::sprite(E().tex("spr/rocket"), {prev.x - 6, prev.y + 8}, s / 900, -55);
        ui::textShadow("2.47x", c.x - s * 0.12f, c.y + s * 0.05f, s * 0.13f, WHITE, ui::Center, true, 2);
    } else if (id == "kart") {
        fx::tiled(E().tex("tex/grass"), r, 0.5f, {t * 4, 0});
        Rectangle road{r.x, c.y - s * 0.2f, r.width, s * 0.42f};
        fx::tiled(E().tex("tex/asphalt"), road, 0.5f, {t * 300, 0});
        float shift = std::fmod(t * 150, 40.0f);
        for (float x = r.x - shift; x < r.x + r.width; x += 20) {
            float x0 = std::max(x, r.x), x1 = std::min(x + 20, r.x + r.width);
            if (x1 <= x0) continue;
            bool red = (int)std::lround((x - r.x + shift) / 20) % 2 == 0;
            DrawRectangleRec({x0, road.y - 8, x1 - x0, 8}, red ? col::red : WHITE);
            DrawRectangleRec({x0, road.y + road.height, x1 - x0, 8}, red ? WHITE : col::red);
        }
        float bob = std::sin(t * 9) * 3;
        fx::sprite(E().tex("spr/kart_davi"), {c.x - s * 0.22f, c.y + s * 0.08f + bob}, s / 360, 0);
        portrait("davi", {c.x - s * 0.22f, c.y + s * 0.06f + bob}, s * 0.05f, col::outline);
        fx::sprite(E().tex("spr/kart_pieter"), {c.x + s * 0.18f, c.y - s * 0.06f}, s / 330, 0);
        portrait("pieter", {c.x + s * 0.18f, c.y - s * 0.08f}, s * 0.055f, col::outline);
        fx::beginAdd();
        fx::glow({c.x + s * 0.02f, c.y - s * 0.06f}, 26, alpha(col::orange, 0.8f));
        fx::endAdd();
    } else if (id == "dungeon") {
        fx::tiled(E().tex("tex/stone"), shrink(r, 4), 0.35f, {0, 0}, rgba(0xa098c0ff));
        fx::tiled(E().tex("tex/brick_dark"), {r.x + 4, r.y + 4, r.width - 8, r.height * 0.22f}, 0.4f);
        fx::beginAdd();
        fx::glow({r.x + r.width * 0.2f, r.y + r.height * 0.15f}, s * 0.35f, alpha(col::orange, 0.5f));
        fx::glow({r.x + r.width * 0.8f, r.y + r.height * 0.15f}, s * 0.35f, alpha(col::orange, 0.5f));
        fx::endAdd();
        fx::sprite(E().tex("spr/chair"), {c.x + s * 0.2f, c.y + s * 0.02f}, s / 620, std::sin(t * 2) * 12);
        portrait("davi", {c.x + s * 0.2f, c.y + bob}, s * 0.09f, col::cyan);
        fx::sprite(E().tex("spr/router"), {c.x - s * 0.28f, c.y + s * 0.18f}, s / 520, 0);
        pieter({c.x - s * 0.12f, c.y - s * 0.12f}, s * 0.08f, P().hat);
        for (int i = 0; i < 3; i++) {
            Vector2 p{c.x - s * 0.05f + std::fmod(t * 120 + i * 30, s * 0.22f), c.y - s * 0.1f + i * 2.0f};
            fx::shadedBall(p, 4, rgba(0x3a2418ff), 1);
        }
    } else if (id == "plinko") {
        for (int row = 0; row < 6; row++)
            for (int k = 0; k <= row; k++) DrawCircleV({c.x + (k - row / 2.0f) * s * 0.11f, c.y - s * 0.3f + row * s * 0.1f}, 3.5f, WHITE);
        const Color bk[] = {col::red, col::orange, col::gold, col::gold, col::orange, col::red};
        for (int i = 0; i < 6; i++) DrawRectangleRounded({c.x + (i - 3) * s * 0.11f + 2, c.y + s * 0.3f, s * 0.1f, s * 0.08f}, 0.3f, 4, bk[i]);
        float ph = std::fmod(t * 0.8f, 1.0f);
        Vector2 b{c.x + std::sin(ph * 18) * s * 0.06f * ph, c.y - s * 0.38f + ph * s * 0.66f};
        DrawCircleV(b, 7, col::pink);
    }
}

}  // namespace pg::art
