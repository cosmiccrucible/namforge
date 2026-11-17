#pragma once

#include "NamA2Core.h"

#include <algorithm>
#include <vector>

namespace namforge
{
// Two quality variants behind one runtime switch. Both models share the same
// parameter contract; only channel width differs. A short block-boundary
// crossfade avoids clicks when switching.
class NamA2DualCore
{
public:
  void load(const ModelDesc& standard, const ModelDesc* eco = nullptr)
  {
    standard_.load(standard);
    has_eco_ = eco != nullptr && eco->num_layers > 0;
    if (has_eco_)
      eco_.load(*eco);
    current_ = 1;
    target_ = current_;
    fade_remaining_ = 0;
    fade_length_ = 0;
  }

  bool hasEco() const { return has_eco_; }
  int numParams() const { return standard_.numParams(); }
  int qualityMode() const { return current_; } // 0 = Eco, 1 = Standard

  void prepare(double sampleRate, int maxBlockSize, int dilationScale = 1)
  {
    standard_.prepare(sampleRate, maxBlockSize, dilationScale);
    if (has_eco_) eco_.prepare(sampleRate, maxBlockSize, dilationScale);
    fade_length_ = std::max(1, (int) (0.02 * sampleRate * std::max(1, dilationScale) + 0.5));
    scratch_a_.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
    scratch_b_.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
  }

  void reset()
  {
    standard_.reset();
    if (has_eco_) eco_.reset();
    fade_remaining_ = 0;
  }

  void setModelParam(int index, float value)
  {
    standard_.setModelParam(index, value);
    if (has_eco_) eco_.setModelParam(index, value);
  }

  void setQualityMode(int mode)
  {
    if (!has_eco_)
    {
      current_ = target_ = 1;
      return;
    }
    mode = std::clamp(mode, 0, 1);
    if (mode == target_)
      return;
    target_ = mode;
    fade_remaining_ = fade_length_;
    if (mode == 0)
      eco_.reset();
    else
      standard_.reset();
  }

  void process(const float* input, float* output, int numSamples)
  {
    if (!has_eco_ || fade_remaining_ <= 0)
    {
      NamA2Core& c = current_ == 0 ? eco_ : standard_;
      c.process(input, output, numSamples);
      return;
    }

    if (static_cast<int>(scratch_a_.size()) < numSamples)
    {
      scratch_a_.resize(static_cast<size_t>(numSamples));
      scratch_b_.resize(static_cast<size_t>(numSamples));
    }

    eco_.process(input, scratch_a_.data(), numSamples);
    standard_.process(input, scratch_b_.data(), numSamples);

    const int fade_total = std::max(1, fade_length_);
    for (int i = 0; i < numSamples; ++i)
    {
      const float t = std::clamp(
        1.0f - static_cast<float>(fade_remaining_ - i) / static_cast<float>(fade_total), 0.0f, 1.0f);
      // target 1 means Standard; target 0 means Eco.
      const float wet = target_ == 1
        ? (scratch_b_[static_cast<size_t>(i)] * t + scratch_a_[static_cast<size_t>(i)] * (1.0f - t))
        : (scratch_a_[static_cast<size_t>(i)] * t + scratch_b_[static_cast<size_t>(i)] * (1.0f - t));
      output[i] = wet;
    }
    fade_remaining_ -= numSamples;
    if (fade_remaining_ <= 0)
    {
      fade_remaining_ = 0;
      current_ = target_;
    }
  }

private:
  NamA2Core standard_;
  NamA2Core eco_;
  bool has_eco_ = false;
  int current_ = 1;
  int target_ = 1;
  int fade_remaining_ = 0;
  int fade_length_ = 0;
  std::vector<float> scratch_a_;
  std::vector<float> scratch_b_;
};

} // namespace namforge
