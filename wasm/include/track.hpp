#pragma once

#include <vector>

#include "f1/sim_types.hpp"

namespace f1 {

class TrackProfile {
 public:
  bool Load(const TrackConfig& cfg);
  float curvature(float s_m) const;
  float elevation(float s_m) const;
  float length() const { return length_m_; }

 private:
  float sample(const std::vector<float>& values, float s_m) const;
  float wrap_s(float s_m) const;

  std::vector<float> s_nodes_;
  std::vector<float> curvature_;
  std::vector<float> elevation_;
  float length_m_ = 0.0f;
};

}  // namespace f1
