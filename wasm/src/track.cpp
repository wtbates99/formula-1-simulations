#include "f1/track.hpp"

#include <algorithm>

namespace f1 {

bool TrackProfile::Load(const TrackConfig& cfg) {
  if (cfg.nodes == nullptr || cfg.node_count < 2 || cfg.length_m <= 1.0f) {
    return false;
  }

  s_nodes_.resize(cfg.node_count);
  curvature_.resize(cfg.node_count);
  elevation_.resize(cfg.node_count);
  length_m_ = cfg.length_m;

  for (std::uint32_t i = 0; i < cfg.node_count; ++i) {
    s_nodes_[i] = cfg.nodes[i].s;
    curvature_[i] = cfg.nodes[i].curvature;
    elevation_[i] = cfg.nodes[i].elevation;
  }

  return true;
}

float TrackProfile::wrap_s(float s_m) const {
  if (length_m_ <= 0.0f) {
    return 0.0f;
  }
  while (s_m < 0.0f) {
    s_m += length_m_;
  }
  while (s_m >= length_m_) {
    s_m -= length_m_;
  }
  return s_m;
}

float TrackProfile::sample(const std::vector<float>& values, float s_m) const {
  if (s_nodes_.empty()) {
    return 0.0f;
  }
  const float s = wrap_s(s_m);

  auto upper = std::upper_bound(s_nodes_.begin(), s_nodes_.end(), s);
  if (upper == s_nodes_.begin()) {
    return values.front();
  }

  std::size_t i1 = static_cast<std::size_t>(upper - s_nodes_.begin());
  std::size_t i0 = i1 - 1;

  if (i1 >= s_nodes_.size()) {
    const float s0 = s_nodes_.back();
    const float s1 = length_m_ + s_nodes_.front();
    const float t = (s - s0) / (s1 - s0);
    return values.back() + (values.front() - values.back()) * t;
  }

  const float s0 = s_nodes_[i0];
  const float s1 = s_nodes_[i1];
  if (s1 <= s0) {
    return values[i0];
  }
  const float t = (s - s0) / (s1 - s0);
  return values[i0] + (values[i1] - values[i0]) * t;
}

float TrackProfile::curvature(float s_m) const { return sample(curvature_, s_m); }

float TrackProfile::elevation(float s_m) const { return sample(elevation_, s_m); }

}  // namespace f1
