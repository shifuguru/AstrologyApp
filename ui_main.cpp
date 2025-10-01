// Add these first, before anything else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif

#include <algorithm>
#include <chrono>
#include <date/date.h>
#include <date/tz.h>

// ui_main.cpp — Dear ImGui + Win32 + DX11 UI for AstrologyChart
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <filesystem>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// Your core header with the class + helpers:
extern "C" { 
#include "swephexp.h" 
}
#include <cmath>
#include <iostream>
#include "Gazetteer.hpp"

// ==== BEGIN: Paste the SAME helpers + AstrologyChart from your working console version ====
// Keep these declarations in a shared header if you prefer.

static inline double norm360(double x) { double y = fmod(x, 360.0); if (y < 0) y += 360.0; return y; }
struct DMS { int deg; int min; double sec; };
static DMS toDMS(double degrees) { double d = floor(degrees); double mfull = (degrees - d) * 60.0; double m = floor(mfull); double s = (mfull - m) * 60.0; return { (int)d,(int)m,s }; }
static const char* SIGN_NAMES[12] = { "Aries","Taurus","Gemini","Cancer","Leo","Virgo","Libra","Scorpio","Sagittarius","Capricorn","Aquarius","Pisces" };
static inline float deg2rad(float deg) { return deg * (float)M_PI / 180.0f; }
static inline ImVec2 polar(const ImVec2& C, float R, float angRad) {
	return ImVec2(C.x + R * cosf(angRad), C.y + R * sinf(angRad));
}

// Project ecliptic longitude to screen angle.
// We rotate the wheel so that the Ascendant (1st house cusp) is on the left (9 o'clock).
// i.e. screenAngle = (planetLon - ASC) and then rotate -90 degrees to have ASC on the left.
// Feel free to tweak to your taste.

static inline float ecl_to_screen_angle(float ecl_deg, float asc_deg)
{
	// angle of point relative to ascendant
    float a = fmodf(asc_deg - ecl_deg, 360.0f); // relative to ASC
	if (a < 0) a += 360.0f;

	// Put ASC on the left (9 o'clock)
    a += 180.0f;
	if (a >= 360.0f) a -= 360.0f;

	return deg2rad(a);
}

// Planet colors (tweak to taste)
static ImU32 colSun = IM_COL32(255, 212, 0, 255);
static ImU32 colMoon = IM_COL32(210, 210, 210, 255);
static ImU32 colMercury = IM_COL32(160, 160, 160, 255);
static ImU32 colVenus = IM_COL32(255, 140, 170, 255);
static ImU32 colMars = IM_COL32(230, 60, 60, 255);
static ImU32 colJupiter = IM_COL32(235, 170, 60, 255);
static ImU32 colSaturn = IM_COL32(160, 120, 70, 255);
static ImU32 colUranus = IM_COL32(80, 200, 200, 255);
static ImU32 colNeptune = IM_COL32(80, 140, 220, 255);
static ImU32 colPluto = IM_COL32(170, 80, 190, 255);
static ImU32 colNode = IM_COL32(120, 120, 120, 255);
static ImU32 colChiron = IM_COL32(120, 170, 80, 255);
static ImU32 colLilith = IM_COL32(210, 80, 180, 255);

static ImU32 color_for_body(const std::string& n) {
    if (n == "Sun") return colSun;
    if (n == "Moon") return colMoon;
    if (n == "Mercury") return colMercury;
    if (n == "Venus") return colVenus;
    if (n == "Mars") return colMars;
    if (n == "Jupiter") return colJupiter;
    if (n == "Saturn") return colSaturn;
    if (n == "Uranus") return colUranus;
    if (n == "Neptune") return colNeptune;
    if (n == "Pluto") return colPluto;
    if (n == "True Node" || n == "Mean Node") return colNode;
    if (n == "Chiron") return colChiron;
    if (n == "Lilith") return colLilith;
    return IM_COL32(220, 220, 220, 255);
}

// Aspect settings
struct AspectSetting {
    std::string label; // UI + legend name
	float   angle;  // exact angle in degrees
	float   base_orb; // base orb in degrees
	ImU32   color; // color for drawing
	float   width; // line width
	bool    enabled; // whether to draw
};

// palette for aspects
static const ImU32 A_COL_CONJ = IM_COL32(230, 230, 230, 190);
static const ImU32 A_COL_OPP = IM_COL32(230, 190, 90, 175);
static const ImU32 A_COL_TRI = IM_COL32(140, 235, 160, 160);
static const ImU32 A_COL_SQR = IM_COL32(255, 120, 120, 180);
static const ImU32 A_COL_SXT = IM_COL32(120, 200, 255, 170);
static const ImU32 A_COL_SSEXT = IM_COL32(180, 180, 180, 120); // 30
static const ImU32 A_COL_SSQ = IM_COL32(210, 160, 110, 130); // 45
static const ImU32 A_COL_SESQ = IM_COL32(255, 160, 100, 150); // 135
static const ImU32 A_COL_QNT = IM_COL32(200, 160, 255, 140); // 72
static const ImU32 A_COL_BQNT = IM_COL32(190, 150, 245, 145); // 144
static const ImU32 A_COL_QCX = IM_COL32(200, 200, 140, 150); // 150 (quincunx)

// Defaults. You can tweak base_orb and width here.
static std::vector<AspectSetting> gAspects = {
    // Major
    { "Conjunction",    0.0f, 6.0f, A_COL_CONJ, 2.2f, true },
    { "Opposition",   180.0f, 5.0f, A_COL_OPP,  2.0f, true },
    { "Trine",        120.0f, 5.0f, A_COL_TRI,  1.9f, true },
    { "Square",        90.0f, 5.0f, A_COL_SQR,  1.9f, true },
    { "Sextile",       60.0f, 4.0f, A_COL_SXT,  1.8f, true },
    // Minor
    { "Semisextile",   30.0f, 2.2f, A_COL_SSEXT,1.3f, false },
    { "Semisquare",    45.0f, 2.2f, A_COL_SSQ,  1.3f, false },
    { "Sesquiquadrate",135.0f,2.2f, A_COL_SESQ, 1.3f, false },
    { "Quintile",      72.0f, 1.8f, A_COL_QNT,  1.2f, false },
    { "Biquintile",   144.0f, 1.8f, A_COL_BQNT, 1.2f, false },
    { "Quincunx",     150.0f, 2.5f, A_COL_QCX,  1.5f, true },
};

// orb multipliers (editable in UI)
static float
gOrbGlobal = 1.00f,
gOrbLuminaries = 1.60f,
gOrbPersonal = 1.25f,
gOrbSocial = 1.10f,
gOrbOuter = 0.95f,
gOrbPoints = 0.90f;

// Which extra points to include as aspectable bodies
static bool gUseASC = true;
static bool gUseMC = true;
static bool gUseNode = true;   // True Node (or Mean Node if you prefer)
static bool gUseChiron = true;
static bool gUseLilith = true;

// Small swatch (since ImGui doesn’t have DrawColorSwatch)
static void DrawColorSwatch(const char* id, ImU32 col, ImVec2 size = ImVec2(60, 6))
{
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, col, 3.0f);
    dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 80), 3.0f);
}

// If you also want to *edit* the swatch color via ColorEdit3:
static bool EditColorFromU32(const char* id, ImU32& col)
{
    float rgb[3] = {
        ((col >> 16) & 255) / 255.0f,
        ((col >> 8) & 255) / 255.0f,
        ((col) & 255) / 255.0f
    };
    bool changed = ImGui::ColorEdit3(id, rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    if (changed) {
        col = IM_COL32(int(rgb[0] * 255.0f), int(rgb[1] * 255.0f), int(rgb[2] * 255.0f), (col >> 24) & 255);
    }
    return changed;
}

// body class → orb multiplier
static float orb_weight_for(const std::string& n) {
    if (n == "Sun" || n == "Moon") 
        return gOrbLuminaries * gOrbGlobal;
    if (n == "Mercury" || n == "Venus" || n == "Mars") 
        return gOrbPersonal * gOrbGlobal;
    if (n == "Jupiter" || n == "Saturn") 
        return gOrbSocial * gOrbGlobal;
    if (n == "Uranus" || n == "Neptune" || n == "Pluto") 
        return gOrbOuter * gOrbGlobal;
    // points/asteroids
    if (n == "ASC" || n == "MC" || n == "True Node" || n == "Mean Node" || n == "Chiron" || n == "Lilith") 
        return gOrbPoints * gOrbGlobal;
    return gOrbGlobal;
}

// tiny swatch helper for legend/table
static void DrawColorSwatch(ImU32 col, const ImVec2& size = ImVec2(18, 3)) {
    ImGui::InvisibleButton("##sw", ImVec2(size.x, size.y));
    auto p0 = ImGui::GetItemRectMin();
    auto p1 = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col, 2.0f);
}

auto places = load_places_csv("data/places/world_cities_min.csv");
static char cityQuery[128] = "";
static std::vector<int> cityHits;
static int selectedCity = -1;
// NEW: treat typed time as local by default + remember the city's tzid
static bool gInputIsLocal = true;
static std::string gSelectedTzid = "UTC";

static std::string fmtLongitude(double lon, bool asciiDegrees = false) {
    lon = norm360(lon);
    int signIdx = (int)(lon / 30.0) % 12;
    double within = fmod(lon, 30.0);
    auto dms = toDMS(within);
    std::ostringstream os;
    os << SIGN_NAMES[signIdx] << " " << dms.deg << (asciiDegrees ? " deg " : "° ") << std::setfill('0') << std::setw(2) << dms.min << "' " << std::fixed << std::setprecision(2) << dms.sec << "\"";
    return os.str();
}
static bool parseUtcDateTime(const std::string& s, int& Y, int& M, int& D, double& hour) {
    if (s.size() < 16) return false;
    try {
        Y = std::stoi(s.substr(0, 4)); M = std::stoi(s.substr(5, 2)); D = std::stoi(s.substr(8, 2));
        int h = std::stoi(s.substr(11, 2)); int m = std::stoi(s.substr(14, 2)); double sec = 0.0;
        if (s.size() >= 19) sec = std::stod(s.substr(17));
        hour = h + m / 60.0 + sec / 3600.0; return true;
    }
    catch (...) { return false; }
}

struct Body { std::string name; double lon{}, lat{}, speed{}; bool retro{}; };
struct Houses { double cusps[13]{}; double ascmc[10]{}; };

class AstrologyChart {
public:
    AstrologyChart(int Y, int M, int D, double hour_utc, double lat_deg, double lon_deg, char hsys = 'P')
        : Y(Y), M(M), D(D), hour(hour_utc), lat(lat_deg), lon(lon_deg), hsys(hsys) {
        jd_ut = swe_julday(Y, M, D, hour, SE_GREG_CAL);
    }
    void compute() { computePlanets(); computeHouses(); }
    const std::vector<Body>& getBodies() const { return bodies; }
    const Houses& getHouses() const { return H; }
    double getJDUT() const { return jd_ut; }
    char getHouse() const { return hsys; }
private:
    int Y, M, D; double hour, lat, lon; char hsys; double jd_ut{}; std::vector<Body> bodies; Houses H{};
    void computePlanets() {
        static const int kBodies[] = { SE_SUN,SE_MOON,SE_MERCURY,SE_VENUS,SE_MARS,SE_JUPITER,SE_SATURN,SE_URANUS,SE_NEPTUNE,SE_PLUTO,SE_TRUE_NODE,SE_CHIRON,SE_MEAN_APOG };
        bodies.clear();
        for (int ipl : kBodies) {
            double xx[6]; char serr[256] = { 0 };
            int rc = swe_calc_ut(jd_ut, ipl, SEFLG_SWIEPH | SEFLG_SPEED, xx, serr);
            if (rc < 0) throw std::runtime_error(std::string("swe_calc_ut: ") + serr);
            Body b;
            switch (ipl) {
            case SE_SUN: b.name = "Sun"; break; case SE_MOON: b.name = "Moon"; break; case SE_MERCURY: b.name = "Mercury"; break;
            case SE_VENUS: b.name = "Venus"; break; case SE_MARS: b.name = "Mars"; break; case SE_JUPITER: b.name = "Jupiter"; break;
            case SE_SATURN: b.name = "Saturn"; break; case SE_URANUS: b.name = "Uranus"; break; case SE_NEPTUNE: b.name = "Neptune"; break;
            case SE_PLUTO: b.name = "Pluto"; break; case SE_TRUE_NODE: b.name = "True Node"; break; case SE_CHIRON: b.name = "Chiron"; break;
            case SE_MEAN_APOG: b.name = "Lilith"; break; default: b.name = "Body"; break;
            }
            b.lon = norm360(xx[0]); b.lat = xx[1]; b.speed = xx[3]; b.retro = (xx[3] < 0);
            bodies.push_back(b);
        }
    }
    void computeHouses() {
        int rc = swe_houses_ex(jd_ut, SEFLG_SWIEPH, lat, lon, hsys, H.cusps, H.ascmc);
        if (rc == -1) throw std::runtime_error("swe_houses_ex failed");
    }
};

// ==== END: helpers + class ====

// Win32 / DX11 glue (trimmed from ImGui example)
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer != nullptr) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}
static void CleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; } }
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    static D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 1,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK) return false;
    CreateRenderTarget(); return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE: if (wParam != SIZE_MINIMIZED) { if (g_pd3dDevice) { CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0); CreateRenderTarget(); } } return 0;
    case WM_SYSCOMMAND: if ((wParam & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Example helper (you can drop this right above wWinMain or in an anon. namespace)
static bool LocalToUTC(const std::string& tzid, int Y, int M, int D, int hh, int mm, int ss, int& outY, int& outM, int& outD, double& outHourDec)
{
    try {
        using namespace date;
        using namespace std::chrono;

        const time_zone* tz = locate_zone(tzid);           // throws if unknown tzid
        local_time<seconds> lt = local_days{ year{Y} / M / D }   // local wall-clock
        + hours{ hh } + minutes{ mm } + seconds{ ss };
        zoned_time<seconds> zt{ tz, lt };                    // apply STD/DST rules
        sys_time<seconds> st = zt.get_sys_time();          // UTC

        // break out fields for Swiss Ephemeris
        std::time_t tt = system_clock::to_time_t(st);
        std::tm g{};
        gmtime_s(&g, &tt);  // Windows-safe

        outY = g.tm_year + 1900;
        outM = g.tm_mon + 1;
        outD = g.tm_mday;
        outHourDec = g.tm_hour + g.tm_min / 60.0 + g.tm_sec / 3600.0;
        return true;
    }
    catch (...) {
        return false;
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    // Ephemeris path (adjust to yours)
    const char* EPHE_PATH = "C:/Users/Admin/source/repos/Astrology/data/ephe";
    swe_set_ephe_path(EPHE_PATH);

    // Win32 window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), nullptr, nullptr, nullptr, nullptr, _T("AstrologyUI"), nullptr };
    ::RegisterClassEx(&wc);
    HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("Astrology UI (ImGui)"), WS_OVERLAPPEDWINDOW, 100, 100, 1200, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // D3D11
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); ::UnregisterClass(wc.lpszClassName, wc.hInstance); return 1; }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    date::reload_tzdb();   // safe to call; ensures tzdb is present/updated

    // Input state
    char ts[32] = "1996-02-12 16:20:00";  // UTC
    double lat = 53.79648, lon = -1.54785;
    int houseIdx = 0; const char* houseLabels[] = { "Placidus (P)", "Whole Sign (W)", "Equal (E)", "Koch (K)" };
    char hsys = 'P';
    bool asciiDegrees = false;
    bool hasResult = false;
    std::string error;

    // Computed data
    std::vector<Body> outBodies; Houses outH{};

    // Main loop
    MSG msg; ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        if (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) { ::TranslateMessage(&msg); ::DispatchMessage(&msg); continue; }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

		// Full-screen, non-draggable window that follows the host window size
#ifdef IMGUI_HAS_VIEWPORT
		ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::SetNextWindowViewport(vp->ID);
#else
		// Fallback for older Dear ImGui versions without multi-viewport support
		ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
#endif

        ImGuiWindowFlags topFlags =
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus
            // Important: no scrollbars here; we'll scroll inside the child so we don't get double scrollbars
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::Begin("Chart Calculator", nullptr, topFlags);
		ImVec2 avail = ImGui::GetContentRegionAvail();

		// NEW: Wrap ALL content in a full-height child with a vertical scrollbar
        ImGui::BeginChild(
            "MainContent", // unique id
            avail,  // take the full remaining size of the window
            false, // no border
            ImGuiWindowFlags_AlwaysVerticalScrollbar
        );

        ImGui::InputText("Birth datetime (YYYY-MM-DD HH:MM[:SS])", ts, IM_ARRAYSIZE(ts));
        ImGui::Checkbox("Interpret as LOCAL time in selected city's timezone", &gInputIsLocal);
		ImGui::InputText("City", cityQuery, IM_ARRAYSIZE(cityQuery));
        ImGui::SameLine();
        if (ImGui::Button("Find")) {
            find_places(places, cityQuery, cityHits);
            selectedCity = -1;
        }
        if (!cityHits.empty()) {
            ImGui::BeginChild("cityResults", ImVec2(0, 150), true);
            for (int i = 0; i < (int)cityHits.size(); ++i) {
                const auto& p = places[cityHits[i]];
                if (ImGui::Selectable(p.display().c_str(), i==selectedCity)) {
                    selectedCity = i;
					// Autofill lat/lon and optionally remember tzid
                    lat = p.lat; 
                    lon = p.lon;
                    gSelectedTzid = p.tzid.empty() ? "UTC" : p.tzid;   // <— remember the IANA tz
                }
            }
            ImGui::EndChild();
		}

        ImGui::InputDouble("Latitude  (S-)", &lat);
        ImGui::InputDouble("Longitude (E+)", &lon);
        ImGui::Combo("House system##house_combo", &houseIdx, houseLabels, IM_ARRAYSIZE(houseLabels));
        asciiDegrees = ImGui::CheckboxFlags("ASCII degrees (deg)", (unsigned int*)&asciiDegrees, 1) ? asciiDegrees : asciiDegrees;

        if (ImGui::Button("Compute")) {
            try {
                int Y, M, D; double hourDec;
                if (!parseUtcDateTime(ts, Y, M, D, hourDec))
                    throw std::runtime_error("Bad datetime. Use YYYY-MM-DD HH:MM[:SS].");

                // Prepare UTC values (start by assuming the typed time is already UTC)
                int Uy = Y, Um = M, Ud = D;
                double UhourDec = hourDec;

                if (gInputIsLocal) {
                    if (gSelectedTzid.empty())
                        throw std::runtime_error("No city/timezone selected.");

                    // Split decimal hours into hh:mm:ss
                    int hh = (int)hourDec;
                    int mm = (int)((hourDec - hh) * 60.0 + 1e-6);
                    int ss = (int)((hourDec - hh - mm / 60.0) * 3600.0 + 0.5);

                    if (!LocalToUTC(gSelectedTzid, Y, M, D, hh, mm, ss, Uy, Um, Ud, UhourDec))
                        throw std::runtime_error("Time zone conversion failed (unknown tzid?).");
                }

                hsys = (houseIdx == 0 ? 'P' : houseIdx == 1 ? 'W' : houseIdx == 2 ? 'E' : 'K');

                // Compute with UTC (Uy/Um/Ud + UhourDec)
                AstrologyChart chart(Uy, Um, Ud, UhourDec, lat, lon, hsys);
                chart.compute();

                outBodies = chart.getBodies();
                outH = chart.getHouses();
                hasResult = true;
                error.clear();
            }
            catch (const std::exception& e) { error = e.what(); hasResult = false; }
        }

        if (hasResult) {
            ImGui::TextDisabled("Computed in UTC using %s timezone", gInputIsLocal ? gSelectedTzid.c_str() : "UTC (typed)");
        }

        if (ImGui::CollapsingHeader("Aspect settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // toggles + legend
            if (ImGui::BeginTable("tbl_aspects", 3, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Aspect");
                ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < (int)gAspects.size(); ++i)
                {
                    auto& A = gAspects[i];
					ImGui::PushID(i); // <-- makes IDs unique within this row

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
					ImGui::Checkbox("##on", &A.enabled); // "##" = no visible text, uses row ID

                    ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(A.label.c_str()); // e.g. "Conjunction" (0 degrees)"

                    ImGui::TableSetColumnIndex(2);
                    EditColorFromU32("##col", A.color);

					ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Orbs (multipliers)");
            ImGui::SliderFloat("Global", &gOrbGlobal, 0.5f, 2.0f, "%.2f");
            ImGui::SliderFloat("Luminaries", &gOrbLuminaries, 0.6f, 2.5f, "%.2f");
            ImGui::SliderFloat("Personal", &gOrbPersonal, 0.6f, 2.0f, "%.2f");
            ImGui::SliderFloat("Social", &gOrbSocial, 0.6f, 2.0f, "%.2f");
            ImGui::SliderFloat("Outer", &gOrbOuter, 0.5f, 1.5f, "%.2f");
            ImGui::SliderFloat("Points/etc.", &gOrbPoints, 0.5f, 1.5f, "%.2f");

            ImGui::SeparatorText("Points to include");
            ImGui::Checkbox("ASC", &gUseASC); ImGui::SameLine();
            ImGui::Checkbox("MC", &gUseMC);  ImGui::SameLine();
            ImGui::Checkbox("Node", &gUseNode);ImGui::SameLine();
            ImGui::Checkbox("Chiron", &gUseChiron); ImGui::SameLine();
            ImGui::Checkbox("Lilith", &gUseLilith);

            if (ImGui::Button("Reset aspect defaults")) {
                // quick reset
                gAspects = {
                    { "Conjunction",    0.0f, 6.0f, A_COL_CONJ, 2.2f, true },
                    { "Opposition",   180.0f, 5.0f, A_COL_OPP,  2.0f, true },
                    { "Trine",        120.0f, 5.0f, A_COL_TRI,  1.9f, true },
                    { "Square",        90.0f, 5.0f, A_COL_SQR,  1.9f, true },
                    { "Sextile",       60.0f, 4.0f, A_COL_SXT,  1.8f, true },
                    { "Semisextile",   30.0f, 2.2f, A_COL_SSEXT,1.3f, false },
                    { "Semisquare",    45.0f, 2.2f, A_COL_SSQ,  1.3f, false },
                    { "Sesquiquadrate",135.0f,2.2f, A_COL_SESQ, 1.3f, false },
                    { "Quintile",      72.0f, 1.8f, A_COL_QNT,  1.2f, false },
                    { "Biquintile",   144.0f, 1.8f, A_COL_BQNT, 1.2f, false },
                    { "Quincunx",     150.0f, 2.5f, A_COL_QCX,  1.5f, true },
                };
                gOrbLuminaries = 1.60f; gOrbPersonal = 1.25f; gOrbSocial = 1.10f;
                gOrbOuter = 0.95f; gOrbPoints = 0.90f; gOrbGlobal = 1.00f;
                gUseASC = gUseMC = true; gUseNode = true; gUseChiron = gUseLilith = true;
            }
        }

        if (!error.empty()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "ERROR: %s", error.c_str());
        }

        if (hasResult) {
            if (ImGui::CollapsingHeader("Planets", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("tbl", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Body");
                    ImGui::TableSetupColumn("Longitude");
                    ImGui::TableSetupColumn("Retro");
                    ImGui::TableHeadersRow();
                    for (auto& b : outBodies) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(b.name.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(fmtLongitude(b.lon, asciiDegrees).c_str());
                        ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(b.retro ? "R" : "");
                    }
                    ImGui::EndTable();
                }
            }
            if (ImGui::CollapsingHeader("Houses##section", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable("tblH", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("House");
                    ImGui::TableSetupColumn("Cusp");
                    ImGui::TableHeadersRow();
                    for (int i = 1;i <= 12;++i) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::Text("House %d", i);
                        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(fmtLongitude(outH.cusps[i], asciiDegrees).c_str());
                    }
                    ImGui::EndTable();
                }
            }

            // === Chart wheel (centered, responsive) ===
            if (ImGui::CollapsingHeader("Wheel##section", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float wheel_size = std::clamp(avail.x - 20.0f, 320.0f, 680.0f);   // diameter
                float child_h = wheel_size + 40.0f;

                if (ImGui::BeginChild("wheelChild", ImVec2(-FLT_MIN, child_h), true))
                {
                    // Inner width of this child right now
                    float inner_w = ImGui::GetContentRegionAvail().x; // this equals content width at top of child
                    float pad_x = (inner_w - wheel_size) * 0.5f;
                    if (pad_x < 0.0f) pad_x = 0.0f; // don't extend past bounds

                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);

                    ImDrawList* draw = ImGui::GetWindowDrawList();
                    ImVec2 p0 = ImGui::GetCursorScreenPos();
                    ImVec2 center = ImVec2(p0.x + wheel_size * 0.5f, p0.y + wheel_size * 0.5f);

                    float R_outer = wheel_size * 0.48f;
                    float R_inner = wheel_size * 0.33f;
                    float R_planet = wheel_size * 0.31f;
                    float tick5 = wheel_size * 0.015f;
                    float tick30 = wheel_size * 0.03f;

                    // Backdrop
                    draw->AddRectFilled(p0, ImVec2(p0.x + wheel_size, p0.y + wheel_size), IM_COL32(18, 18, 22, 255), 8.0f);

                    // Rings
                    draw->AddCircle(center, R_outer, IM_COL32(200, 200, 200, 255), 256, 2.0f);
                    draw->AddCircle(center, R_inner, IM_COL32(90, 90, 90, 255), 256, 1.0f);

                    // Orientation: Ascendant (1st house cusp) at 9 o’clock
                    float asc = static_cast<float>(outH.ascmc[SE_ASC]);

                    // 12 signs: boundary line every 30°, label at the mid of each sign
                    for (int i = 0; i < 12; ++i) {
                        float lon0 = i * 30.0f;
                        float lon1 = (i + 1) * 30.0f;

                        float a0 = ecl_to_screen_angle(lon0, asc);
                        ImVec2 e0o = polar(center, R_outer, a0);
                        ImVec2 e0i = polar(center, R_inner, a0);
                        draw->AddLine(e0o, e0i, IM_COL32(160, 160, 160, 255), 2.0f);

                        float amid = ecl_to_screen_angle((lon0 + lon1) * 0.5f, asc);
                        ImVec2 lab = polar(center, (R_outer + R_inner) * 0.5f, amid);
                        const char* sign = SIGN_NAMES[i];
                        ImVec2 ts = ImGui::CalcTextSize(sign);
                        draw->AddText(ImVec2(lab.x - ts.x * 0.5f, lab.y - ts.y * 0.5f),
                            IM_COL32(220, 220, 220, 255), sign);
                    }

                    // Degree ticks: every 5° a small tick, every 30° a long one
                    for (int d = 0; d < 360; d += 5) {
                        float a = ecl_to_screen_angle((float)d, asc);
                        bool major = (d % 30 == 0);
                        float len = major ? tick30 : tick5;
                        ImVec2 o = polar(center, R_outer, a);
                        ImVec2 i = polar(center, R_outer - len, a);
                        draw->AddLine(o, i, IM_COL32(120, 120, 120, 255), major ? 2.0f : 1.0f);
                    }

                    // Houses (cusps + labels)
                    // Convention: house 1 is the one containing the ascendant; houses increase counter-clockwise
                    static const char* ROMAN[13] = { "", "I","II","III","IV","V","VI","VII","VIII","IX","X","XI","XII" };

                    // Slightly different radii for house visuals
                    float R_house_outer = R_outer; // cusp lines reach the outer ring
                    float R_house_inner = R_inner; // start from the inner ring
                    float R_house_num = (R_inner + R_planet) * 0.5f; // position for numbers

                    // Color/width so they stand out from sign boundaries
                    ImU32 colHouse = IM_COL32(190, 190, 190, 255);
                    float wHouse = 3.0f;

                    // House cusps + numbers
                    for (int h = 1; h <= 12; ++h) {
						float cuspLon = (float)outH.cusps[h]; // cusp longitude in degrees
						float a = ecl_to_screen_angle(cuspLon, asc); // rotate so ASC = 9 o'clock

                        // radial cusp line
                        ImVec2 p_out = polar(center, R_outer, a);
                        ImVec2 p_in = polar(center, R_outer - wheel_size * 0.06f, a);
                        draw->AddLine(p_in, p_out, colHouse, (h == 1 || h == 10) ? wHouse + 0.5f : wHouse);

                        // house number
						ImVec2 p_num = polar(center, R_house_num, a);
						const char* txt = ROMAN[h];
						ImVec2 ts = ImGui::CalcTextSize(txt);

                        draw->AddText(ImVec2(p_num.x - ts.x * 0.5f, p_num.y - ts.y * 0.5f),
                            IM_COL32(200, 200, 200, 255), txt);
                    }

                    // Optional: draw MC/IC/ASC/DSC axis so orientation is obvious
					float mc = (float)outH.ascmc[SE_MC];
					float dsc = norm360(asc + 180.0f);
					float ic = norm360(mc + 180.0f);

                    auto draw_axis = [&](float lon, ImU32 col) {
                        float a = ecl_to_screen_angle(lon, asc);
						draw->AddLine(polar(center, R_outer, a), polar(center, R_inner, a), col, 2.5f);
                        };
					draw_axis(asc, IM_COL32(255, 255, 255, 200)); // ASC
					draw_axis(dsc, IM_COL32(255, 255, 255, 120)); // DSC
					draw_axis(mc, IM_COL32(200, 200, 255, 180)); // MC
					draw_axis(ic, IM_COL32(200, 200, 255, 120)); // IC

                    // -- Aspect helpers --
                    auto shortest_sep = [](float a, float b) {
                        float d = fmodf(fabsf(a - b), 360.0f);
                        if (d > 180.0f) d = 360.0f - d; // fold to 0..180
                        return d;
                        };

                    // collect draw positions for planets + points we want to include
                    struct P { ImVec2 p; float lon; std::string name; };
					float R_line = R_planet - wheel_size * 0.03f;

                    std::vector<P> ps; ps.reserve(outBodies.size() + 5);
                    // planets/asteroids/nodes you already compute
                    for (const auto& b : outBodies) {
                        float a = ecl_to_screen_angle((float)b.lon, asc);
                        ps.push_back({ polar(center, R_line, a), (float)b.lon, b.name });
                    }

					// sensitive points (added based on checkboxes)
					float lonASC = (float)outH.ascmc[SE_ASC];
                    float lonMC = (float)outH.ascmc[SE_MC];
					float lonNode = (float)outBodies.back().lon; // if you want your existing True Node
					// ^^^ optionally, look up by name instead of "back()"

                    if (gUseASC)    ps.push_back({ polar(center, R_line, ecl_to_screen_angle(lonASC, asc)), lonASC, "ASC" });
                    if (gUseMC)     ps.push_back({ polar(center, R_line, ecl_to_screen_angle(lonMC, asc)), lonMC, "MC" });
					if (gUseNode)   ps.push_back({ polar(center, R_line, ecl_to_screen_angle((float)lonNode, asc)), (float)lonNode, "True Node" });
                    if (gUseChiron) {
                        auto it = std::find_if(outBodies.begin(), outBodies.end(), [](const Body& b) { return b.name == "Chiron"; });
                        if (it != outBodies.end()) { float a = ecl_to_screen_angle((float)it->lon, asc);
                            ps.push_back({ polar(center, R_line, a), (float)it->lon, it->name }); }
                    }
                    if (gUseLilith) {
                        auto it = std::find_if(outBodies.begin(), outBodies.end(), [](const Body& b) { return b.name == "Lilith"; });
                        if (it != outBodies.end()) { float a = ecl_to_screen_angle((float)it->lon, asc); ps.push_back({ polar(center, R_line, a), (float)it->lon, it->name }); }
                    }

					// For each pair, draw the *best* matching aspect (if any),
					// so we don't stack multiple lines for near-angles.
                    for (int i = 0; i < (int)ps.size(); ++i) {
                        for (int j = i + 1; j < (int)ps.size(); ++j) {
							float sep = shortest_sep(ps[i].lon, ps[j].lon); // 0..180

							const AspectSetting* best = nullptr;
                            float bestDelta = 1e9f;

							// Allowed orb scales with the weaker of the two bodies
                            float scale = (std::min)(orb_weight_for(ps[i].name), orb_weight_for(ps[j].name));

                            for (const auto& A : gAspects) {
								if (!A.enabled) continue;
								float allow = A.base_orb * scale;
                                float delta = fabsf(sep - A.angle);
                                if (delta <= allow && delta < bestDelta) {
									bestDelta = delta; 
                                    best = &A;
                                }
							}

                            if (best) {
								// avoid any tiny "dot" for near-perfect conjunction
								if (best->angle == 0.0f && sep < 0.4f) continue;
								ImGui::GetWindowDrawList()->AddLine(ps[i].p, ps[j].p, best->color, best->width);
                            }
                        }
                    }

                    // Planets + labels
                    for (const auto& b : outBodies) {
                        float ang = ecl_to_screen_angle((float)b.lon, asc);
                        ImVec2 pt = polar(center, R_planet, ang);
                        ImU32 col = color_for_body(b.name);
                        draw->AddCircleFilled(pt, wheel_size * 0.012f, col, 24);

                        ImVec2 lbl = polar(center, R_planet + wheel_size * 0.04f, ang);
                        draw->AddText(lbl, col, b.name.c_str());
                    }

                    // Center marker
                    draw->AddCircleFilled(center, wheel_size * 0.01f, IM_COL32(180, 180, 180, 220));

                    // IMPORTANT: submit an item that covers the wheel area so the child grows to it.
                    ImGui::SetCursorScreenPos(p0);
                    ImGui::Dummy(ImVec2(wheel_size, wheel_size));
                }
                ImGui::EndChild();
            }

            // Legend (its own header, unique id)
            if (ImGui::CollapsingHeader("Legend##section", ImGuiTreeNodeFlags_DefaultOpen)) {
				// Planet colors
                if (ImGui::BeginTable("legend", 3, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn(" ");
                    ImGui::TableSetupColumn("Body");
                    ImGui::TableSetupColumn("Color");
                    ImGui::TableHeadersRow();

                    auto row = [&](const char* name) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImU32 c = color_for_body(name);
                        ImGui::InvisibleButton(("##" + std::string(name)).c_str(), ImVec2(16, 16));
                        ImVec2 p = ImGui::GetItemRectMin();
                        ImVec2 q = ImGui::GetItemRectMax();
                        ImGui::GetWindowDrawList()->AddRectFilled(p, q, c, 3.0f);

                        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(name);
                        ImGui::TableSetColumnIndex(2); ImGui::Text("#%02X%02X%02X", (c >> 16) & 255, (c >> 8) & 255, c & 255);
                        };

                    row("Sun"); row("Moon"); row("Mercury"); row("Venus"); row("Mars");
                    row("Jupiter"); row("Saturn"); row("Uranus"); row("Neptune"); row("Pluto");
                    row("True Node"); row("Chiron"); row("Lilith");
                    ImGui::EndTable();
                }
				// Aspect colors
                if (ImGui::BeginTable("aspect_legend", 3, ImGuiTableFlags_SizingFixedFit)) {
					ImGui::TableSetupColumn(" ");
					ImGui::TableSetupColumn("Aspect");
					ImGui::TableSetupColumn("Angle");
					ImGui::TableHeadersRow();
                    for (auto& A : gAspects) {
                        if (!A.enabled) continue;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        // ImGui::DrawColorSwatch(A.color, ImVec2(40, 4));
						ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(A.label.c_str());
						ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f°", A.angle);
					}
					ImGui::EndTable();
                }
            }
        }

		// NEW: Close the wrapper child
		ImGui::EndChild();
        ImGui::End();

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.08f,0.08f,0.10f,1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(_T("AstrologyUI"), wc.hInstance);
    swe_close();
    return 0;
}