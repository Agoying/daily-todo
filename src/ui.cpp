#include "app.h"
#include <cmath>

using namespace Gdiplus;

// ---------------------------------------------------------------- theme

static Color C(int r, int g, int b) { return Color(255, (BYTE)r, (BYTE)g, (BYTE)b); }
static Color CA(int a, int r, int g, int b) { return Color((BYTE)a, (BYTE)r, (BYTE)g, (BYTE)b); }

void InitTheme(bool dark)
{
    TH.dark = dark;
    if (!dark) {
        TH.bg            = C(243, 243, 243);
        TH.bgSoft        = C(238, 238, 238);
        TH.card          = C(255, 255, 255);
        TH.cardHover     = C(250, 250, 250);
        TH.border        = C(228, 228, 228);
        TH.divider       = C(236, 236, 236);
        TH.text          = C(26, 26, 26);
        TH.text2         = C(96, 96, 96);
        TH.text3         = C(142, 142, 142);
        TH.accent        = C(15, 108, 189);
        TH.accentHover   = C(12, 90, 158);
        TH.accentSoft    = C(232, 241, 250);
        TH.onAccent      = C(255, 255, 255);
        TH.danger        = C(196, 43, 28);
        TH.dangerSoft    = C(253, 236, 234);
        TH.success       = C(16, 124, 65);
        TH.warn          = C(157, 93, 0);
        TH.navHover      = CA(12, 0, 0, 0);
        TH.navActive     = C(255, 255, 255);
        TH.control       = C(255, 255, 255);
        TH.controlHover  = C(246, 246, 246);
        TH.controlPressed= C(237, 237, 237);
        TH.shadow        = CA(16, 0, 0, 0);
    } else {
        TH.bg            = C(32, 32, 32);
        TH.bgSoft        = C(38, 38, 38);
        TH.card          = C(44, 44, 44);
        TH.cardHover     = C(51, 51, 51);
        TH.border        = C(62, 62, 62);
        TH.divider       = C(56, 56, 56);
        TH.text          = C(255, 255, 255);
        TH.text2         = C(178, 178, 178);
        TH.text3         = C(138, 138, 138);
        TH.accent        = C(76, 194, 255);
        TH.accentHover   = C(112, 208, 255);
        TH.accentSoft    = C(24, 58, 78);
        TH.onAccent      = C(0, 24, 34);
        TH.danger        = C(255, 120, 100);
        TH.dangerSoft    = C(72, 34, 30);
        TH.success       = C(84, 200, 140);
        TH.warn          = C(240, 190, 90);
        TH.navHover      = CA(16, 255, 255, 255);
        TH.navActive     = CA(20, 255, 255, 255);
        TH.control       = C(50, 50, 50);
        TH.controlHover  = C(58, 58, 58);
        TH.controlPressed= C(66, 66, 66);
        TH.shadow        = CA(60, 0, 0, 0);
    }
}

// ---------------------------------------------------------------- fonts

static Font* g_fonts[FK_COUNT] = { nullptr };
static wstring g_familyText = L"Segoe UI";
static wstring g_familyDisplay = L"Segoe UI";

static bool FamilyExists(Graphics& g, const wchar_t* name)
{
    FontFamily ff(name);
    return ff.IsAvailable();
}

void InitFonts(int dpi)
{
    for (int i = 0; i < FK_COUNT; ++i) {
        if (g_fonts[i]) { delete g_fonts[i]; g_fonts[i] = nullptr; }
    }

    // 探测 Segoe UI Variable（Win11）
    HDC hdc = GetDC(nullptr);
    {
        Graphics probe(hdc);
        if (FamilyExists(probe, L"Segoe UI Variable Text")) {
            g_familyText = L"Segoe UI Variable Text";
            g_familyDisplay = FamilyExists(probe, L"Segoe UI Variable Display")
                              ? L"Segoe UI Variable Display" : L"Segoe UI Variable Text";
        } else {
            g_familyText = L"Segoe UI";
            g_familyDisplay = L"Segoe UI";
        }
    }
    ReleaseDC(nullptr, hdc);

    struct Spec { float px; int style; bool display; };
    Spec specs[FK_COUNT] = {
        { 26.0f, FontStyleBold,     true  },  // H1
        { 18.0f, FontStyleBold,     true  },  // H2
        { 15.0f, FontStyleBold,     false },  // CARD
        { 14.0f, FontStyleRegular,  false },  // BODY
        { 12.0f, FontStyleRegular,  false },  // SUB
        { 11.0f, FontStyleRegular,  false },  // SMALL
        { 13.0f, FontStyleRegular,  false },  // MONO
    };

    for (int i = 0; i < FK_COUNT; ++i) {
        REAL px = specs[i].px * (dpi / 96.0f);
        const wstring& fam = specs[i].display ? g_familyDisplay : g_familyText;
        g_fonts[i] = new Font(fam.c_str(), px, specs[i].style, UnitPixel);
        if (g_fonts[i]->GetLastStatus() != Ok) {
            delete g_fonts[i];
            g_fonts[i] = new Font(L"Segoe UI", px, specs[i].style, UnitPixel);
        }
    }
}

Font* F(FontKind k) { return g_fonts[k] ? g_fonts[k] : g_fonts[FK_BODY]; }

// ---------------------------------------------------------------- primitives

static void RoundPath(GraphicsPath& p, const RectF& r, float rad)
{
    float d = rad * 2.0f;
    if (d > r.Width) d = r.Width;
    if (d > r.Height) d = r.Height;
    rad = d / 2.0f;
    if (rad <= 0.05f) { p.AddRectangle(r); return; }

    p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

static RectF R2F(const RECT& r)
{
    return RectF((REAL)r.left, (REAL)r.top,
                 (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
}

void FillRound(Graphics& g, const RECT& r, int radius, const Color& c)
{
    SolidBrush b(c);
    FillRound(g, r, radius, b);
}

void FillRound(Graphics& g, const RECT& r, int radius, Brush& b)
{
    GraphicsPath p;
    RoundPath(p, R2F(r), (float)radius);
    g.FillPath(&b, &p);
}

void StrokeRound(Graphics& g, const RECT& r, int radius, const Color& c, float w)
{
    GraphicsPath p;
    RectF rf = R2F(r);
    rf.X += w / 2.0f; rf.Y += w / 2.0f;
    rf.Width -= w; rf.Height -= w;
    RoundPath(p, rf, (float)radius);
    Pen pen(c, w);
    g.DrawPath(&pen, &p);
}

void FillRectC(Graphics& g, const RECT& r, const Color& c)
{
    SolidBrush b(c);
    g.FillRectangle(&b, RectF((REAL)r.left, (REAL)r.top,
                              (REAL)(r.right - r.left), (REAL)(r.bottom - r.top)));
}

RECT Inflate(RECT r, int dx, int dy)
{
    r.left -= dx; r.top -= dy; r.right += dx; r.bottom += dy;
    return r;
}

// ---------------------------------------------------------------- text

static void DrawTextImpl(Graphics& g, const wstring& s, const RECT& r, Font* fo,
                         const Color& c, TextAlign align, bool wrap)
{
    if (s.empty() || !fo) return;
    StringFormat fmt;
    fmt.SetAlignment(align == ALIGN_CENTER ? StringAlignmentCenter
                    : align == ALIGN_RIGHT ? StringAlignmentFar : StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    if (!wrap) fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    if (!wrap) fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    else fmt.SetTrimming(StringTrimmingNone);

    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    SolidBrush br(c);
    RectF rf((REAL)r.left, (REAL)r.top,
             (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
    g.DrawString(s.c_str(), (INT)s.size(), fo, rf, &fmt, &br);
}

void DrawTextC(Graphics& g, const wstring& s, const RECT& r, FontKind fk,
               const Color& c, TextAlign align, bool wrap)
{
    DrawTextImpl(g, s, r, F(fk), c, align, wrap);
}

void DrawTextC(Graphics& g, const wstring& s, const RECT& r, Font* f,
               const Color& c, TextAlign align, bool wrap)
{
    DrawTextImpl(g, s, r, f, c, align, wrap);
}

int MeasureTextW(Graphics& g, const wstring& s, FontKind fk)
{
    if (s.empty()) return 0;
    RectF box;
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.MeasureString(s.c_str(), (INT)s.size(), F(fk), PointF(0, 0), &box);
    return (int)ceilf(box.Width) + 2;
}

// ---------------------------------------------------------------- glyphs

void DrawGlyph(Graphics& g, Glyph gl, const RECT& box, const Color& c, float stroke)
{
    float w = (float)(box.right - box.left);
    float h = (float)(box.bottom - box.top);
    if (w <= 0 || h <= 0) return;

    GraphicsState st = g.Save();
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    float penW = stroke * (w / 16.0f);
    if (penW < 1.0f) penW = 1.0f;
    Pen pen(c, penW);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    float l = (float)box.left, t = (float)box.top;

#define PX(u) (l + (u) * w)
#define PY(v) (t + (v) * h)

    switch (gl) {
    case GL_PLUS:
        g.DrawLine(&pen, PX(0.5f), PY(0.16f), PX(0.5f), PY(0.84f));
        g.DrawLine(&pen, PX(0.16f), PY(0.5f), PX(0.84f), PY(0.5f));
        break;

    case GL_EDIT: {
        GraphicsPath p;
        p.AddLine(PX(0.18f), PY(0.82f), PX(0.25f), PY(0.60f));
        p.AddLine(PX(0.25f), PY(0.60f), PX(0.63f), PY(0.22f));
        p.AddLine(PX(0.63f), PY(0.22f), PX(0.78f), PY(0.37f));
        p.AddLine(PX(0.78f), PY(0.37f), PX(0.40f), PY(0.75f));
        p.CloseFigure();
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, PX(0.25f), PY(0.60f), PX(0.40f), PY(0.75f));
        break;
    }

    case GL_TRASH: {
        g.DrawLine(&pen, PX(0.18f), PY(0.27f), PX(0.82f), PY(0.27f));
        g.DrawLine(&pen, PX(0.39f), PY(0.27f), PX(0.39f), PY(0.16f));
        g.DrawLine(&pen, PX(0.39f), PY(0.16f), PX(0.61f), PY(0.16f));
        g.DrawLine(&pen, PX(0.61f), PY(0.16f), PX(0.61f), PY(0.27f));
        GraphicsPath p;
        p.AddLine(PX(0.27f), PY(0.27f), PX(0.33f), PY(0.84f));
        p.AddLine(PX(0.33f), PY(0.84f), PX(0.67f), PY(0.84f));
        p.AddLine(PX(0.67f), PY(0.84f), PX(0.73f), PY(0.27f));
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, PX(0.43f), PY(0.40f), PX(0.44f), PY(0.72f));
        g.DrawLine(&pen, PX(0.57f), PY(0.40f), PX(0.56f), PY(0.72f));
        break;
    }

    case GL_GEAR: {
        const float TWO_PI = 6.28318531f;
        const int   TEETH = 8;
        const float R_OUT = 0.47f, R_MID = 0.36f, R_HOLE = 0.16f;
        PointF poly[TEETH * 4];
        int n = 0;
        float step = TWO_PI / TEETH;
        for (int i = 0; i < TEETH; ++i) {
            float a = TWO_PI * (float)i / (float)TEETH;
            poly[n++] = PointF(0.5f + cosf(a - step * 0.15f) * R_OUT, 0.5f + sinf(a - step * 0.15f) * R_OUT);
            poly[n++] = PointF(0.5f + cosf(a + step * 0.15f) * R_OUT, 0.5f + sinf(a + step * 0.15f) * R_OUT);
            poly[n++] = PointF(0.5f + cosf(a + step * 0.36f) * R_MID, 0.5f + sinf(a + step * 0.36f) * R_MID);
            poly[n++] = PointF(0.5f + cosf(a + step * 0.86f) * R_MID, 0.5f + sinf(a + step * 0.86f) * R_MID);
        }
        for (int i = 0; i < n; ++i) poly[i] = PointF(PX(poly[i].X), PY(poly[i].Y));
        GraphicsPath gear(FillModeAlternate);
        gear.AddPolygon(poly, n);
        gear.AddEllipse(RectF(PX(0.5f - R_HOLE), PY(0.5f - R_HOLE),
                              w * R_HOLE * 2.0f, h * R_HOLE * 2.0f));
        SolidBrush gbr(c);
        g.FillPath(&gbr, &gear);
        break;
    }

    case GL_CHEVRON_L: {
        GraphicsPath p;
        p.AddLine(PX(0.62f), PY(0.20f), PX(0.34f), PY(0.50f));
        p.AddLine(PX(0.34f), PY(0.50f), PX(0.62f), PY(0.80f));
        g.DrawPath(&pen, &p);
        break;
    }

    case GL_CHEVRON_R: {
        GraphicsPath p;
        p.AddLine(PX(0.38f), PY(0.20f), PX(0.66f), PY(0.50f));
        p.AddLine(PX(0.66f), PY(0.50f), PX(0.38f), PY(0.80f));
        g.DrawPath(&pen, &p);
        break;
    }

    case GL_CHEVRON_D: {
        GraphicsPath p;
        p.AddLine(PX(0.22f), PY(0.40f), PX(0.50f), PY(0.66f));
        p.AddLine(PX(0.50f), PY(0.66f), PX(0.78f), PY(0.40f));
        g.DrawPath(&pen, &p);
        break;
    }

    case GL_CLOCK: {
        g.DrawEllipse(&pen, PX(0.14f), PY(0.14f), w * 0.72f, h * 0.72f);
        g.DrawLine(&pen, PX(0.50f), PY(0.50f), PX(0.50f), PY(0.29f));
        g.DrawLine(&pen, PX(0.50f), PY(0.50f), PX(0.66f), PY(0.58f));
        break;
    }

    case GL_CHECK: {
        GraphicsPath p;
        p.AddLine(PX(0.18f), PY(0.52f), PX(0.41f), PY(0.75f));
        p.AddLine(PX(0.41f), PY(0.75f), PX(0.82f), PY(0.26f));
        g.DrawPath(&pen, &p);
        break;
    }

    case GL_BELL: {
        GraphicsPath p;
        p.AddLine(PX(0.22f), PY(0.72f), PX(0.30f), PY(0.66f));
        p.AddLine(PX(0.30f), PY(0.66f), PX(0.30f), PY(0.44f));
        p.AddBezier(PX(0.30f), PY(0.44f), PX(0.30f), PY(0.22f), PX(0.70f), PY(0.22f), PX(0.70f), PY(0.44f));
        p.AddLine(PX(0.70f), PY(0.44f), PX(0.70f), PY(0.66f));
        p.AddLine(PX(0.70f), PY(0.66f), PX(0.78f), PY(0.72f));
        p.AddLine(PX(0.78f), PY(0.72f), PX(0.22f), PY(0.72f));
        g.DrawPath(&pen, &p);
        g.DrawArc(&pen, PX(0.42f), PY(0.72f), w * 0.16f, h * 0.16f, 0.0f, 180.0f);
        break;
    }

    case GL_POWER: {
        g.DrawArc(&pen, PX(0.22f), PY(0.24f), w * 0.56f, h * 0.56f, -60.0f, 300.0f);
        g.DrawLine(&pen, PX(0.50f), PY(0.18f), PX(0.50f), PY(0.44f));
        break;
    }

    case GL_PERSON: {
        // 头
        float hr = w * 0.16f;
        g.DrawEllipse(&pen, PX(0.50f) - hr, PY(0.28f) - hr, hr * 2, hr * 2);
        // 肩
        GraphicsPath bp;
        bp.AddBezier(PX(0.22f), PY(0.82f), PX(0.26f), PY(0.58f), PX(0.74f), PY(0.58f), PX(0.78f), PY(0.82f));
        g.DrawPath(&pen, &bp);
        break;
    }

    case GL_FOLDER: {
        GraphicsPath p;
        p.AddLine(PX(0.18f), PY(0.30f), PX(0.44f), PY(0.30f));
        p.AddLine(PX(0.44f), PY(0.30f), PX(0.52f), PY(0.40f));
        p.AddLine(PX(0.52f), PY(0.40f), PX(0.82f), PY(0.40f));
        p.AddLine(PX(0.82f), PY(0.40f), PX(0.82f), PY(0.78f));
        p.AddLine(PX(0.82f), PY(0.78f), PX(0.18f), PY(0.78f));
        p.CloseFigure();
        g.DrawPath(&pen, &p);
        break;
    }

    case GL_SEARCH: {
        g.DrawEllipse(&pen, PX(0.16f), PY(0.16f), w * 0.50f, h * 0.50f);
        g.DrawLine(&pen, PX(0.62f), PY(0.62f), PX(0.84f), PY(0.84f));
        break;
    }
    }

#undef PX
#undef PY

    g.Restore(st);
}
