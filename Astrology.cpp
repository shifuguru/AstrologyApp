// Astrology.cpp : This file contains the 'main' function. Program execution begins and ends there.
// Minimal Swiss Ephemeris bootstrap for a natal chart (UTC time).
// Build notes:
// - Add Swiss Ephemeris C sources / headers to the project (deps/swe/*) 
// - Place ephemeris data files (.se1) under ../data/ephe relative to the executable (or edit EPHE_PATH below).
// - Project properties: C/C++ > Additional Include Directories: add path to deps/swe
// - Build a static library from deps/swe/*.c, link it; (or add them to the project).
// - Empty Houses System

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32


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
// #include "Astrology.h"

// ---- Configuration ----
static const char* EPHE_PATH = "C:/Users/Admin/source/repos/Astrology/data/ephe"; // "../../data/ephe"; // path to ephemeris files (.se1) relative to executable

// ---- Helpers ----
static inline double norm360(double x)
{
	double y = fmod(x, 360.0);
	if (y < 0) y += 360.0;
	return y;
}

struct DMS { int deg; int min; double sec; };
static DMS toDMS(double degrees) {
	double d = std::floor(degrees);
	double mfull = (degrees - d) * 60.0;
	double m = std::floor(mfull);
	double s = (mfull - m) * 60.0;
	return { static_cast<int>(d), static_cast<int>(m), s };
}

static const char* SIGN_NAMES[12] = {
	"Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
	"Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"
};

static std::string fmtLongitude(double lon)
{
	lon = norm360(lon);
	int signIdx = static_cast<int>(lon / 30.0) % 12;
	double within = fmod(lon, 30.0);
	auto dms = toDMS(within);
	std::ostringstream os;
	os << SIGN_NAMES[signIdx] << " "
		<< dms.deg << "° " << std::setfill('0')
		<< std::setw(2) << dms.min << "' "
		<< std::fixed << std::setprecision(2) << dms.sec << "\"";
	return os.str();
}

static bool parseUtcDateTime(const std::string& s, int& year, int& month, int& day, double& hour)
{
	// Expecting strict format: "YYYY-MM-DD HH:MM:SS"
	if (s.size() < 19) return false;

	try {
		int h = std::stoi(s.substr(11, 2));
		int m = std::stoi(s.substr(14, 2));
		double sec = std::stod(s.substr(17));

		year = std::stoi(s.substr(0, 4));
		month = std::stoi(s.substr(5, 2));
		day = std::stoi(s.substr(8, 2));

		hour = h + m / 60.0 + sec / 3600.0; // convert to decimal hours

		return true;
	}
	catch (...) {
		return false;
	}
}

struct BodyOut {
	std::string name;
	double longitude; // ecliptic longitude in degrees
	double latitude; // ecliptic latitude in degrees
	double speed; // degrees per day
	bool isRetrograde; // true if retrograde (speed < 0)
};

static BodyOut calcBody(double jd_ut, int ipl) {
	double xx[6]; char serr[256] = { 0 };
	int rc = swe_calc_ut(jd_ut, ipl, SEFLG_SWIEPH | SEFLG_SPEED, xx, serr);
	if (rc < 0) {
		throw std::runtime_error(std::string("swe_calc_ut error: ") + serr);
	}
	BodyOut b{};
	switch (ipl)
	{
		case SE_SUN:		b.name = "Sun"; break;
		case SE_MOON:		b.name = "Moon"; break;
		case SE_MERCURY:	b.name = "Mercury"; break;
		case SE_VENUS:		b.name = "Venus"; break;
		case SE_MARS:		b.name = "Mars"; break;
		case SE_JUPITER:	b.name = "Jupiter"; break;
		case SE_SATURN:		b.name = "Saturn"; break;
		case SE_URANUS:		b.name = "Uranus"; break;
		case SE_NEPTUNE:	b.name = "Neptune"; break;
		case SE_PLUTO:		b.name = "Pluto"; break;
		case SE_TRUE_NODE:	b.name = "True Node"; break;
		case SE_MEAN_NODE:	b.name = "Mean Node"; break;
		case SE_CHIRON:		b.name = "Chiron"; break;
		case SE_MEAN_APOG:	b.name = "Lilith"; break;
		default:			b.name = "Body"; break;
	}

	b.longitude		= norm360(xx[0]); // ecliptic longitude
	b.latitude		= xx[1]; // ecliptic latitude
	b.speed			= xx[3]; // speed in degrees per day
	b.isRetrograde	= (b.speed < 0);

	return b;
}

struct HousesOut {
	double cusps[13]; // house cusps 1..12 (0 unused)
	double ascmc[10]; // ascmc[0] = ascendant, ascmc[1] = MC, ascmc[2] = ARMC, ascmc[3] = vertex, ascmc[4] = equatorial ascendant, ascmc[5..9] unused
};

static HousesOut calcHouses(double jd_ut, double lat, double lon, int hsys = 'P')
{
	HousesOut H{};

	int rc = swe_houses_ex(jd_ut, SEFLG_SWIEPH, lat, lon, hsys, H.cusps, H.ascmc);
	if (rc == -1) throw std::runtime_error("swe_houses_ex failed");
	return H;
}

int main(int argc, char** argv)
{
#ifdef _WIN32
		SetConsoleOutputCP(CP_UTF8);
		SetConsoleCP(CP_UTF8);
#endif	// _WIN32

		// Initialize Swiss Ephemeris
	try
	{
		std::filesystem::path ephe = EPHE_PATH;
		if (!ephe.is_absolute()) {
			ephe = std::filesystem::weakly_canonical(std::filesystem::current_path() / ephe);
		}
		swe_set_ephe_path(ephe.string().c_str());
		
		// Demo inputs (UTC) - Wellington, NZ, 7 June 1999, 02:00:00
		int Y = 2000, M = 1, D = 1;
		double hour = 2.0;
		double lat = -41.29, lon = 174.78;

		// If you pass "YYYY-MM-DD HH:MM:SS" in argv[1], uncomment:
		// if (argc > 1) parseUtcDateTime(argv[1], Y, M, D, hour);

		double jd_ut = swe_julday(Y, M, D, hour, SE_GREG_CAL);

		std::vector<int> bodies = {
			SE_SUN, SE_MOON, SE_MERCURY, SE_VENUS, SE_MARS,
			SE_JUPITER, SE_SATURN, SE_URANUS, SE_NEPTUNE, SE_PLUTO,
			SE_TRUE_NODE, SE_CHIRON, SE_MEAN_APOG
		};

		std::cout << "Planets: \n";
		for (int ipl : bodies) {
			BodyOut b = calcBody(jd_ut, ipl);
			std::cout << std::setw(10) << b.name << ": "
				<< fmtLongitude(b.longitude) << ")"
				<< (b.isRetrograde ? " [R]" : "")
				<< "\n";
		}

		HousesOut H = calcHouses(jd_ut, lat, lon, 'P');
		std::cout << "\nAscendant: " << fmtLongitude(H.ascmc[0]) << "\n";
		std::cout << "Midheaven: " << fmtLongitude(H.ascmc[1]) << "\n";

		swe_close(); // free memory allocated by Swiss Ephemeris
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "\nERROR: " << ex.what() << "\n";
		swe_close();
		return 1;
	}
}