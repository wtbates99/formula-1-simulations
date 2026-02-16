#include "f1sim/support/scenario_loader.hpp"

#include <fstream>
#include <regex>
#include <sstream>

namespace f1sim::support {

namespace {

bool read_file(const std::string& path, std::string* out, std::string* err) {
    std::ifstream in(path);
    if (!in.good()) {
        *err = "Failed to open scenario file: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    *out = buffer.str();
    return true;
}

bool extract_number(const std::string& body, const std::string& key, double* out) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*([0-9]+(?:\\.[0-9]+)?)");
    std::smatch m;
    if (!std::regex_search(body, m, pattern)) return false;
    *out = std::stod(m[1].str());
    return true;
}

bool extract_integer(const std::string& body, const std::string& key, int* out) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch m;
    if (!std::regex_search(body, m, pattern)) return false;
    *out = std::stoi(m[1].str());
    return true;
}

} // namespace

bool load_scenario_json(
    const std::string& path,
    SimConfig* config,
    std::vector<DriverProfile>* drivers,
    std::string* error_message
) {
    std::string body;
    if (!read_file(path, &body, error_message)) return false;

    double track_length = config->track_length_m;
    double dt = config->dt_seconds;
    int total_laps = config->total_laps;
    int seed = static_cast<int>(config->seed);
    extract_number(body, "track_length_m", &track_length);
    extract_number(body, "dt_seconds", &dt);
    extract_integer(body, "total_laps", &total_laps);
    extract_integer(body, "seed", &seed);

    std::vector<DriverProfile> parsed_drivers;
    const std::regex driver_pattern(
        R"REGEX(\{\s*"id"\s*:\s*"([^"]+)"\s*,\s*"team"\s*:\s*"([^"]+)"\s*,\s*"skill"\s*:\s*([0-9.]+)\s*,\s*"aggression"\s*:\s*([0-9.]+)(?:\s*,\s*"consistency"\s*:\s*([0-9.]+))?(?:\s*,\s*"start_compound"\s*:\s*"([^"]+)")?(?:\s*,\s*"planned_pit_laps"\s*:\s*\[([^\]]*)\])?\s*\})REGEX");
    auto begin = std::sregex_iterator(body.begin(), body.end(), driver_pattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        DriverProfile d;
        d.id = (*it)[1].str();
        d.team = (*it)[2].str();
        d.skill = std::stod((*it)[3].str());
        d.aggression = std::stod((*it)[4].str());
        d.consistency = (*it)[5].matched ? std::stod((*it)[5].str()) : 0.6;
        d.start_compound = (*it)[6].matched ? tyre_compound_from_string((*it)[6].str()) : TyreCompound::Medium;

        if ((*it)[7].matched) {
            const std::string laps_block = (*it)[7].str();
            const std::regex num_re(R"((\d+))");
            auto nbegin = std::sregex_iterator(laps_block.begin(), laps_block.end(), num_re);
            auto nend = std::sregex_iterator();
            for (auto nit = nbegin; nit != nend; ++nit) {
                d.planned_pit_laps.push_back(std::stoi((*nit)[1].str()));
            }
        }

        parsed_drivers.push_back(d);
    }

    if (!parsed_drivers.empty()) *drivers = parsed_drivers;
    config->track_length_m = track_length;
    config->dt_seconds = dt;
    config->total_laps = total_laps;
    config->seed = static_cast<std::uint32_t>(seed);
    return true;
}

} // namespace f1sim::support
