#pragma once

#include "f1sim/sim/simulator.hpp"

#include <string>
#include <vector>

namespace f1sim::support {

bool load_scenario_json(
    const std::string& path,
    SimConfig* config,
    std::vector<DriverProfile>* drivers,
    std::string* error_message
);

} // namespace f1sim::support
