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
    const wchar_t* label;
    bool multi;
    Align align;
    int x1, y1, x2, y2;  // writing area on the 1600x748 reference photo
};

const int kPhotoW = 1600, kPhotoH = 748;

const FieldDef kFields[] = {
    {L"date",          L"التاريخ",                  false, LEFT,   868,  50, 1022,  96},
    {L"sender_name",   L"اسم المرسل",               false, AUTO,    62, 200,  606, 233},
    {L"sender_phone",  L"هاتف المرسل",              false, AUTO,    62, 265,  606, 338},
    {L"sender_addr",   L"عنوان المرسل",             true,  AUTO,    56, 372,  606, 545},
    {L"recv_name",     L"اسم المستلم",              false, AUTO,   620, 196, 1188, 228},
    {L"recv_phone",    L"هاتف المستلم",             false, AUTO,   620, 260, 1188, 336},
    {L"recv_city",     L"المدينة / القرية",         false, AUTO,   620, 366, 1188, 438},
    {L"recv_street",   L"الشارع وأقرب معلم",        true,  AUTO,   620, 466, 1196, 546},
    {L"pieces",        L"عدد الطرود",               false, CENTER, 1200, 163, 1462, 216},
    {L"notes",         L"ملاحظات",                  true,  AUTO,  1200, 244, 1578, 384},
    {L"ref",           L"رقم المرسل المرجعي",       false, AUTO,  1202, 410, 1582, 548},
    {L"value",         L"قيمة الطلب شامل التوصيل",  false, CENTER, 1210, 582, 1590, 706},
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
    int rotate = 0;                     // 0, 90 or 270
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
HFONT g_uiFont, g_uiBold;
HBITMAP g_formBmp;

HWND g_main, g_status, g_printerLbl, g_waybillNo, g_fontSize, g_copies;
HWND g_edit[NF];
HWND g_editor;

enum {
    IDC_PRINT = 100, IDC_EDITOR, IDC_TEST, IDC_CLEAR, IDC_PRINTER, IDC_EXCEL,
    IDC_EDIT_BASE = 200,
    IDC_WAYBILL = 300, IDC_FONTSIZE, IDC_COPIES,
    IDC_ED_SAVE = 400, IDC_ED_CANCEL, IDC_ED_TEST, IDC_ED_RESET, IDC_ED_APPLY, IDC_ED_ROTATE
};

int S(int v) { return MulDiv(v, g_dpi, 96); }

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
    lay.rotate = (int)IniGetD(L"layout", L"rotate", 0);
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

// Draws text inside rc, shrinking the font until it fits.
void DrawFitted(HDC dc, const wstring& text, RECT rc, const FieldDef& f, const Layout& lay,
                double s, COLORREF color, double maxPt) {
    if (text.empty()) return;
    bool ar = HasArabic(text);
    UINT fmt = DT_NOPREFIX | (f.multi ? DT_WORDBREAK : (DT_SINGLELINE | DT_VCENTER));
    if (f.align == CENTER) fmt |= DT_CENTER;
    else if (f.align == LEFT) fmt |= DT_LEFT;
    else fmt |= ar ? DT_RIGHT : DT_LEFT;
    if (ar) fmt |= DT_RTLREADING;

    int boxW = rc.right - rc.left, boxH = rc.bottom - rc.top;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    for (double pt = maxPt;; pt -= 0.5) {
        int h = (int)lround(pt * 25.4 / 72.0 * s);
        if (h < 1) h = 1;
        HFONT font = CreateFontW(-h, 0, 0, 0, lay.bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                 DEFAULT_PITCH, lay.font.c_str());
        HGDIOBJ old = SelectObject(dc, font);
        RECT calc = {0, 0, boxW, 0};
        DrawTextW(dc, text.c_str(), -1, &calc, (fmt & ~DT_VCENTER) | DT_CALCRECT);
        bool fits = calc.right - calc.left <= boxW && calc.bottom - calc.top <= boxH;
        if (fits || pt <= 6) {
            UINT extra = (!fits && !f.multi) ? DT_END_ELLIPSIS : 0;
            DrawTextW(dc, text.c_str(), -1, &rc, fmt | extra);
        }
        SelectObject(dc, old);
        DeleteObject(font);
        if (fits || pt <= 6) break;
    }
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

    // Draw in uniform units (s0 per mm), then map to the page with rotation
    // and the printer's unprintable margin taken out.
    XFORM xf = {};
    if (lay.rotate == 90) {
        xf.eM21 = (FLOAT)(-sx / s0); xf.eDx = (FLOAT)(lay.paperH * sx - physX);
        xf.eM12 = (FLOAT)(sy / s0);  xf.eDy = (FLOAT)(-physY);
    } else if (lay.rotate == 270) {
        xf.eM21 = (FLOAT)(sx / s0);  xf.eDx = (FLOAT)(-physX);
        xf.eM12 = (FLOAT)(-sy / s0); xf.eDy = (FLOAT)(lay.paperW * sy - physY);
    } else {
        xf.eM11 = (FLOAT)(sx / s0);  xf.eDx = (FLOAT)(-physX);
        xf.eM22 = (FLOAT)(sy / s0);  xf.eDy = (FLOAT)(-physY);
    }

    DOCINFOW di = {};
    di.cbSize = sizeof(di);
    di.lpszDocName = testPage ? L"Waybill test page" : L"Waybill";
    bool ok = StartDocW(dc, &di) > 0;
    RenderOpts o;
    o.boxes = testPage;
    o.placeholders = testPage;
    for (int c = 0; ok && c < copies; ++c) {
        ok = StartPage(dc) > 0;
        if (!ok) break;
        SetGraphicsMode(dc, GM_ADVANCED);
        SetWorldTransform(dc, &xf);
        Render(dc, lay, texts, s0, 0, 0, o);
        ModifyWorldTransform(dc, nullptr, MWT_IDENTITY);
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
// Main window helpers
// ---------------------------------------------------------------------------

void SetStatus(const wstring& s) { SetWindowTextW(g_status, s.c_str()); }

void UpdatePrinterLabel() {
    wstring p = CurrentPrinter();
    SetWindowTextW(g_printerLbl, (L"الطابعة: " + (p.empty() ? wstring(L"(غير محددة)") : p)).c_str());
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
    SetFocus(g_edit[F_RNAME]);
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
        SetStatus(err);
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
    SetStatus(msg);
}

void DoTestPage(const Layout& lay, HWND owner) {
    wstring texts[NF];
    ReadTexts(texts);
    wstring err;
    if (PrintPages(lay, texts, 1, true, err))
        SetStatus(L"انطبعت صفحة التجربة: قارن المربعات المطبوعة مع مربعات البوليصة.");
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

const int kPanel = 96;

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
    int x = cw - S(10), y = top + S(54);
    auto place = [&](HWND w, int width, int height = 28) {
        x -= S(width);
        MoveWindow(w, x, y, S(width), S(height), TRUE);
        x -= S(6);
    };
    place(ed.save, 90); place(ed.cancel, 80); place(ed.test, 110); place(ed.reset, 125);
    x -= S(16);
    place(ed.lblW, 115); place(ed.editW, 50); place(ed.lblH, 85); place(ed.editH, 50); place(ed.apply, 65);
    x -= S(16);
    place(ed.lblRot, 110); place(ed.rot, 190, 200);
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
        HBRUSH bg = CreateSolidBrush(RGB(70, 72, 82));
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
    FillRect(dc, &panel, GetSysColorBrush(COLOR_BTNFACE));
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
    SetStatus(L"انحفظت مواقع الخانات.");
    return true;
}

LRESULT CALLBACK EditorProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
            HWND w = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h,
                                     (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(w, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
            return w;
        };
        ed.status = mk(L"STATIC", L"", SS_RIGHT, 0, WS_EX_RTLREADING);
        ed.save = mk(L"BUTTON", L"حفظ", BS_DEFPUSHBUTTON, IDC_ED_SAVE);
        ed.cancel = mk(L"BUTTON", L"إلغاء", BS_PUSHBUTTON, IDC_ED_CANCEL);
        ed.test = mk(L"BUTTON", L"طباعة تجربة", BS_PUSHBUTTON, IDC_ED_TEST);
        ed.reset = mk(L"BUTTON", L"رجوع للأصل", BS_PUSHBUTTON, IDC_ED_RESET);
        ed.lblW = mk(L"STATIC", L"عرض الورقة (سم):", SS_RIGHT | SS_CENTERIMAGE, 0, WS_EX_RTLREADING);
        ed.editW = mk(L"EDIT", L"", ES_CENTER | WS_BORDER, 0);
        ed.lblH = mk(L"STATIC", L"الطول (سم):", SS_RIGHT | SS_CENTERIMAGE, 0, WS_EX_RTLREADING);
        ed.editH = mk(L"EDIT", L"", ES_CENTER | WS_BORDER, 0);
        ed.apply = mk(L"BUTTON", L"تطبيق", BS_PUSHBUTTON, IDC_ED_APPLY);
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
// Main window
// ---------------------------------------------------------------------------

// The main window places controls right to left but does not mirror their
// insides, so Arabic controls opt into RTL while numbers and phones stay LTR.
HWND MakeCtl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
             int id = 0, DWORD ex = WS_EX_LAYOUTRTL, HFONT font = nullptr) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), parent,
                             (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)(font ? font : g_uiFont), TRUE);
    return c;
}

HWND MakeEdit(HWND p, int x, int y, int w, int h, int id, bool multi = false, DWORD extra = 0, bool rtl = true) {
    DWORD st = WS_TABSTOP | WS_BORDER | extra |
               (multi ? (ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL) : ES_AUTOHSCROLL);
    return MakeCtl(p, L"EDIT", L"", st, x, y, w, h, id, rtl ? WS_EX_LAYOUTRTL : 0);
}

void BuildMain(HWND h) {
    const int colW = 300, gx[3] = {12, 324, 636};
    const wchar_t* groups[3] = {L"1. بيانات المرسل", L"3. بيانات المستلم", L"5. معلومات الشحنة"};
    for (int i = 0; i < 3; ++i)
        MakeCtl(h, L"BUTTON", groups[i], BS_GROUPBOX, gx[i], 8, colW, 404, 0, WS_EX_LAYOUTRTL, g_uiBold);

    auto item = [&](int col, int y, int field, int height = 26) {
        MakeCtl(h, L"STATIC", kFields[field].label, 0, gx[col] + 14, y, colW - 28, 20);
        bool ltr = field == F_SPHONE || field == F_RPHONE || field == F_VALUE || field == F_REF;
        g_edit[field] = MakeEdit(h, gx[col] + 14, y + 20, colW - 28, height, IDC_EDIT_BASE + field, kFields[field].multi,
                                 0, !ltr);
    };
    // Sender
    item(0, 32, F_SNAME);
    item(0, 90, F_SPHONE);
    item(0, 148, F_SADDR, 120);
    MakeCtl(h, L"STATIC", L"بيانات المرسل بتنحفظ لحالها، ما في داعي تكتبها كل مرة.", 0, gx[0] + 14, 300, colW - 28, 40);
    // Receiver
    item(1, 32, F_RNAME);
    item(1, 90, F_RPHONE);
    item(1, 148, F_RCITY);
    item(1, 206, F_RSTREET, 100);
    // Shipment
    MakeCtl(h, L"STATIC", L"رقم البوليصة (للسجل فقط)", 0, gx[2] + 14, 32, colW - 28, 20);
    g_waybillNo = MakeEdit(h, gx[2] + 14, 52, colW - 28, 26, IDC_WAYBILL, false, 0, false);
    MakeCtl(h, L"STATIC", kFields[F_DATE].label, 0, gx[2] + 14, 86, 130, 20);
    g_edit[F_DATE] = MakeEdit(h, gx[2] + 14, 106, 130, 26, IDC_EDIT_BASE + F_DATE, false, 0, false);
    MakeCtl(h, L"STATIC", kFields[F_PIECES].label, 0, gx[2] + 156, 86, 130, 20);
    g_edit[F_PIECES] = MakeEdit(h, gx[2] + 156, 106, 130, 26, IDC_EDIT_BASE + F_PIECES, false, 0, false);
    item(2, 140, F_VALUE);
    item(2, 198, F_REF);
    item(2, 256, F_NOTES, 112);

    // Print settings
    MakeCtl(h, L"BUTTON", L"الطباعة", BS_GROUPBOX, 12, 418, 924, 62, 0, WS_EX_LAYOUTRTL, g_uiBold);
    MakeCtl(h, L"STATIC", L"حجم الخط:", SS_CENTERIMAGE, 26, 440, 70, 26);
    g_fontSize = MakeEdit(h, 96, 440, 56, 26, IDC_FONTSIZE, false, ES_NUMBER, false);
    MakeCtl(h, L"STATIC", L"عدد مرات الطباعة:", SS_CENTERIMAGE, 172, 440, 120, 26);
    g_copies = MakeEdit(h, 292, 440, 56, 26, IDC_COPIES, false, ES_NUMBER, false);
    auto updown = [&](HWND buddy, int lo, int hi) {
        HWND u = CreateWindowExW(0, UPDOWN_CLASSW, nullptr,
                                 WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                                 0, 0, 0, 0, h, nullptr, g_inst, nullptr);
        SendMessageW(u, UDM_SETBUDDY, (WPARAM)buddy, 0);
        SendMessageW(u, UDM_SETRANGE32, lo, hi);
    };
    updown(g_fontSize, 6, 24);
    updown(g_copies, 1, 20);
    MakeCtl(h, L"STATIC", L"(كل نسخة بتنطبع على ورقة جديدة)", SS_CENTERIMAGE, 356, 440, 210, 26);
    g_printerLbl = MakeCtl(h, L"STATIC", L"", SS_CENTERIMAGE | SS_ENDELLIPSIS, 576, 440, 350, 26);

    // Buttons
    struct Btn { const wchar_t* text; int id; } btns[] = {
        {L"طباعة البوليصة", IDC_PRINT}, {L"ضبط بالماوس", IDC_EDITOR}, {L"طباعة تجربة", IDC_TEST},
        {L"بوليصة جديدة", IDC_CLEAR}, {L"اختيار الطابعة", IDC_PRINTER}, {L"فتح سجل الإكسل", IDC_EXCEL},
    };
    for (int i = 0; i < 6; ++i)
        MakeCtl(h, L"BUTTON", btns[i].text, WS_TABSTOP | (i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                12 + i * 155, 490, 149, 40, btns[i].id, WS_EX_LAYOUTRTL, i == 0 ? g_uiBold : nullptr);
    g_status = MakeCtl(h, L"STATIC", L"", SS_CENTERIMAGE | SS_ENDELLIPSIS, 12, 538, 924, 24);

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
    UpdatePrinterLabel();
    SetStatus(L"عبّي الخانات واضغط \"طباعة البوليصة\". أول مرة جرّب \"ضبط بالماوس\" عشان تظبط المواقع.");
}

LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE:
        g_main = h;
        BuildMain(h);
        SetFocus(g_edit[F_SNAME]);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
            case IDC_PRINT: DoPrint(); return 0;
            case IDC_EDITOR: OpenEditor(); return 0;
            case IDC_TEST: SaveState(); DoTestPage(g_lay, h); return 0;
            case IDC_CLEAR: ClearReceiver(); SetStatus(L"جاهز لبوليصة جديدة."); return 0;
            case IDC_PRINTER: ChoosePrinter(); return 0;
            case IDC_EXCEL: OpenExcel(); return 0;
        }
        if (HIWORD(wp) == EN_CHANGE && g_editor) InvalidateRect(g_editor, nullptr, FALSE);
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
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
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&icc);

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
    g_uiFont = CreateFontW(-MulDiv(10, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
    g_uiBold = CreateFontW(-MulDiv(10, g_dpi, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
    g_formBmp = (HBITMAP)LoadImageW(inst, L"FORMBMP", IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(1));

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"WaybillMain";
    wc.hIcon = icon;
    RegisterClassW(&wc);
    wc.lpfnWndProc = EditorProc;
    wc.lpszClassName = L"WaybillEditor";
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, S(948), S(572)};
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT | WS_EX_CONTROLPARENT);
    HWND h = CreateWindowExW(WS_EX_LAYOUTRTL | WS_EX_NOINHERITLAYOUT | WS_EX_CONTROLPARENT, L"WaybillMain", L"طباعة بوليصة الشحن",
                             style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, inst, nullptr);
    ShowWindow(h, show);
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if ((msg.hwnd == g_main || IsChild(g_main, msg.hwnd)) && IsDialogMessageW(g_main, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
