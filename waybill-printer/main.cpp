// Waybill printer: type a SPEX domestic waybill on screen and print only the
// text, straight onto the pre-printed form. Field positions are adjusted with
// the mouse over a photo of the form, and every printed waybill is logged to
// a formatted Excel sheet.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winspool.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using std::string;
using std::vector;
using std::wstring;

// ---------------------------------------------------------------------------
// Form fields
// ---------------------------------------------------------------------------

enum Align { AUTO, LEFT, CENTER };

struct FieldDef {
    const wchar_t* key;
    const wchar_t* label;  // Arabic wording as printed on the form
    const wchar_t* en;     // English wording as printed on the form
    bool multi;
    Align align;
    int x1, y1, x2, y2;  // writing area on the 1600x748 reference photo
};

const int kPhotoW = 1600, kPhotoH = 748;

const FieldDef kFields[] = {
    {L"date",         L"التاريخ",                       L"Date",                          false, LEFT,   868,  50, 1022,  96},
    {L"sender_name",  L"اسم المرسل المعتمد لدى سبيكس",  L"Sender's Name",                 false, AUTO,    62, 200,  606, 233},
    {L"sender_phone", L"رقم هاتف المرسل المعتمد",       L"Sender's Phone number",         false, AUTO,    62, 265,  606, 338},
    {L"sender_addr",  L"عنوان المرسل",                  L"Sender's Address",              true,  AUTO,    56, 372,  606, 545},
    {L"recv_name",    L"اسم المستلم",                   L"Name",                          false, AUTO,   620, 196, 1188, 228},
    {L"recv_phone",   L"رقم الهاتف",                    L"Phone No.",                     false, AUTO,   620, 260, 1188, 336},
    {L"recv_city",    L"المدينة/القرية",                L"City / the village",            false, AUTO,   620, 366, 1188, 438},
    {L"recv_street",  L"الشارع إن وجد واقرب معلم",      L"The street that he found the nearest", true, AUTO, 620, 466, 1196, 546},
    {L"pieces",       L"عدد الطرود",                    L"No. of pieces",                 false, CENTER, 1200, 163, 1462, 216},
    {L"notes",        L"6. ملاحظات",                    L"6. Notes",                      true,  AUTO,  1200, 244, 1578, 384},
    {L"ref",          L"7. رقم المرسل المرجعي",         L"7. The sender's reference number", false, AUTO, 1202, 410, 1582, 548},
    {L"value",        L"8. قيمة الطلب شامل التوصيل",    L"8. The value of the order includes the delivery", false, CENTER, 1210, 582, 1590, 706},
};
const int NF = sizeof(kFields) / sizeof(kFields[0]);

enum {
    F_DATE, F_SNAME, F_SPHONE, F_SADDR, F_RNAME, F_RPHONE, F_RCITY, F_RSTREET,
    F_PIECES, F_NOTES, F_REF, F_VALUE
};

struct Box { double x, y, w, h; };

struct Layout {
    double paperW = 240, paperH = 112;  // mm
    double offX = 0, offY = 0;          // shift of everything, mm
    int rotate = 90;                    // 0 = fed wide edge first, 90/270 = fed lengthwise
    bool customPaper = true;
    wstring font = L"Arial";
    bool bold = true;
    int fontPt = 11;
    Box box[NF];
};

Box DefaultBox(int i, double paperW, double paperH) {
    const FieldDef& f = kFields[i];
    double sx = paperW / kPhotoW, sy = paperH / kPhotoH;
    return {f.x1 * sx, f.y1 * sy, (f.x2 - f.x1) * sx, (f.y2 - f.y1) * sy};
}

void ResetBoxes(Layout& lay) {
    for (int i = 0; i < NF; ++i) lay.box[i] = DefaultBox(i, lay.paperW, lay.paperH);
    lay.offX = lay.offY = 0;
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

HINSTANCE g_inst;
wstring g_dir, g_ini, g_dataFile, g_xlsx;
Layout g_lay;
wstring g_printer;
int g_dpi = 96;
HFONT g_uiFont;
HBITMAP g_formBmp;

HWND g_main, g_waybillNo, g_fontSize, g_copies;
wstring g_statusText;
int g_statusKind;  // 0 info, 1 success, 2 error
HWND g_edit[NF];
HWND g_editor;

enum {
    IDC_PRINT = 100, IDC_EDITOR, IDC_TEST, IDC_CLEAR, IDC_PRINTER, IDC_EXCEL,
    IDC_FONT_DEC, IDC_FONT_INC, IDC_COPY_DEC, IDC_COPY_INC, IDC_ROT_LONG, IDC_ROT_FLIP, IDC_ROT_WIDE,
    IDC_EDIT_BASE = 200,
    IDC_WAYBILL = 300, IDC_FONTSIZE, IDC_COPIES,
    IDC_ED_SAVE = 400, IDC_ED_CANCEL, IDC_ED_TEST, IDC_ED_RESET, IDC_ED_APPLY, IDC_ED_ROTATE
};

double g_fit = 1;  // shrinks the whole window on small screens
int S(int v) { return (int)lround(v * g_dpi / 96.0 * g_fit); }

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    wstring s(n, L'\0');
    if (n > 0) GetWindowTextW(h, &s[0], n + 1);
    return s;
}

wstring Trim(const wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

wstring ReplaceAll(wstring s, const wstring& from, const wstring& to) {
    for (size_t p = 0; (p = s.find(from, p)) != wstring::npos; p += to.size())
        s.replace(p, from.size(), to);
    return s;
}

bool HasArabic(const wstring& s) {
    for (wchar_t c : s)
        if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) ||
            (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF))
            return true;
    return false;
}

wstring Num(double v, int decimals = 1) {
    wchar_t b[64];
    swprintf(b, 64, decimals == 0 ? L"%.0f" : L"%.1f", v);
    return b;
}

string Utf8(const wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

wstring FromUtf8(const string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// Accepts Western and Arabic-Indic digits, with optional thousands separators.
bool ParseNumber(const wstring& in, double& out) {
    string ascii;
    bool dot = false, digit = false;
    for (wchar_t c : Trim(in)) {
        if (c >= L'0' && c <= L'9') { ascii += (char)c; digit = true; }
        else if (c >= 0x0660 && c <= 0x0669) { ascii += (char)('0' + c - 0x0660); digit = true; }
        else if (c >= 0x06F0 && c <= 0x06F9) { ascii += (char)('0' + c - 0x06F0); digit = true; }
        else if ((c == L'.' || c == 0x066B) && !dot) { ascii += '.'; dot = true; }
        else if (c == L',' || c == 0x066C) continue;
        else return false;
    }
    if (!digit) return false;
    out = strtod(ascii.c_str(), nullptr);
    return true;
}

void Msg(HWND owner, const wstring& text, UINT icon = MB_ICONINFORMATION) {
    MessageBoxW(owner, text.c_str(), L"طباعة البوليصة", icon | MB_OK | MB_RTLREADING | MB_RIGHT);
}

// ---------------------------------------------------------------------------
// Settings file (UTF-16 INI next to the exe, so Arabic text survives)
// ---------------------------------------------------------------------------

wstring IniGet(const wchar_t* sec, const wchar_t* key) {
    wchar_t buf[4096];
    GetPrivateProfileStringW(sec, key, L"", buf, 4096, g_ini.c_str());
    return buf;
}

double IniGetD(const wchar_t* sec, const wchar_t* key, double def) {
    wstring s = IniGet(sec, key);
    return s.empty() ? def : _wtof(s.c_str());
}

void IniSet(const wchar_t* sec, const wchar_t* key, const wstring& v) {
    WritePrivateProfileStringW(sec, key, v.c_str(), g_ini.c_str());
}

// Multi-line text is stored on one line with "\n" markers.
wstring IniEncode(const wstring& s) { return ReplaceAll(ReplaceAll(s, L"\r\n", L"\\n"), L"\n", L"\\n"); }
wstring IniDecode(const wstring& s) { return ReplaceAll(s, L"\\n", L"\r\n"); }

void WriteFileUtf16(const wstring& path, const wstring& content) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    const wchar_t bom = 0xFEFF;
    WriteFile(f, &bom, 2, &w, nullptr);
    WriteFile(f, content.data(), (DWORD)(content.size() * 2), &w, nullptr);
    CloseHandle(f);
}

void SaveLayout(const Layout& lay) {
    IniSet(L"layout", L"paper_w", Num(lay.paperW));
    IniSet(L"layout", L"paper_h", Num(lay.paperH));
    IniSet(L"layout", L"offset_x", Num(lay.offX));
    IniSet(L"layout", L"offset_y", Num(lay.offY));
    IniSet(L"layout", L"rotate", Num(lay.rotate, 0));
    IniSet(L"layout", L"custom_paper", lay.customPaper ? L"1" : L"0");
    IniSet(L"layout", L"font", lay.font);
    IniSet(L"layout", L"bold", lay.bold ? L"1" : L"0");
    IniSet(L"layout", L"font_size", Num(lay.fontPt, 0));
    for (int i = 0; i < NF; ++i) {
        wstring sec = wstring(L"field_") + kFields[i].key;
        IniSet(sec.c_str(), L"x", Num(lay.box[i].x));
        IniSet(sec.c_str(), L"y", Num(lay.box[i].y));
        IniSet(sec.c_str(), L"w", Num(lay.box[i].w));
        IniSet(sec.c_str(), L"h", Num(lay.box[i].h));
    }
}

void LoadLayout(Layout& lay) {
    lay.paperW = IniGetD(L"layout", L"paper_w", 240);
    lay.paperH = IniGetD(L"layout", L"paper_h", 112);
    lay.offX = IniGetD(L"layout", L"offset_x", 0);
    lay.offY = IniGetD(L"layout", L"offset_y", 0);
    lay.rotate = (int)IniGetD(L"layout", L"rotate", 90);
    if (lay.rotate != 90 && lay.rotate != 270) lay.rotate = 0;
    lay.customPaper = IniGetD(L"layout", L"custom_paper", 1) != 0;
    wstring font = IniGet(L"layout", L"font");
    lay.font = font.empty() ? L"Arial" : font;
    lay.bold = IniGetD(L"layout", L"bold", 1) != 0;
    lay.fontPt = (int)IniGetD(L"layout", L"font_size", 11);
    for (int i = 0; i < NF; ++i) {
        Box d = DefaultBox(i, lay.paperW, lay.paperH);
        wstring sec = wstring(L"field_") + kFields[i].key;
        lay.box[i] = {IniGetD(sec.c_str(), L"x", d.x), IniGetD(sec.c_str(), L"y", d.y),
                      IniGetD(sec.c_str(), L"w", d.w), IniGetD(sec.c_str(), L"h", d.h)};
    }
}

void EnsureIni() {
    if (GetFileAttributesW(g_ini.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    // The BOM makes Windows keep this file in UTF-16, so Arabic values are safe.
    WriteFileUtf16(g_ini,
        L"; إعدادات برنامج طباعة البوليصة\r\n"
        L"; الأفضل تعديل المواقع بالماوس من زر \"ضبط المواقع بالماوس\" داخل البرنامج\r\n"
        L"; كل المقاسات بالمليمتر\r\n\r\n");
    Layout def;
    ResetBoxes(def);
    SaveLayout(def);
}

// ---------------------------------------------------------------------------
// Drawing (shared by the screen editor and the printer)
// ---------------------------------------------------------------------------

struct RenderOpts {
    bool boxes = false;         // outline every field
    bool placeholders = false;  // show the field name when it is empty
    bool screen = false;
    int selected = -1;
};

RECT FieldRect(const Layout& lay, int i, double s, double ox, double oy) {
    const Box& b = lay.box[i];
    double x = lay.offX + b.x, y = lay.offY + b.y;
    return {(LONG)lround(ox + x * s), (LONG)lround(oy + y * s),
            (LONG)lround(ox + (x + b.w) * s), (LONG)lround(oy + (y + b.h) * s)};
}

HFONT PrintFont(const Layout& lay, double pt, double s) {
    int h = (int)lround(pt * 25.4 / 72.0 * s);
    return CreateFontW(-(h < 1 ? 1 : h), 0, 0, 0, lay.bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, lay.font.c_str());
}

// Draws text inside rc: first shrinks the font, then lets a one-line field
// wrap onto more lines, and only cuts the text as a last resort.
void DrawFitted(HDC dc, const wstring& text, RECT rc, const FieldDef& f, const Layout& lay,
                double s, COLORREF color, double maxPt) {
    if (text.empty()) return;
    bool ar = HasArabic(text);
    UINT align = f.align == CENTER ? DT_CENTER : f.align == LEFT ? DT_LEFT : ar ? DT_RIGHT : DT_LEFT;
    UINT base = DT_NOPREFIX | align | (ar ? DT_RTLREADING : 0);
    int boxW = rc.right - rc.left, boxH = rc.bottom - rc.top;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);

    // Tries sizes from maxPt down to minPt; draws and returns true on the first fit.
    auto attempt = [&](bool wrap, double minPt) {
        for (double pt = maxPt; pt >= minPt - 0.01; pt -= 0.5) {
            HFONT font = PrintFont(lay, pt, s);
            HGDIOBJ old = SelectObject(dc, font);
            RECT calc = {0, 0, boxW, 0};
            DrawTextW(dc, text.c_str(), -1, &calc, base | DT_CALCRECT | (wrap ? DT_WORDBREAK : DT_SINGLELINE));
            int tw = calc.right - calc.left, th = calc.bottom - calc.top;
            bool fits = tw <= boxW && th <= boxH;
            if (fits) {
                RECT r = rc;
                if (wrap && !f.multi) r.top += (boxH - th) / 2;  // keep wrapped one-line fields centred
                DrawTextW(dc, text.c_str(), -1, &r, base | (wrap ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER));
            }
            SelectObject(dc, old);
            DeleteObject(font);
            if (fits) return true;
        }
        return false;
    };
    if (!f.multi && attempt(false, std::min(maxPt, 8.0))) return;
    if (attempt(true, 6)) return;

    HFONT font = PrintFont(lay, 6, s);
    HGDIOBJ old = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), -1, &rc,
              base | (f.multi ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS));
    SelectObject(dc, old);
    DeleteObject(font);
}

void Render(HDC dc, const Layout& lay, const wstring* texts, double s, double ox, double oy,
            const RenderOpts& o) {
    for (int i = 0; i < NF; ++i) {
        RECT rc = FieldRect(lay, i, s, ox, oy);
        if (o.boxes) {
            bool sel = i == o.selected;
            HPEN pen = o.screen
                ? CreatePen(sel ? PS_SOLID : PS_DOT, sel ? 2 : 1, sel ? RGB(220, 30, 30) : RGB(40, 90, 200))
                : CreatePen(PS_SOLID, (int)std::max(1.0, 0.25 * s), RGB(0, 0, 0));
            HGDIOBJ op = SelectObject(dc, pen);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
            if (sel) {  // resize handle
                int k = S(5);
                RECT hr = {rc.right - k, rc.bottom - k, rc.right + k, rc.bottom + k};
                HBRUSH red = CreateSolidBrush(RGB(220, 30, 30));
                FillRect(dc, &hr, red);
                DeleteObject(red);
            }
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(pen);
        }
        RECT inner = rc;
        InflateRect(&inner, -(int)std::max(1.0, 0.6 * s), 0);
        if (!texts[i].empty())
            DrawFitted(dc, texts[i], inner, kFields[i], lay, s, o.screen ? RGB(0, 0, 170) : RGB(0, 0, 0), lay.fontPt);
        else if (o.placeholders)
            DrawFitted(dc, kFields[i].label, inner, kFields[i], lay, s, RGB(140, 140, 140), 8);
    }
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------

wstring DefaultPrinter() {
    DWORD n = 0;
    GetDefaultPrinterW(nullptr, &n);
    if (n == 0) return L"";
    wstring s(n, L'\0');
    if (!GetDefaultPrinterW(&s[0], &n)) return L"";
    s.resize(wcslen(s.c_str()));
    return s;
}

wstring CurrentPrinter() { return g_printer.empty() ? DefaultPrinter() : g_printer; }

// A sheet fed lengthwise is drawn upright into a bitmap that is then turned
// a quarter turn pixel by pixel, so Arabic shaping never depends on how a
// printer driver handles rotated text.
vector<uint32_t> RenderUpright(const Layout& lay, const wstring* texts, const RenderOpts& o, double s, int& w, int& h) {
    w = (int)lround(lay.paperW * s);
    h = (int)lround(lay.paperH * s);
    vector<uint32_t> out;
    HDC mem = CreateCompatibleDC(nullptr);
    BITMAPINFO bi = {};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        RECT r = {0, 0, w, h};
        FillRect(mem, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
        Render(mem, lay, texts, s, 0, 0, o);
        GdiFlush();
        out.assign((const uint32_t*)bits, (const uint32_t*)bits + (size_t)w * h);
        SelectObject(mem, old);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    return out;
}

// 90: the form's top edge ends up along the right side of the page; 270: along the left.
vector<uint32_t> Turn(const vector<uint32_t>& src, int w, int h, int rotate) {
    vector<uint32_t> dst(src.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int X = rotate == 90 ? h - 1 - y : y, Y = rotate == 90 ? x : w - 1 - x;
            dst[(size_t)Y * h + X] = src[(size_t)y * w + x];
        }
    return dst;
}

// Prints one page per copy, so each copy lands on the next sheet in the tray.
bool PrintPages(const Layout& lay, const wstring* texts, int copies, bool testPage, wstring& err) {
    wstring name = CurrentPrinter();
    if (name.empty()) { err = L"ما في طابعة معرّفة على الجهاز."; return false; }

    vector<BYTE> dmBuf;
    DEVMODEW* dm = nullptr;
    HANDLE hp = nullptr;
    if (OpenPrinterW(&name[0], &hp, nullptr)) {
        LONG sz = DocumentPropertiesW(nullptr, hp, &name[0], nullptr, nullptr, 0);
        if (sz > 0) {
            dmBuf.resize(sz);
            dm = (DEVMODEW*)dmBuf.data();
            if (DocumentPropertiesW(nullptr, hp, &name[0], dm, nullptr, DM_OUT_BUFFER) == IDOK) {
                dm->dmCopies = 1;
                dm->dmFields |= DM_COPIES;
                if (lay.customPaper) {
                    bool turned = lay.rotate != 0;
                    dm->dmPaperSize = DMPAPER_USER;
                    dm->dmPaperWidth = (short)lround((turned ? lay.paperH : lay.paperW) * 10);
                    dm->dmPaperLength = (short)lround((turned ? lay.paperW : lay.paperH) * 10);
                    dm->dmOrientation = DMORIENT_PORTRAIT;
                    dm->dmFields |= DM_PAPERSIZE | DM_PAPERWIDTH | DM_PAPERLENGTH | DM_ORIENTATION;
                }
                DocumentPropertiesW(nullptr, hp, &name[0], dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER);
            } else {
                dm = nullptr;
            }
        }
        ClosePrinter(hp);
    }

    HDC dc = CreateDCW(L"WINSPOOL", name.c_str(), nullptr, dm);
    if (!dc) { err = L"ما قدرت أفتح الطابعة: " + name; return false; }

    double dpiX = GetDeviceCaps(dc, LOGPIXELSX), dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    double physX = GetDeviceCaps(dc, PHYSICALOFFSETX), physY = GetDeviceCaps(dc, PHYSICALOFFSETY);
    double sx = dpiX / 25.4, sy = dpiY / 25.4, s0 = std::max(sx, sy);

    RenderOpts o;
    o.boxes = testPage;
    o.placeholders = testPage;
    vector<uint32_t> turned;
    int tw = 0, th = 0;
    if (lay.rotate != 0) {
        int w, h;
        vector<uint32_t> upright = RenderUpright(lay, texts, o, std::min(s0, 300 / 25.4), w, h);
        if (upright.empty()) {
            DeleteDC(dc);
            err = L"ما في ذاكرة كافية لتجهيز الصفحة.";
            return false;
        }
        turned = Turn(upright, w, h, lay.rotate);
        tw = h;
        th = w;
    }

    DOCINFOW di = {};
    di.cbSize = sizeof(di);
    di.lpszDocName = testPage ? L"Waybill test page" : L"Waybill";
    bool ok = StartDocW(dc, &di) > 0;
    for (int c = 0; ok && c < copies; ++c) {
        ok = StartPage(dc) > 0;
        if (!ok) break;
        if (lay.rotate != 0) {
            BITMAPINFO bi = {};
            bi.bmiHeader = {sizeof(BITMAPINFOHEADER), tw, -th, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
            StretchDIBits(dc, (int)-physX, (int)-physY, (int)lround(lay.paperH * sx), (int)lround(lay.paperW * sy),
                          0, 0, tw, th, turned.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        } else {
            // Draw in uniform units (s0 per mm), scaled to the page with the
            // printer's unprintable margin taken out.
            XFORM xf = {(FLOAT)(sx / s0), 0, 0, (FLOAT)(sy / s0), (FLOAT)-physX, (FLOAT)-physY};
            SetGraphicsMode(dc, GM_ADVANCED);
            SetWorldTransform(dc, &xf);
            Render(dc, lay, texts, s0, 0, 0, o);
            ModifyWorldTransform(dc, nullptr, MWT_IDENTITY);
        }
        ok = EndPage(dc) > 0;
    }
    if (ok) ok = EndDoc(dc) > 0;
    else AbortDoc(dc);
    DeleteDC(dc);
    if (!ok) err = L"صار خطأ أثناء الطباعة. تأكد إن الطابعة شغالة وفيها ورق.";
    return ok;
}

// ---------------------------------------------------------------------------
// Excel log: records live in a UTF-8 text file, and a formatted .xlsx is
// rebuilt from them after every print.
// ---------------------------------------------------------------------------

enum {
    R_TIME, R_WAYBILL, R_SNAME, R_SPHONE, R_SADDR, R_RNAME, R_RPHONE, R_RCITY, R_RSTREET,
    R_PIECES, R_VALUE, R_REF, R_NOTES, R_COPIES, R_COUNT
};

wstring Clean(wstring s) {
    s = ReplaceAll(s, L"\r\n", L" - ");
    for (wchar_t& c : s)
        if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
    return Trim(s);
}

void AppendRecord(const vector<wstring>& rec) {
    wstring line;
    for (size_t i = 0; i < rec.size(); ++i) line += (i ? L"\t" : L"") + Clean(rec[i]);
    string bytes = Utf8(line) + "\n";
    HANDLE f = CreateFileW(g_dataFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, bytes.data(), (DWORD)bytes.size(), &w, nullptr);
    CloseHandle(f);
}

vector<vector<wstring>> LoadRecords() {
    vector<vector<wstring>> out;
    HANDLE f = CreateFileW(g_dataFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    string data;
    char buf[65536];
    DWORD n;
    while (ReadFile(f, buf, sizeof(buf), &n, nullptr) && n > 0) data.append(buf, n);
    CloseHandle(f);
    wstring all = FromUtf8(data);
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find(L'\n', start);
        if (end == wstring::npos) end = all.size();
        wstring line = all.substr(start, end - start);
        start = end + 1;
        if (Trim(line).empty()) continue;
        vector<wstring> rec;
        size_t p = 0;
        for (;;) {
            size_t t = line.find(L'\t', p);
            rec.push_back(line.substr(p, t == wstring::npos ? wstring::npos : t - p));
            if (t == wstring::npos) break;
            p = t + 1;
        }
        rec.resize(R_COUNT);
        out.push_back(rec);
    }
    return out;
}

uint32_t Crc32(const string& d) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// Minimal ZIP writer (stored entries), enough for an .xlsx package.
string Zip(const vector<std::pair<string, string>>& files) {
    string out, central;
    auto u16 = [](string& s, uint16_t v) { s += (char)(v & 0xFF); s += (char)(v >> 8); };
    auto u32 = [&](string& s, uint32_t v) { u16(s, v & 0xFFFF); u16(s, v >> 16); };
    SYSTEMTIME t;
    GetLocalTime(&t);
    uint16_t dosTime = (uint16_t)((t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2));
    uint16_t dosDate = (uint16_t)(((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay);
    for (auto& [name, data] : files) {
        uint32_t crc = Crc32(data), offset = (uint32_t)out.size();
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0); u16(out, 0);
        u16(out, dosTime); u16(out, dosDate);
        u32(out, crc); u32(out, (uint32_t)data.size()); u32(out, (uint32_t)data.size());
        u16(out, (uint16_t)name.size()); u16(out, 0);
        out += name;
        out += data;
        u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0); u16(central, 0);
        u16(central, dosTime); u16(central, dosDate);
        u32(central, crc); u32(central, (uint32_t)data.size()); u32(central, (uint32_t)data.size());
        u16(central, (uint16_t)name.size()); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0);
        u32(central, 0); u32(central, offset);
        central += name;
    }
    uint32_t cdOffset = (uint32_t)out.size();
    out += central;
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0);
    u16(out, (uint16_t)files.size()); u16(out, (uint16_t)files.size());
    u32(out, (uint32_t)central.size()); u32(out, cdOffset); u16(out, 0);
    return out;
}

string Xml(const wstring& w) {
    string s;
    for (char c : Utf8(w)) {
        switch (c) {
            case '&': s += "&amp;"; break;
            case '<': s += "&lt;"; break;
            case '>': s += "&gt;"; break;
            case '"': s += "&quot;"; break;
            default:
                if ((unsigned char)c >= 0x20) s += c;
        }
    }
    return s;
}

string ColName(int c) { return string(1, (char)('A' + c)); }

string CellStr(int col, int row, int style, const wstring& v) {
    string r = ColName(col) + std::to_string(row);
    if (v.empty()) return "<c r=\"" + r + "\" s=\"" + std::to_string(style) + "\"/>";
    return "<c r=\"" + r + "\" s=\"" + std::to_string(style) + "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">" +
           Xml(v) + "</t></is></c>";
}

string CellNum(int col, int row, int style, double v) {
    char b[64];
    snprintf(b, sizeof(b), "%.10g", v);
    return "<c r=\"" + ColName(col) + std::to_string(row) + "\" s=\"" + std::to_string(style) + "\"><v>" + b + "</v></c>";
}

string CellFormula(int col, int row, int style, const string& f) {
    return "<c r=\"" + ColName(col) + std::to_string(row) + "\" s=\"" + std::to_string(style) + "\"><f>" + f + "</f></c>";
}

bool BuildXlsx() {
    auto recs = LoadRecords();

    struct Col { const wchar_t* title; int rec; int kind; double width; };  // kind 0 text, 1 count, 2 money
    const Col cols[] = {
        {L"#", -1, 1, 6},
        {L"التاريخ والوقت", R_TIME, 0, 18},
        {L"رقم البوليصة", R_WAYBILL, 0, 14},
        {L"اسم المستلم", R_RNAME, 0, 22},
        {L"هاتف المستلم", R_RPHONE, 0, 15},
        {L"المدينة / القرية", R_RCITY, 0, 16},
        {L"الشارع وأقرب معلم", R_RSTREET, 0, 30},
        {L"عدد الطرود", R_PIECES, 1, 11},
        {L"قيمة الطلب شامل التوصيل", R_VALUE, 2, 16},
        {L"رقم المرسل المرجعي", R_REF, 0, 16},
        {L"ملاحظات", R_NOTES, 0, 30},
        {L"اسم المرسل", R_SNAME, 0, 20},
        {L"هاتف المرسل", R_SPHONE, 0, 15},
        {L"عنوان المرسل", R_SADDR, 0, 28},
        {L"عدد النسخ", R_COPIES, 1, 10},
    };
    const int NC = sizeof(cols) / sizeof(cols[0]);
    const string last = ColName(NC - 1);
    const wstring sheetName = L"سجل البوالص";

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t now[64];
    swprintf(now, 64, L"%02d/%02d/%04d %02d:%02d", t.wDay, t.wMonth, t.wYear, t.wHour, t.wMinute);

    string rows;
    rows += "<row r=\"1\" ht=\"34\" customHeight=\"1\">" + CellStr(0, 1, 1, L"سجل بوالص الشحن");
    for (int c = 1; c < NC; ++c) rows += CellStr(c, 1, 1, L"");
    rows += "</row>";
    rows += "<row r=\"2\" ht=\"20\" customHeight=\"1\">" +
            CellStr(0, 2, 2, L"عدد البوالص: " + std::to_wstring(recs.size()) + L"    |    آخر تحديث: " + now) + "</row>";
    rows += "<row r=\"3\" ht=\"32\" customHeight=\"1\">";
    for (int c = 0; c < NC; ++c) rows += CellStr(c, 3, 3, cols[c].title);
    rows += "</row>";

    int r = 4;
    for (size_t i = 0; i < recs.size(); ++i, ++r) {
        bool zebra = i % 2 == 1;
        rows += "<row r=\"" + std::to_string(r) + "\">";
        for (int c = 0; c < NC; ++c) {
            wstring v = cols[c].rec < 0 ? std::to_wstring(i + 1) : recs[i][cols[c].rec];
            double num;
            if (cols[c].kind == 0)
                rows += CellStr(c, r, zebra ? 5 : 4, v);
            else if (ParseNumber(v, num))
                rows += CellNum(c, r, cols[c].kind == 2 ? (zebra ? 7 : 6) : (zebra ? 9 : 8), num);
            else
                rows += CellStr(c, r, zebra ? 9 : 8, v);
        }
        rows += "</row>";
    }
    int dataEnd = r - 1;
    if (!recs.empty()) {
        rows += "<row r=\"" + std::to_string(r) + "\" ht=\"24\" customHeight=\"1\">";
        for (int c = 0; c < NC; ++c) {
            if (c == 0) rows += CellStr(c, r, 10, L"المجموع");
            else if (cols[c].kind != 0 && cols[c].rec >= 0) {
                string col = ColName(c);
                rows += CellFormula(c, r, cols[c].kind == 2 ? 11 : 12,
                                    "SUM(" + col + "4:" + col + std::to_string(dataEnd) + ")");
            } else rows += CellStr(c, r, 10, L"");
        }
        rows += "</row>";
    }

    string colsXml = "<cols>";
    for (int c = 0; c < NC; ++c) {
        char b[128];
        snprintf(b, sizeof(b), "<col min=\"%d\" max=\"%d\" width=\"%.1f\" customWidth=\"1\"/>", c + 1, c + 1, cols[c].width);
        colsXml += b;
    }
    colsXml += "</cols>";

    string merges = "<mergeCells count=\"" + string(recs.empty() ? "2" : "3") + "\">"
                    "<mergeCell ref=\"A1:" + last + "1\"/><mergeCell ref=\"A2:" + last + "2\"/>";
    if (!recs.empty()) merges += "<mergeCell ref=\"A" + std::to_string(r) + ":G" + std::to_string(r) + "\"/>";
    merges += "</mergeCells>";

    string filter = recs.empty() ? "" : "<autoFilter ref=\"A3:" + last + std::to_string(dataEnd) + "\"/>";

    string sheet =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>"
        "<sheetViews><sheetView rightToLeft=\"1\" workbookViewId=\"0\">"
        "<pane ySplit=\"3\" topLeftCell=\"A4\" activePane=\"bottomLeft\" state=\"frozen\"/>"
        "</sheetView></sheetViews>"
        "<sheetFormatPr defaultRowHeight=\"20\"/>" +
        colsXml + "<sheetData>" + rows + "</sheetData>" + filter + merges +
        "<pageMargins left=\"0.4\" right=\"0.4\" top=\"0.5\" bottom=\"0.5\" header=\"0.3\" footer=\"0.3\"/>"
        "<pageSetup orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>"
        "</worksheet>";

    string styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<numFmts count=\"1\"><numFmt numFmtId=\"164\" formatCode=\"#,##0.00\"/></numFmts>"
        "<fonts count=\"4\">"
        "<font><sz val=\"11\"/><color rgb=\"FF000000\"/><name val=\"Arial\"/></font>"
        "<font><b/><sz val=\"11\"/><color rgb=\"FF000000\"/><name val=\"Arial\"/></font>"
        "<font><b/><sz val=\"16\"/><color rgb=\"FFFFFFFF\"/><name val=\"Arial\"/></font>"
        "<font><i/><sz val=\"10\"/><color rgb=\"FF595959\"/><name val=\"Arial\"/></font>"
        "</fonts>"
        "<fills count=\"6\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF5E2D86\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFF7A541\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFF1ECF7\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFFDE3C2\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"2\">"
        "<border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left style=\"thin\"><color rgb=\"FFB7B7B7\"/></left><right style=\"thin\"><color rgb=\"FFB7B7B7\"/></right>"
        "<top style=\"thin\"><color rgb=\"FFB7B7B7\"/></top><bottom style=\"thin\"><color rgb=\"FFB7B7B7\"/></bottom><diagonal/></border>"
        "</borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"13\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        // 1 title, 2 subtitle, 3 header
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"2\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"3\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\" wrapText=\"1\"/></xf>"
        // 4/5 text, 6/7 money, 8/9 count (plain / zebra)
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"4\" borderId=\"1\" xfId=\"0\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"4\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"4\" borderId=\"1\" xfId=\"0\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        // 10 total label, 11 total money, 12 total count
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"5\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"164\" fontId=\"1\" fillId=\"5\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"5\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>";

    string definedName = recs.empty() ? "" :
        "<definedNames><definedName name=\"_xlnm._FilterDatabase\" localSheetId=\"0\" hidden=\"1\">'" +
        Xml(sheetName) + "'!$A$3:$" + last + "$" + std::to_string(dataEnd) + "</definedName></definedNames>";

    vector<std::pair<string, string>> files = {
        {"[Content_Types].xml",
         "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
         "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
         "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
         "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
         "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
         "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
         "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
         "</Types>"},
        {"_rels/.rels",
         "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
         "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
         "</Relationships>"},
        {"xl/workbook.xml",
         "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
         "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
         "<sheets><sheet name=\"" + Xml(sheetName) + "\" sheetId=\"1\" r:id=\"rId1\"/></sheets>" + definedName +
         "</workbook>"},
        {"xl/_rels/workbook.xml.rels",
         "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
         "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
         "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
         "</Relationships>"},
        {"xl/styles.xml", styles},
        {"xl/worksheets/sheet1.xml", sheet},
    };
    string zip = Zip(files);

    wstring tmp = g_xlsx + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w;
    bool ok = WriteFile(f, zip.data(), (DWORD)zip.size(), &w, nullptr) && w == zip.size();
    CloseHandle(f);
    // Fails while the sheet is open in Excel; the records stay safe in the text file.
    if (!ok || !MoveFileExW(tmp.c_str(), g_xlsx.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Look and feel: colors, fonts, rounded shapes and owner-drawn buttons
// ---------------------------------------------------------------------------

namespace Theme {
const COLORREF Purple = RGB(94, 45, 134), PurpleLight = RGB(128, 70, 176), PurpleDark = RGB(70, 30, 102);
const COLORREF Orange = RGB(247, 165, 65);
const COLORREF Bg = RGB(244, 242, 248), Card = RGB(255, 255, 255), CardBorder = RGB(228, 223, 236);
const COLORREF Text = RGB(40, 32, 58), Muted = RGB(112, 104, 132), HeaderSub = RGB(226, 214, 240);
const COLORREF InputBorder = RGB(214, 208, 226), Tint = RGB(243, 237, 250), TintHover = RGB(228, 218, 242);
const COLORREF StatusBg = RGB(234, 230, 242), Good = RGB(28, 140, 84), Bad = RGB(200, 50, 50);
}

HFONT g_fTitle, g_fSub, g_fCard, g_fLabel, g_fInput, g_fButton, g_fSmall, g_fBig, g_fEn, g_fWaybill;
HBRUSH g_bgBrush;
HWND g_hover;

HFONT MakeFont(double pt, bool bold) {
    return CreateFontW(-(int)lround(pt * g_dpi / 72.0 * g_fit), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       L"Segoe UI");
}

Gdiplus::Color GC(COLORREF c, BYTE a = 255) { return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

void RoundPath(Gdiplus::GraphicsPath& p, const RECT& r, float rad) {
    float x = (float)r.left, y = (float)r.top, w = (float)(r.right - r.left) - 1, h = (float)(r.bottom - r.top) - 1;
    float d = std::min(rad * 2, std::min(w, h));
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

void FillRound(Gdiplus::Graphics& g, const RECT& r, float rad, COLORREF fill, BYTE alpha = 255) {
    Gdiplus::GraphicsPath p;
    RoundPath(p, r, rad);
    Gdiplus::SolidBrush b(GC(fill, alpha));
    g.FillPath(&b, &p);
}

void StrokeRound(Gdiplus::Graphics& g, const RECT& r, float rad, COLORREF color, float width, BYTE alpha = 255) {
    Gdiplus::GraphicsPath p;
    RoundPath(p, r, rad);
    Gdiplus::Pen pen(GC(color, alpha), width);
    g.DrawPath(&pen, &p);
}

void PaintText(HDC dc, const wstring& t, RECT r, HFONT f, COLORREF c,
               UINT fmt = DT_RIGHT | DT_VCENTER | DT_SINGLELINE) {
    HGDIOBJ old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, t.c_str(), -1, &r, fmt | DT_NOPREFIX | (HasArabic(t) ? DT_RTLREADING : 0));
    SelectObject(dc, old);
}

enum BtnKind { BTN_PRIMARY, BTN_SECONDARY, BTN_SMALL, BTN_SEGMENT };

bool SegmentSelected(int id);

// Buttons repaint on hover; the parent draws them in WM_DRAWITEM.
LRESULT CALLBACK ButtonHover(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (m == WM_MOUSEMOVE && g_hover != h) {
        if (g_hover) InvalidateRect(g_hover, nullptr, FALSE);
        g_hover = h;
        InvalidateRect(h, nullptr, FALSE);
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
    } else if (m == WM_MOUSELEAVE && g_hover == h) {
        g_hover = nullptr;
        InvalidateRect(h, nullptr, FALSE);
    } else if (m == WM_ERASEBKGND) {
        return 1;
    }
    return DefSubclassProc(h, m, wp, lp);
}

HWND MakeButton(HWND parent, const wchar_t* text, int id, BtnKind kind, COLORREF behind) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                             parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SetWindowLongPtrW(b, GWLP_USERDATA, (LONG_PTR)kind | ((LONG_PTR)behind << 8));
    SetWindowSubclass(b, ButtonHover, 1, 0);
    return b;
}

void DrawButton(const DRAWITEMSTRUCT* d) {
    LONG_PTR data = GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA);
    BtnKind kind = (BtnKind)(data & 0xFF);
    COLORREF behind = (COLORREF)(data >> 8);
    bool hover = d->hwndItem == g_hover, down = d->itemState & ODS_SELECTED;
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    HDC mem = CreateCompatibleDC(d->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ ob = SelectObject(mem, bmp);
    RECT r = {0, 0, w, h};
    HBRUSH bb = CreateSolidBrush(behind);
    FillRect(mem, &r, bb);
    DeleteObject(bb);

    COLORREF textColor;
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        float rad = (float)S(kind == BTN_SMALL || kind == BTN_SEGMENT ? 8 : 10);
        if (kind == BTN_PRIMARY) {
            COLORREF top = down ? Theme::PurpleDark : hover ? Theme::PurpleLight : Theme::Purple;
            COLORREF bottom = down ? Theme::PurpleDark : Theme::Purple;
            if (!down) {  // soft shadow
                RECT sh = {0, S(2), w, h};
                FillRound(g, sh, rad, Theme::PurpleDark, 60);
            }
            RECT body = {0, 0, w, h - (down ? 0 : S(2))};
            Gdiplus::GraphicsPath p;
            RoundPath(p, body, rad);
            Gdiplus::LinearGradientBrush lg(Gdiplus::Point(0, 0), Gdiplus::Point(0, h), GC(top), GC(bottom));
            g.FillPath(&lg, &p);
            textColor = RGB(255, 255, 255);
        } else if (kind == BTN_SECONDARY) {
            FillRound(g, r, rad, down ? Theme::TintHover : hover ? Theme::Tint : Theme::Card);
            RECT br = {1, 1, w - 1, h - 1};
            StrokeRound(g, br, rad, hover ? Theme::Purple : Theme::InputBorder, hover ? 1.6f : 1.2f);
            textColor = Theme::Purple;
        } else if (kind == BTN_SEGMENT && SegmentSelected(GetDlgCtrlID(d->hwndItem))) {
            FillRound(g, r, rad, Theme::Purple);
            textColor = RGB(255, 255, 255);
        } else {
            FillRound(g, r, rad, down || hover ? Theme::TintHover : Theme::Tint);
            textColor = Theme::Purple;
        }
    }
    wchar_t text[128];
    GetWindowTextW(d->hwndItem, text, 128);
    RECT tr = r;
    if (kind == BTN_PRIMARY && !down) tr.bottom -= S(2);
    PaintText(mem, text, tr, kind == BTN_SMALL && wcslen(text) == 1 ? g_fBig : g_fButton, textColor,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    BitBlt(d->hDC, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// ---------------------------------------------------------------------------
// Main window helpers
// ---------------------------------------------------------------------------

void SetStatus(const wstring& s, int kind = 0) {
    g_statusText = s;
    g_statusKind = kind;
    if (g_main) InvalidateRect(g_main, nullptr, FALSE);
}

void UpdatePrinterLabel() {
    if (g_main) InvalidateRect(g_main, nullptr, FALSE);
}

wstring Today() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t b[32];
    swprintf(b, 32, L"%02d/%02d/%04d", t.wDay, t.wMonth, t.wYear);
    return b;
}

void ReadTexts(wstring* texts) {
    for (int i = 0; i < NF; ++i) texts[i] = Trim(GetText(g_edit[i]));
}

int ReadInt(HWND h, int def, int lo, int hi) {
    wstring s = Trim(GetText(h));
    double v;
    int n = ParseNumber(s, v) ? (int)v : def;
    return n < lo ? lo : (n > hi ? hi : n);
}

void SaveState() {
    IniSet(L"sender", L"name", IniEncode(GetText(g_edit[F_SNAME])));
    IniSet(L"sender", L"phone", IniEncode(GetText(g_edit[F_SPHONE])));
    IniSet(L"sender", L"address", IniEncode(GetText(g_edit[F_SADDR])));
    IniSet(L"state", L"waybill_no", Trim(GetText(g_waybillNo)));
    IniSet(L"state", L"copies", Num(ReadInt(g_copies, 1, 1, 20), 0));
    IniSet(L"printer", L"name", g_printer);
    g_lay.fontPt = ReadInt(g_fontSize, g_lay.fontPt, 6, 24);
    IniSet(L"layout", L"font_size", Num(g_lay.fontPt, 0));
}

// Keeps leading zeros: 00461202 -> 00461203.
wstring NextWaybill(const wstring& cur) {
    wstring s = Trim(cur);
    if (s.empty()) return s;
    for (wchar_t c : s)
        if (c < L'0' || c > L'9') return s;
    wstring r = s;
    int i = (int)r.size() - 1;
    for (; i >= 0; --i) {
        if (r[i] == L'9') r[i] = L'0';
        else { ++r[i]; break; }
    }
    if (i < 0) r = L"1" + r;
    return r;
}

void ClearReceiver() {
    for (int i : {F_RNAME, F_RPHONE, F_RCITY, F_RSTREET, F_PIECES, F_NOTES, F_REF, F_VALUE})
        SetWindowTextW(g_edit[i], L"");
    SetWindowTextW(g_edit[F_DATE], Today().c_str());
    SetWindowTextW(g_edit[F_PIECES], L"1");
    SetFocus(g_waybillNo);
    SendMessageW(g_waybillNo, EM_SETSEL, 0, -1);
}

void DoPrint() {
    wstring texts[NF];
    ReadTexts(texts);
    if (texts[F_RNAME].empty() && texts[F_RPHONE].empty()) {
        if (MessageBoxW(g_main, L"اسم وهاتف المستلم فاضيين. بدك تطبع هيك؟", L"تأكيد",
                        MB_YESNO | MB_ICONQUESTION | MB_RTLREADING | MB_RIGHT) != IDYES)
            return;
    }
    SaveState();
    int copies = ReadInt(g_copies, 1, 1, 20);
    wstring err;
    SetStatus(L"جاري الطباعة...");
    if (!PrintPages(g_lay, texts, copies, false, err)) {
        SetStatus(err, 2);
        Msg(g_main, err, MB_ICONERROR);
        return;
    }

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t ts[32];
    swprintf(ts, 32, L"%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    wstring waybill = Trim(GetText(g_waybillNo));
    vector<wstring> rec(R_COUNT);
    rec[R_TIME] = ts;
    rec[R_WAYBILL] = waybill;
    rec[R_SNAME] = texts[F_SNAME];
    rec[R_SPHONE] = texts[F_SPHONE];
    rec[R_SADDR] = texts[F_SADDR];
    rec[R_RNAME] = texts[F_RNAME];
    rec[R_RPHONE] = texts[F_RPHONE];
    rec[R_RCITY] = texts[F_RCITY];
    rec[R_RSTREET] = texts[F_RSTREET];
    rec[R_PIECES] = texts[F_PIECES];
    rec[R_VALUE] = texts[F_VALUE];
    rec[R_REF] = texts[F_REF];
    rec[R_NOTES] = texts[F_NOTES];
    rec[R_COPIES] = std::to_wstring(copies);
    AppendRecord(rec);
    bool xl = BuildXlsx();

    SetWindowTextW(g_waybillNo, NextWaybill(waybill).c_str());
    SaveState();
    wstring msg = L"تمت الطباعة";
    if (!waybill.empty()) msg += L" (بوليصة رقم " + waybill + L")";
    msg += L" — عدد النسخ: " + std::to_wstring(copies);
    msg += xl ? L" — وانحفظت بسجل الإكسل." : L" — انحفظت بالسجل، بس ملف الإكسل مفتوح: سكّره وبيتحدث مع الطباعة الجاية.";
    SetStatus(msg, xl ? 1 : 0);
}

void DoTestPage(const Layout& lay, HWND owner) {
    wstring texts[NF];
    ReadTexts(texts);
    wstring err;
    if (PrintPages(lay, texts, 1, true, err))
        SetStatus(L"انطبعت صفحة التجربة: قارن المربعات المطبوعة مع مربعات البوليصة.", 1);
    else
        Msg(owner, err, MB_ICONERROR);
}

void ChoosePrinter() {
    PRINTDLGW pd = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = g_main;
    pd.Flags = PD_PRINTSETUP;
    if (!PrintDlgW(&pd)) return;
    if (pd.hDevNames) {
        DEVNAMES* dn = (DEVNAMES*)GlobalLock(pd.hDevNames);
        g_printer = (const wchar_t*)dn + dn->wDeviceOffset;
        GlobalUnlock(pd.hDevNames);
    }
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    UpdatePrinterLabel();
    SaveState();
}

void OpenExcel() {
    if (GetFileAttributesW(g_dataFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Msg(g_main, L"لسا ما في ولا بوليصة مطبوعة بالسجل.");
        return;
    }
    if (!BuildXlsx() && GetFileAttributesW(g_xlsx.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Msg(g_main, L"ما قدرت أعمل ملف الإكسل.", MB_ICONERROR);
        return;
    }
    ShellExecuteW(g_main, L"open", g_xlsx.c_str(), nullptr, g_dir.c_str(), SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// Mouse layout editor
// ---------------------------------------------------------------------------

struct Editor {
    Layout lay;
    int sel = -1;
    enum { NONE, MOVE_FIELD, RESIZE_FIELD, MOVE_ALL } drag = NONE;
    POINT start{};
    Box startBox{};
    double startOffX = 0, startOffY = 0;
    double s = 1, ox = 0, oy = 0;
    bool dirty = false;
    HWND status, save, cancel, test, reset, lblW, editW, lblH, editH, apply, lblRot, rot;
} ed;

const int kPanel = 100;

double Round05(double v) { return std::round(v * 2) / 2; }

wstring Describe(double v, const wchar_t* pos, const wchar_t* neg) {
    if (std::fabs(v) < 0.05) return L"0";
    return Num(std::fabs(v)) + L" ملم " + (v > 0 ? pos : neg);
}

void EdUpdateStatus() {
    wstring s = L"إزاحة كل الكلام: أفقي " + Describe(ed.lay.offX, L"يمين", L"يسار") +
                L"  ،  عمودي " + Describe(ed.lay.offY, L"لتحت", L"لفوق");
    if (ed.sel >= 0) {
        const Box& b = ed.lay.box[ed.sel];
        s += L"     |     الخانة المحددة: " + wstring(kFields[ed.sel].label) + L"  (المكان " + Num(b.x) + L" × " +
             Num(b.y) + L" ملم ، الحجم " + Num(b.w) + L" × " + Num(b.h) + L" ملم)";
    }
    s += L"\r\nاسحب أي خانة لتحريكها • اسحب المربع الأحمر بزاويتها لتكبيرها • اسحب من مكان فاضي لتحريك كل الكلام مع بعض • "
         L"الأسهم بالكيبورد = 0.5 ملم (مع Shift = 2 ملم)";
    SetWindowTextW(ed.status, s.c_str());
}

void EdChanged(HWND h) {
    ed.dirty = true;
    EdUpdateStatus();
    RECT rc;
    GetClientRect(h, &rc);
    rc.bottom -= S(kPanel);
    InvalidateRect(h, &rc, FALSE);
}

void EdLayout(HWND h) {
    RECT rc;
    GetClientRect(h, &rc);
    int cw = rc.right, top = rc.bottom - S(kPanel);
    MoveWindow(ed.status, S(10), top + S(6), cw - S(20), S(40), TRUE);
    int x = cw - S(10), y = top + S(52);
    auto place = [&](HWND w, int width, int height = 34) {
        x -= S(width);
        MoveWindow(w, x, y, S(width), S(height), TRUE);
        x -= S(6);
    };
    place(ed.save, 90); place(ed.cancel, 80); place(ed.test, 110); place(ed.reset, 125);
    x -= S(16);
    place(ed.lblW, 115);
    x -= S(50); MoveWindow(ed.editW, x, y + S(5), S(50), S(24), TRUE); x -= S(6);
    place(ed.lblH, 85);
    x -= S(50); MoveWindow(ed.editH, x, y + S(5), S(50), S(24), TRUE); x -= S(6);
    place(ed.apply, 65);
    x -= S(16);
    place(ed.lblRot, 110);
    x -= S(190);
    MoveWindow(ed.rot, x, y + S(4), S(190), S(200), TRUE);
}

void EdComputeScale(HWND h) {
    RECT rc;
    GetClientRect(h, &rc);
    double cw = rc.right, ch = rc.bottom - S(kPanel);
    double m = S(24);
    ed.s = std::min((cw - 2 * m) / ed.lay.paperW, (ch - 2 * m) / ed.lay.paperH);
    if (ed.s < 0.5) ed.s = 0.5;
    ed.ox = (cw - ed.lay.paperW * ed.s) / 2;
    ed.oy = (ch - ed.lay.paperH * ed.s) / 2;
}

void EdPaint(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    int cw = rc.right, ch = rc.bottom - S(kPanel);
    if (cw > 0 && ch > 0) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cw, ch);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        RECT canvas = {0, 0, cw, ch};
        HBRUSH bg = CreateSolidBrush(RGB(62, 52, 80));
        FillRect(mem, &canvas, bg);
        DeleteObject(bg);

        EdComputeScale(h);
        RECT paper = {(LONG)lround(ed.ox), (LONG)lround(ed.oy), (LONG)lround(ed.ox + ed.lay.paperW * ed.s),
                      (LONG)lround(ed.oy + ed.lay.paperH * ed.s)};
        FillRect(mem, &paper, (HBRUSH)GetStockObject(WHITE_BRUSH));
        if (g_formBmp) {
            BITMAP bm;
            GetObject(g_formBmp, sizeof(bm), &bm);
            HDC src = CreateCompatibleDC(mem);
            HGDIOBJ o = SelectObject(src, g_formBmp);
            SetStretchBltMode(mem, HALFTONE);
            SetBrushOrgEx(mem, 0, 0, nullptr);
            StretchBlt(mem, paper.left, paper.top, paper.right - paper.left, paper.bottom - paper.top, src, 0, 0,
                       bm.bmWidth, bm.bmHeight, SRCCOPY);
            SelectObject(src, o);
            DeleteDC(src);
            // Fade the photo so the typed text stands out.
            HDC white = CreateCompatibleDC(mem);
            HBITMAP wb = CreateCompatibleBitmap(mem, 1, 1);
            HGDIOBJ wo = SelectObject(white, wb);
            SetPixel(white, 0, 0, RGB(255, 255, 255));
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 110, 0};
            AlphaBlend(mem, paper.left, paper.top, paper.right - paper.left, paper.bottom - paper.top, white, 0, 0, 1, 1, bf);
            SelectObject(white, wo);
            DeleteObject(wb);
            DeleteDC(white);
        }

        wstring texts[NF];
        ReadTexts(texts);
        RenderOpts o;
        o.boxes = true;
        o.placeholders = true;
        o.screen = true;
        o.selected = ed.sel;
        Render(mem, ed.lay, texts, ed.s, ed.ox, ed.oy, o);

        BitBlt(dc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
    RECT panel = {0, ch, cw, rc.bottom};
    FillRect(dc, &panel, g_bgBrush);
    EndPaint(h, &ps);
}

int EdHitField(POINT p) {
    for (int i = NF - 1; i >= 0; --i) {
        RECT r = FieldRect(ed.lay, i, ed.s, ed.ox, ed.oy);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

bool EdHitHandle(POINT p) {
    if (ed.sel < 0) return false;
    RECT r = FieldRect(ed.lay, ed.sel, ed.s, ed.ox, ed.oy);
    int k = S(8);
    return std::abs(p.x - r.right) <= k && std::abs(p.y - r.bottom) <= k;
}

void EdFillSize() {
    SetWindowTextW(ed.editW, Num(ed.lay.paperW / 10).c_str());
    SetWindowTextW(ed.editH, Num(ed.lay.paperH / 10).c_str());
}

bool EdSave() {
    g_lay = ed.lay;
    g_lay.fontPt = ReadInt(g_fontSize, g_lay.fontPt, 6, 24);
    SaveLayout(g_lay);
    ed.dirty = false;
    SetStatus(L"انحفظت مواقع الخانات.", 1);
    RedrawWindow(g_main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    return true;
}

LRESULT CALLBACK EditorProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
            HWND w = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h,
                                     (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(w, WM_SETFONT, (WPARAM)g_fLabel, TRUE);
            return w;
        };
        ed.status = mk(L"STATIC", L"", SS_RIGHT, 0, WS_EX_RTLREADING);
        ed.save = MakeButton(h, L"حفظ", IDC_ED_SAVE, BTN_PRIMARY, Theme::Bg);
        ed.cancel = MakeButton(h, L"إلغاء", IDC_ED_CANCEL, BTN_SECONDARY, Theme::Bg);
        ed.test = MakeButton(h, L"طباعة تجربة", IDC_ED_TEST, BTN_SECONDARY, Theme::Bg);
        ed.reset = MakeButton(h, L"رجوع للأصل", IDC_ED_RESET, BTN_SECONDARY, Theme::Bg);
        ed.lblW = mk(L"STATIC", L"عرض الورقة (سم):", SS_RIGHT | SS_CENTERIMAGE, 0, WS_EX_RTLREADING);
        ed.editW = mk(L"EDIT", L"", ES_CENTER | WS_BORDER, 0);
        ed.lblH = mk(L"STATIC", L"الطول (سم):", SS_RIGHT | SS_CENTERIMAGE, 0, WS_EX_RTLREADING);
        ed.editH = mk(L"EDIT", L"", ES_CENTER | WS_BORDER, 0);
        ed.apply = MakeButton(h, L"تطبيق", IDC_ED_APPLY, BTN_SMALL, Theme::Bg);
        ed.lblRot = mk(L"STATIC", L"اتجاه دخول الورقة:", SS_RIGHT | SS_CENTERIMAGE, 0, WS_EX_RTLREADING);
        ed.rot = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_ED_ROTATE, WS_EX_RTLREADING);
        SendMessageW(ed.rot, CB_ADDSTRING, 0, (LPARAM)L"عادي (الجهة العريضة)");
        SendMessageW(ed.rot, CB_ADDSTRING, 0, (LPARAM)L"مدوّر 90° (الجهة الضيقة)");
        SendMessageW(ed.rot, CB_ADDSTRING, 0, (LPARAM)L"مدوّر 270° (الجهة الضيقة)");
        SendMessageW(ed.rot, CB_SETCURSEL, ed.lay.rotate == 90 ? 1 : ed.lay.rotate == 270 ? 2 : 0, 0);
        EdFillSize();
        EdLayout(h);
        EdUpdateStatus();
        return 0;
    }
    case WM_SIZE:
        EdLayout(h);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        EdPaint(h);
        return 0;
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wp, Theme::Bg);
        SetTextColor((HDC)wp, Theme::Text);
        return (LRESULT)g_bgBrush;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && (HWND)wp == h) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(h, &p);
            RECT rc;
            GetClientRect(h, &rc);
            if (p.y < rc.bottom - S(kPanel)) {
                SetCursor(LoadCursor(nullptr, EdHitHandle(p) ? IDC_SIZENWSE : IDC_SIZEALL));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN: {
        SetFocus(h);
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
        RECT rc;
        GetClientRect(h, &rc);
        if (p.y >= rc.bottom - S(kPanel)) break;
        if (EdHitHandle(p)) ed.drag = Editor::RESIZE_FIELD;
        else if ((ed.sel = EdHitField(p)) >= 0) ed.drag = Editor::MOVE_FIELD;
        else ed.drag = Editor::MOVE_ALL;
        ed.start = p;
        if (ed.sel >= 0) ed.startBox = ed.lay.box[ed.sel];
        ed.startOffX = ed.lay.offX;
        ed.startOffY = ed.lay.offY;
        SetCapture(h);
        EdUpdateStatus();
        rc.bottom -= S(kPanel);
        InvalidateRect(h, &rc, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (ed.drag != Editor::NONE) {
            POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
            double dx = (p.x - ed.start.x) / ed.s, dy = (p.y - ed.start.y) / ed.s;
            if (ed.drag == Editor::MOVE_ALL) {
                ed.lay.offX = Round05(ed.startOffX + dx);
                ed.lay.offY = Round05(ed.startOffY + dy);
            } else if (ed.drag == Editor::MOVE_FIELD) {
                ed.lay.box[ed.sel].x = Round05(ed.startBox.x + dx);
                ed.lay.box[ed.sel].y = Round05(ed.startBox.y + dy);
            } else {
                ed.lay.box[ed.sel].w = std::max(4.0, Round05(ed.startBox.w + dx));
                ed.lay.box[ed.sel].h = std::max(3.0, Round05(ed.startBox.h + dy));
            }
            EdChanged(h);
        }
        return 0;
    case WM_LBUTTONUP:
        if (ed.drag != Editor::NONE) {
            ed.drag = Editor::NONE;
            ReleaseCapture();
        }
        return 0;
    case WM_KEYDOWN: {
        double step = GetKeyState(VK_SHIFT) < 0 ? 2.0 : 0.5, dx = 0, dy = 0;
        switch (wp) {
            case VK_LEFT: dx = -step; break;
            case VK_RIGHT: dx = step; break;
            case VK_UP: dy = -step; break;
            case VK_DOWN: dy = step; break;
            case VK_ESCAPE: ed.sel = -1; EdUpdateStatus(); InvalidateRect(h, nullptr, FALSE); return 0;
            default: return 0;
        }
        if (ed.sel >= 0) { ed.lay.box[ed.sel].x += dx; ed.lay.box[ed.sel].y += dy; }
        else { ed.lay.offX += dx; ed.lay.offY += dy; }
        EdChanged(h);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_ED_SAVE:
            EdSave();
            DestroyWindow(h);
            return 0;
        case IDC_ED_CANCEL:
            ed.dirty = false;
            DestroyWindow(h);
            return 0;
        case IDC_ED_TEST:
            DoTestPage(ed.lay, h);
            SetFocus(h);
            return 0;
        case IDC_ED_RESET:
            if (MessageBoxW(h, L"ترجع كل الخانات لمكانها الأصلي وتلغي الإزاحة؟", L"تأكيد",
                            MB_YESNO | MB_ICONQUESTION | MB_RTLREADING | MB_RIGHT) == IDYES) {
                ResetBoxes(ed.lay);
                ed.sel = -1;
                EdChanged(h);
            }
            SetFocus(h);
            return 0;
        case IDC_ED_APPLY: {
            double w, hh;
            if (!ParseNumber(GetText(ed.editW), w) || !ParseNumber(GetText(ed.editH), hh) ||
                w < 5 || w > 50 || hh < 5 || hh > 50) {
                Msg(h, L"اكتب العرض والطول بالسنتيمتر، مثلاً 24 و 11.2", MB_ICONWARNING);
                return 0;
            }
            double kx = w * 10 / ed.lay.paperW, ky = hh * 10 / ed.lay.paperH;
            for (Box& b : ed.lay.box) { b.x *= kx; b.w *= kx; b.y *= ky; b.h *= ky; }
            ed.lay.paperW = w * 10;
            ed.lay.paperH = hh * 10;
            EdChanged(h);
            SetFocus(h);
            return 0;
        }
        case IDC_ED_ROTATE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int i = (int)SendMessageW(ed.rot, CB_GETCURSEL, 0, 0);
                ed.lay.rotate = i == 1 ? 90 : i == 2 ? 270 : 0;
                ed.dirty = true;
                SetFocus(h);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        if (ed.dirty) {
            int r = MessageBoxW(h, L"بدك تحفظ التعديلات على المواقع؟", L"حفظ",
                                MB_YESNOCANCEL | MB_ICONQUESTION | MB_RTLREADING | MB_RIGHT);
            if (r == IDCANCEL) return 0;
            if (r == IDYES) EdSave();
        }
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        EnableWindow(g_main, TRUE);
        SetForegroundWindow(g_main);
        g_editor = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void OpenEditor() {
    if (g_editor) { SetForegroundWindow(g_editor); return; }
    ed = Editor();
    ed.lay = g_lay;
    ed.lay.fontPt = ReadInt(g_fontSize, g_lay.fontPt, 6, 24);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT r = {0, 0, S(1180), S(660)};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int w = std::min<int>(r.right - r.left, work.right - work.left);
    int hgt = std::min<int>(r.bottom - r.top, work.bottom - work.top);
    g_editor = CreateWindowExW(0, L"WaybillEditor", L"المعاينة وضبط مواقع الكتابة بالماوس",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - hgt) / 2,
                               w, hgt, g_main, nullptr, g_inst, nullptr);
    EnableWindow(g_main, FALSE);
    ShowWindow(g_editor, SW_SHOW);
    SetFocus(g_editor);
}

// ---------------------------------------------------------------------------
// Main window: everything except the inputs and buttons is painted, and the
// layout runs right to left from fixed 96-dpi coordinates.
// ---------------------------------------------------------------------------


const int kMainW = 1100, kMainH = 690;
const int kCardY = 76, kCardH = 454, kCardW = 340, kCardGap = 16, kMargin = 24;
const int kBarY = kCardY + kCardH + 12, kActY = kBarY + 54 + 12;

struct Label { RECT rc; wstring text; HFONT* font; COLORREF color; UINT fmt; };
struct Input { HWND edit; RECT box; };

vector<Label> g_labels;
vector<Input> g_inputs;
HWND g_focus;
HICON g_icon;

// Rect measured from the right edge of the window, in 96-dpi units.
RECT RR(int xr, int y, int w, int h) {
    int W = S(kMainW);
    return {W - S(xr) - S(w), S(y), W - S(xr), S(y) + S(h)};
}

int CardX(int i) { return kMargin + i * (kCardW + kCardGap); }

void AddLabel(int xr, int y, int w, int h, const wstring& text, HFONT* font = &g_fLabel,
              COLORREF c = Theme::Muted, UINT fmt = DT_RIGHT | DT_VCENTER | DT_SINGLELINE) {
    g_labels.push_back({RR(xr, y, w, h), text, font, c, fmt});
}

int TextWidth(HFONT f, const wstring& t) {
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, f);
    RECT r = {0, 0, 0, 0};
    DrawTextW(dc, t.c_str(), -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return r.right - r.left;
}

HWND AddInput(HWND parent, int xr, int y, int w, int h, int id, bool multi, bool rtl, DWORD extra = 0,
              HFONT font = g_fInput) {
    RECT box = RR(xr, y, w, h);
    DWORD st = WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra |
               (multi ? (ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL) : ES_AUTOHSCROLL);
    HWND e = CreateWindowExW(rtl ? WS_EX_LAYOUTRTL : 0, L"EDIT", L"", st, 0, 0, 10, 10, parent,
                             (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(e, WM_SETFONT, (WPARAM)font, TRUE);
    // Borderless edit centred inside a painted rounded box.
    HDC dc = GetDC(e);
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(e, dc);
    int padX = S(10), lineH = tm.tmHeight + S(2);
    RECT er = box;
    if (multi) {
        InflateRect(&er, -padX, -S(8));
    } else {
        er.left += padX;
        er.right -= padX;
        er.top = box.top + (box.bottom - box.top - lineH) / 2;
        er.bottom = er.top + lineH;
    }
    MoveWindow(e, er.left, er.top, er.right - er.left, er.bottom - er.top, FALSE);
    g_inputs.push_back({e, box});
    return e;
}

// A field as named on the form: Arabic on the right, English on the left,
// and the input box under them. One row is 22 + boxH.
HWND AddField(HWND parent, int xr, int y, int w, int field, int boxH = 38, bool rtl = true) {
    const FieldDef& f = kFields[field];
    AddLabel(xr, y, w, 22, f.label, &g_fLabel, Theme::Text);
    int arW = (int)lround(TextWidth(g_fLabel, f.label) * 96.0 / g_dpi / g_fit) + 12;
    if (arW < w - 30)
        AddLabel(xr + arW, y, w - arW, 22, f.en, &g_fEn, Theme::Muted,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    g_edit[field] = AddInput(parent, xr, y + 22, w, boxH, IDC_EDIT_BASE + field, f.multi, rtl);
    return g_edit[field];
}

void PlaceButton(HWND b, RECT r) { MoveWindow(b, r.left, r.top, r.right - r.left, r.bottom - r.top, FALSE); }

bool SegmentSelected(int id) {
    return (id == IDC_ROT_LONG && g_lay.rotate == 90) || (id == IDC_ROT_FLIP && g_lay.rotate == 270) ||
           (id == IDC_ROT_WIDE && g_lay.rotate == 0);
}

void BuildMain(HWND h) {
    const int in = 16, fw = kCardW - 2 * in, y0 = kCardY + 62, row = 70, bottom = kCardY + kCardH;

    // Waybill number from the barcode, in the header (a scanner can type it in).
    g_waybillNo = AddInput(h, kMainW - kMargin - 210, 11, 210, 38, IDC_WAYBILL, false, false, ES_CENTER, g_fWaybill);

    // 1. Sender
    int x = CardX(0) + in;
    AddField(h, x, y0, fw, F_SNAME);
    AddField(h, x, y0 + row, fw, F_SPHONE, 38, false);
    AddField(h, x, y0 + 2 * row, fw, F_SADDR, bottom - 40 - (y0 + 2 * row + 22));
    AddLabel(x + 26, bottom - 37, fw - 26, 24, L"بيانات المرسل بتنحفظ تلقائياً", &g_fSmall);

    // 3. Receiver
    x = CardX(1) + in;
    AddField(h, x, y0, fw, F_RNAME);
    AddField(h, x, y0 + row, fw, F_RPHONE, 38, false);
    AddField(h, x, y0 + 2 * row, fw, F_RCITY);
    AddField(h, x, y0 + 3 * row, fw, F_RSTREET, bottom - in - (y0 + 3 * row + 22));

    // 5. Shipment
    x = CardX(2) + in;
    int half = (fw - 12) / 2;
    AddField(h, x, y0, half, F_PIECES, 38, false);
    AddField(h, x + half + 12, y0, half, F_DATE, 38, false);
    AddField(h, x, y0 + row, fw, F_REF, 38, false);
    AddField(h, x, y0 + 2 * row, fw, F_VALUE, 38, false);
    AddField(h, x, y0 + 3 * row, fw, F_NOTES, bottom - in - (y0 + 3 * row + 22));

    // Print settings bar
    const int sy = kBarY + 11, bx = kMargin + 16;
    AddLabel(bx, sy, 72, 32, L"حجم الخط", &g_fLabel, Theme::Text);
    PlaceButton(MakeButton(h, L"+", IDC_FONT_INC, BTN_SMALL, Theme::Card), RR(bx + 76, sy, 32, 32));
    g_fontSize = AddInput(h, bx + 112, sy, 48, 32, IDC_FONTSIZE, false, false, ES_NUMBER | ES_CENTER);
    PlaceButton(MakeButton(h, L"−", IDC_FONT_DEC, BTN_SMALL, Theme::Card), RR(bx + 164, sy, 32, 32));

    AddLabel(bx + 220, sy, 76, 32, L"عدد النسخ", &g_fLabel, Theme::Text);
    PlaceButton(MakeButton(h, L"+", IDC_COPY_INC, BTN_SMALL, Theme::Card), RR(bx + 300, sy, 32, 32));
    g_copies = AddInput(h, bx + 336, sy, 48, 32, IDC_COPIES, false, false, ES_NUMBER | ES_CENTER);
    PlaceButton(MakeButton(h, L"−", IDC_COPY_DEC, BTN_SMALL, Theme::Card), RR(bx + 388, sy, 32, 32));

    AddLabel(bx + 444, sy, 100, 32, L"الورقة بتدخل:", &g_fLabel, Theme::Text);
    PlaceButton(MakeButton(h, L"بالطول", IDC_ROT_LONG, BTN_SEGMENT, Theme::Card), RR(bx + 548, sy, 78, 32));
    PlaceButton(MakeButton(h, L"بالطول مقلوب", IDC_ROT_FLIP, BTN_SEGMENT, Theme::Card), RR(bx + 630, sy, 112, 32));
    PlaceButton(MakeButton(h, L"بالعرض", IDC_ROT_WIDE, BTN_SEGMENT, Theme::Card), RR(bx + 746, sy, 78, 32));

    PlaceButton(MakeButton(h, L"تغيير الطابعة", IDC_PRINTER, BTN_SECONDARY, Theme::Card),
                RR(kMainW - bx - 130, sy, 130, 32));

    // Actions
    PlaceButton(MakeButton(h, L"طباعة البوليصة", IDC_PRINT, BTN_PRIMARY, Theme::Bg), RR(kMargin, kActY, 240, 46));
    struct Btn { const wchar_t* text; int id; } btns[] = {
        {L"ضبط المواقع بالماوس", IDC_EDITOR}, {L"طباعة تجربة", IDC_TEST},
        {L"بوليصة جديدة", IDC_CLEAR}, {L"سجل الإكسل", IDC_EXCEL},
    };
    for (int i = 0; i < 4; ++i)
        PlaceButton(MakeButton(h, btns[i].text, btns[i].id, BTN_SECONDARY, Theme::Bg),
                    RR(kMargin + 252 + i * 200, kActY, 188, 46));

    // Initial values
    SetWindowTextW(g_edit[F_SNAME], IniDecode(IniGet(L"sender", L"name")).c_str());
    SetWindowTextW(g_edit[F_SPHONE], IniDecode(IniGet(L"sender", L"phone")).c_str());
    SetWindowTextW(g_edit[F_SADDR], IniDecode(IniGet(L"sender", L"address")).c_str());
    SetWindowTextW(g_waybillNo, IniGet(L"state", L"waybill_no").c_str());
    SetWindowTextW(g_fontSize, Num(g_lay.fontPt, 0).c_str());
    wstring copies = IniGet(L"state", L"copies");
    SetWindowTextW(g_copies, copies.empty() ? L"1" : copies.c_str());
    SetWindowTextW(g_edit[F_DATE], Today().c_str());
    SetWindowTextW(g_edit[F_PIECES], L"1");
    SetStatus(L"اكتب رقم البوليصة، عبّي البيانات، واضغط \"طباعة البوليصة\".");
}

void PaintMain(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    FillRect(mem, &cr, g_bgBrush);

    RECT bar = RR(kMargin, kBarY, kMainW - 2 * kMargin, 54);
    RECT status = {0, cr.bottom - S(28), cr.right, cr.bottom};
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // Header with a gradient and an orange underline.
        Gdiplus::LinearGradientBrush hb(Gdiplus::Point(0, 0), Gdiplus::Point(cr.right, 0), GC(Theme::PurpleLight),
                                        GC(Theme::PurpleDark));
        g.FillRectangle(&hb, 0, 0, (INT)cr.right, (INT)S(60));
        Gdiplus::SolidBrush orange(GC(Theme::Orange));
        g.FillRectangle(&orange, 0, (INT)S(60), (INT)cr.right, (INT)S(4));

        auto card = [&](RECT r) {
            RECT sh = r;
            OffsetRect(&sh, 0, S(3));
            FillRound(g, sh, (float)S(14), Theme::PurpleDark, 18);
            FillRound(g, r, (float)S(14), Theme::Card);
            StrokeRound(g, r, (float)S(14), Theme::CardBorder, 1);
        };
        Gdiplus::SolidBrush purple(GC(Theme::Purple)), line(GC(Theme::CardBorder));
        for (int i = 0; i < 3; ++i) {
            int x = CardX(i);
            card(RR(x, kCardY, kCardW, kCardH));
            RECT b = RR(x + 16, kCardY + 14, 28, 28);
            g.FillEllipse(&purple, (INT)b.left, (INT)b.top, (INT)(b.right - b.left), (INT)(b.bottom - b.top));
            RECT l = RR(x + 16, kCardY + 52, kCardW - 32, 1);
            g.FillRectangle(&line, (INT)l.left, (INT)l.top, (INT)(l.right - l.left), 1);
        }
        RECT tip = RR(CardX(0) + 16, kCardY + kCardH - 34, 18, 18);
        g.FillEllipse(&orange, (INT)tip.left, (INT)tip.top, (INT)(tip.right - tip.left), (INT)(tip.bottom - tip.top));
        card(bar);

        // Input boxes; the focused one gets a ring (orange on the purple header).
        for (const Input& in : g_inputs) {
            bool focus = in.edit == g_focus, onHeader = in.edit == g_waybillNo;
            if (focus) {
                RECT ring = in.box;
                InflateRect(&ring, S(3), S(3));
                FillRound(g, ring, (float)S(11), onHeader ? Theme::Orange : Theme::Purple, onHeader ? 200 : 40);
            }
            FillRound(g, in.box, (float)S(8), RGB(255, 255, 255));
            if (!onHeader)
                StrokeRound(g, in.box, (float)S(8), focus ? Theme::Purple : Theme::InputBorder, focus ? 1.8f : 1.2f);
        }

        Gdiplus::SolidBrush sb(GC(Theme::StatusBg));
        g.FillRectangle(&sb, 0, (INT)status.top, (INT)cr.right, (INT)(status.bottom - status.top));
        COLORREF dotColor = g_statusKind == 1 ? Theme::Good : g_statusKind == 2 ? Theme::Bad : Theme::Purple;
        Gdiplus::SolidBrush db(GC(dotColor));
        g.FillEllipse(&db, (INT)(cr.right - S(kMargin) - S(10)), (INT)(status.top + S(9)), (INT)S(10), (INT)S(10));
    }

    // Header
    if (g_icon) {
        RECT ir = RR(kMargin, 12, 36, 36);
        DrawIconEx(mem, ir.left, ir.top, g_icon, S(36), S(36), 0, nullptr, DI_NORMAL);
    }
    PaintText(mem, L"طباعة بوليصة الشحن", RR(kMargin + 48, 5, 420, 32), g_fTitle, RGB(255, 255, 255));
    PaintText(mem, L"Domestic Waybill  •  عبّي البيانات وبتنطبع بمكانها على البوليصة", RR(kMargin + 48, 35, 560, 20),
              g_fSub, Theme::HeaderSub);
    PaintText(mem, L"رقم البوليصة:", RR(kMainW - kMargin - 210 - 140, 11, 130, 38), g_fCard, RGB(255, 255, 255),
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Card titles: number, Arabic, English, as on the form.
    const wchar_t* titles[3] = {L"معلومات المرسل", L"معلومات المستلم", L"معلومات الشحنة"};
    const wchar_t* english[3] = {L"FROM (SHIPPER)", L"TO (RECEIVER)", L"SHIPMENT INFORMATION"};
    const wchar_t* numbers[3] = {L"1", L"3", L"5"};
    for (int i = 0; i < 3; ++i) {
        int x = CardX(i);
        PaintText(mem, numbers[i], RR(x + 16, kCardY + 14, 28, 28), g_fButton, RGB(255, 255, 255),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        PaintText(mem, titles[i], RR(x + 52, kCardY + 12, 134, 32), g_fCard, Theme::Text);
        PaintText(mem, english[i], RR(x + 190, kCardY + 12, kCardW - 206, 32), g_fEn, Theme::Purple,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    PaintText(mem, L"i", RR(CardX(0) + 16, kCardY + kCardH - 34, 18, 18), g_fSmall, RGB(255, 255, 255),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    for (const Label& l : g_labels) PaintText(mem, l.text, l.rc, *l.font, l.color, l.fmt);

    // Status on the right, printer on the left.
    wstring printer = CurrentPrinter();
    RECT pr = {S(kMargin), status.top, S(kMargin) + S(330), status.bottom};
    PaintText(mem, L"الطابعة: " + (printer.empty() ? wstring(L"غير محددة") : printer), pr, g_fSmall, Theme::Muted,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT st = {pr.right + S(12), status.top, cr.right - S(kMargin + 18), status.bottom};
    PaintText(mem, g_statusText, st, g_fLabel,
              g_statusKind == 2 ? Theme::Bad : g_statusKind == 1 ? Theme::Good : Theme::Text,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

void Step(HWND edit, int delta, int lo, int hi) {
    int v = ReadInt(edit, lo, lo, hi) + delta;
    v = v < lo ? lo : (v > hi ? hi : v);
    SetWindowTextW(edit, std::to_wstring(v).c_str());
}

void SetRotation(HWND h, int rotate) {
    g_lay.rotate = rotate;
    IniSet(L"layout", L"rotate", Num(rotate, 0));
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    SetStatus(rotate == 0 ? L"الورقة بتدخل بالعرض." :
              L"الورقة بتدخل بالطول، والكلام بينطبع مدوّر. إذا طلع مقلوب اختار الخيار الثاني.");
}

LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE:
        g_main = h;
        BuildMain(h);
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_waybillNo);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintMain(h);
        return 0;
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, (HWND)lp == g_waybillNo ? Theme::Purple : Theme::Text);
        SetBkColor((HDC)wp, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    case WM_LBUTTONDOWN: {  // a click on a box's padding focuses its input
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
        for (const Input& in : g_inputs)
            if (PtInRect(&in.box, p)) { SetFocus(in.edit); break; }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDOK) {  // Enter in a one-line box moves on, like a barcode scanner expects
            HWND f = GetFocus();
            if (f == g_waybillNo) SetFocus(g_edit[F_RNAME]);
            else if (f && IsChild(h, f)) SetFocus(GetNextDlgTabItem(h, f, FALSE));
            return 0;
        }
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
            g_focus = code == EN_SETFOCUS ? (HWND)lp : nullptr;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (code == BN_CLICKED || code == BN_DOUBLECLICKED) {
            switch (id) {
                case IDC_FONT_DEC: Step(g_fontSize, -1, 6, 24); return 0;
                case IDC_FONT_INC: Step(g_fontSize, 1, 6, 24); return 0;
                case IDC_COPY_DEC: Step(g_copies, -1, 1, 20); return 0;
                case IDC_COPY_INC: Step(g_copies, 1, 1, 20); return 0;
            }
        }
        if (code != BN_CLICKED) break;
        switch (id) {
            case IDC_PRINT: DoPrint(); return 0;
            case IDC_EDITOR: OpenEditor(); return 0;
            case IDC_TEST: SaveState(); DoTestPage(g_lay, h); return 0;
            case IDC_CLEAR: ClearReceiver(); SetStatus(L"جاهز لبوليصة جديدة. اكتب رقم البوليصة الجديدة فوق."); return 0;
            case IDC_PRINTER: ChoosePrinter(); return 0;
            case IDC_EXCEL: OpenExcel(); return 0;
            case IDC_ROT_LONG: SetRotation(h, 90); return 0;
            case IDC_ROT_FLIP: SetRotation(h, 270); return 0;
            case IDC_ROT_WIDE: SetRotation(h, 0); return 0;
        }
        break;
    }
    case WM_CLOSE:
        SaveState();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gsi, nullptr);

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    g_dir = path;
    g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/") + 1);
    g_ini = g_dir + L"waybill-settings.ini";
    g_dataFile = g_dir + L"waybill-records.txt";
    g_xlsx = g_dir + L"سجل_البوالص.xlsx";
    EnsureIni();
    LoadLayout(g_lay);
    g_printer = IniGet(L"printer", L"name");

    HDC screen = GetDC(nullptr);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT frame = {0, 0, 0, 0};
    AdjustWindowRectEx(&frame, style, FALSE, WS_EX_CONTROLPARENT);
    double needW = kMainW * g_dpi / 96.0 + (frame.right - frame.left);
    double needH = kMainH * g_dpi / 96.0 + (frame.bottom - frame.top);
    g_fit = std::min({1.0, (work.right - work.left) / needW, (work.bottom - work.top) / needH});

    g_fTitle = MakeFont(16, true);
    g_fSub = MakeFont(9.5, false);
    g_fCard = MakeFont(12, true);
    g_fLabel = MakeFont(10, true);
    g_fEn = MakeFont(8, false);
    g_fInput = MakeFont(12, false);
    g_fButton = MakeFont(10.5, true);
    g_fSmall = MakeFont(9, false);
    g_fBig = MakeFont(14, true);
    g_fWaybill = MakeFont(15, true);
    g_uiFont = g_fLabel;
    g_bgBrush = CreateSolidBrush(Theme::Bg);
    g_formBmp = (HBITMAP)LoadImageW(inst, L"FORMBMP", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    g_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(36), S(36), 0);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"WaybillMain";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    wc.lpfnWndProc = EditorProc;
    wc.lpszClassName = L"WaybillEditor";
    RegisterClassW(&wc);

    RECT r = {0, 0, S(kMainW), S(kMainH)};
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_CONTROLPARENT);
    int w = r.right - r.left, hgt = r.bottom - r.top;
    HWND h = CreateWindowExW(WS_EX_CONTROLPARENT, L"WaybillMain", L"طباعة بوليصة الشحن", style,
                             work.left + std::max(0, (int)(work.right - work.left - w) / 2),
                             work.top + std::max(0, (int)(work.bottom - work.top - hgt) / 2), w, hgt,
                             nullptr, nullptr, inst, nullptr);
    ShowWindow(h, show);
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if ((msg.hwnd == g_main || IsChild(g_main, msg.hwnd)) && IsDialogMessageW(g_main, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}
