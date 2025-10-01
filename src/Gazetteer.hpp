#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

struct Place {
    std::string name, admin, country, tzid;
    double lat{0}, lon{0};
    std::string display() const {
        std::ostringstream os;
        os << name;
		if (!admin.empty()) os << ", " << admin;
		if (!country.empty()) os << " (" << country << ")";
		return os.str();
    }
};

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

static std::vector<Place> load_places_csv(const std::string& path) {
    std::vector<Place> v;
    std::ifstream f(path);
    if (!f) return v;
    std::string line;
    // skip header
    std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
		auto cols = split_csv(line);
		if (cols.size() < 6) continue;
		Place p;
		p.name = cols[0];
		p.admin = cols[1];
		p.country = cols[2];
		p.lat = std::stod(cols[3]); // south = negative
		p.lon = std::stod(cols[4]); // west = negative
		p.tzid = cols[5];
        v.push_back(p);
    }
    return v;
}

inline void find_places(const std::vector<Place>& all, const char* q, std::vector<int>& hits) {
    hits.clear();
    if (!q || !*q) return;
    std::string needle = q;
	std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    for (int i = 0; i < (int)all.size(); ++i) {
		std::string hay = all[i].name + "," + all[i].admin + " " + all[i].country;
		std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
        if (hay.find(needle) != std::string::npos) hits.push_back(i);
		if ((int)hits.size() >= 1000) break; // limit
    }
}
