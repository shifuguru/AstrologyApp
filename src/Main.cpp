// Main.cpp — v2 console app with a reusable AstrologyChart class (C++17)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// #define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <filesystem>

extern "C" {
#include "swephexp.h"
}

// ---- Config ----
static const char* EPHE_PATH = "C:/Users/Admin/source/repos/Astrology/data/ephe"; // or "../../data/ephe"

// ---- Helpers ----
static inline double norm360(double x) { double y = fmod(x, 360.0); if (y < 0) y += 360.0; return y; }

struct DMS { int deg; int min; double sec; };
static DMS toDMS(double degrees) {
    double d = std::floor(degrees);
    double mfull = (degrees - d) * 60.0;
    double m = std::floor(mfull);
    double s = (mfull - m) * 60.0;
    return { (int)d, (int)m, s };
}

static const char* SIGN_NAMES[12] = {
  "Aries","Taurus","Gemini","Cancer","Leo","Virgo",
  "Libra","Scorpio","Sagittarius","Capricorn","Aquarius","Pisces"
};

static std::string fmtLongitude(double lon, bool asciiDegrees = false) {
    lon = norm360(lon);
    int signIdx = (int)(lon / 30.0) % 12;
    double within = fmod(lon, 30.0);
    auto dms = toDMS(within);
    std::ostringstream os;
    os << SIGN_NAMES[signIdx] << " "
        << dms.deg << (asciiDegrees ? " deg " : u8"° ") << std::setfill('0')
        << std::setw(2) << dms.min << "' "
        << std::fixed << std::setprecision(2) << dms.sec << "\"";
    return os.str();
}

// strict UTC parser: "YYYY-MM-DD HH:MM[:SS]"
static bool parseUtcDateTime(const std::string& s, int& year, int& month, int& day, double& hour) {
    if (s.size() < 16) return false;
    try {
        year = std::stoi(s.substr(0, 4));
        month = std::stoi(s.substr(5, 2));
        day = std::stoi(s.substr(8, 2));
        int h = std::stoi(s.substr(11, 2));
        int m = std::stoi(s.substr(14, 2));
        double sec = 0.0;
        if (s.size() >= 19) sec = std::stod(s.substr(17));
        hour = h + m / 60.0 + sec / 3600.0;
        return true;
    }
    catch (...) { return false; }
}

// ---- Core data ----
struct Body {
    std::string name;
    double lon{};
    double lat{};
    double speed{};
    bool retro{};
};

struct Houses {
    double cusps[13]{}; // 1..12
    double ascmc[10]{}; // [SE_ASC], [SE_MC], ...
};

// ---- AstrologyChart class ----
class AstrologyChart {
public:
    AstrologyChart(int Y, int M, int D, double hour_utc, double lat_deg, double lon_deg, char house = 'P')
        : Y(Y), M(M), D(D), hour(hour_utc), lat(lat_deg), lon(lon_deg), hsys(house) {
        jd_ut = swe_julday(Y, M, D, hour, SE_GREG_CAL);
    }

    void compute() { computePlanets(); computeHouses(); }

    void print(bool asciiDegrees = false) const {
        std::cout << "Planets:\n";
        for (const auto& b : bodies) {
            std::cout << std::left << std::setw(11) << b.name
                << fmtLongitude(b.lon, asciiDegrees)
                << (b.retro ? " [R]" : "") << "\n";
        }
        std::cout << "\nHouses (" << houseName() << "):\n";
        for (int i = 1; i <= 12; ++i) {
            std::cout << "House " << std::setw(2) << i << ": "
                << fmtLongitude(H.cusps[i], asciiDegrees) << "\n";
        }
        std::cout << "\nAscendant: " << fmtLongitude(norm360(H.ascmc[SE_ASC]), asciiDegrees) << "\n";
        std::cout << "Midheaven: " << fmtLongitude(norm360(H.ascmc[SE_MC]), asciiDegrees) << "\n";
    }

    const std::vector<Body>& getBodies() const { return bodies; }
    const Houses& getHouses() const { return H; }
    double getJulianDayUT() const { return jd_ut; }

private:
    int Y, M, D;
    double hour, lat, lon;
    char hsys;
    double jd_ut{};
    std::vector<Body> bodies;
    Houses H{};

    const char* houseName() const {
        switch (hsys) {
        case 'P': return "Placidus";
        case 'W': return "Whole Sign";
        case 'E': return "Equal";
        case 'K': return "Koch";
        default:  return "Custom";
        }
    }

    void computePlanets() {
        static const int kBodies[] = {
          SE_SUN, SE_MOON, SE_MERCURY, SE_VENUS, SE_MARS,
          SE_JUPITER, SE_SATURN, SE_URANUS, SE_NEPTUNE, SE_PLUTO,
          SE_TRUE_NODE, SE_CHIRON, SE_MEAN_APOG
        };
        bodies.clear();
        for (int ipl : kBodies) {
            double xx[6]; char serr[256] = { 0 };
            int rc = swe_calc_ut(jd_ut, ipl, SEFLG_SWIEPH | SEFLG_SPEED, xx, serr);
            if (rc < 0) throw std::runtime_error(std::string("swe_calc_ut: ") + serr);

            Body b;
            switch (ipl) {
            case SE_SUN:        b.name = "Sun"; break;
            case SE_MOON:       b.name = "Moon"; break;
            case SE_MERCURY:    b.name = "Mercury"; break;
            case SE_VENUS:      b.name = "Venus"; break;
            case SE_MARS:       b.name = "Mars"; break;
            case SE_JUPITER:    b.name = "Jupiter"; break;
            case SE_SATURN:     b.name = "Saturn"; break;
            case SE_URANUS:     b.name = "Uranus"; break;
            case SE_NEPTUNE:    b.name = "Neptune"; break;
            case SE_PLUTO:      b.name = "Pluto"; break;
            case SE_TRUE_NODE:  b.name = "True Node"; break;
            case SE_CHIRON:     b.name = "Chiron"; break;
            case SE_MEAN_APOG:  b.name = "Lilith"; break;
            default:            b.name = "Body"; break;
            }
            b.lon = norm360(xx[0]);
            b.lat = xx[1];
            b.speed = xx[3];
            b.retro = (xx[3] < 0);
            bodies.push_back(b);
        }
    }

    void computeHouses() {
        int rc = swe_houses_ex(jd_ut, SEFLG_SWIEPH, lat, lon, hsys, H.cusps, H.ascmc);
        if (rc == -1) throw std::runtime_error("swe_houses_ex failed");
    }
};

// ---- main ----
int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try {
        // set ephemeris path
        namespace fs = std::filesystem;
        fs::path ephe = EPHE_PATH;
        if (!ephe.is_absolute())
            ephe = fs::weakly_canonical(fs::current_path() / ephe);
        swe_set_ephe_path(ephe.string().c_str());

        // demo inputs
        int Y = 1996, M = 2, D = 12;
        double hour = 16.2;
        double lat = -53.80, lon = -1.54;
        // if (argc > 1) parseUtcDateTime(argv[1], Y, M, D, hour);

        AstrologyChart chart(Y, M, D, hour, lat, lon, 'P');
        chart.compute();
        chart.print(false);

        swe_close();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << "\n";
        swe_close();
        return 1;
    }
}
