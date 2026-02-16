#pragma once

#include "f1sim/sim/simulator.hpp"

#include <string>
#include <vector>

namespace f1sim::support {

bool apply_telemetry_seed(
    const std::string& db_path,
    int season,
    int round,
    std::vector<DriverProfile>* drivers,
    std::string* error_message
);

} // namespace f1sim::support
