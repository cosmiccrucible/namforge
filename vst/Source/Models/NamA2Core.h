#pragma once

// Framework-free inference for the compact A2/ConcatWaveNet subset used by
// NAM Forge. One instance owns one model. No allocation happens in process().

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__AVX2__)
  #include <immintrin.h>
#endif

#if defined(_MSC_VER)
  #define NAM_RESTRICT __restrict
#else
  #define NAM_RESTRICT __restrict__
#endif

namespace namforge {

struct ModelDesc
{
  const char* name = "NamModel";
  int num_layers = 0;
  int channels = 0;
  int input_channels = 0; // audio + encoded model params
  int head_kernel = 16;
  float head_scale = 1.0f;
  float leaky_slope = 0.01f;
  int activation = 0; // 0=LeakyReLU, 1=Tanh, 2=ReLU, 3=Sigmoid
  const int* kernel_sizes = nullptr;
  const int* dilations = nullptr;
  const float* param_min = nullptr;
  const float* param_max = nullptr;
  const float* weights = nullptr; // exact .nam export order
};

class NamA2Core
{
public:
  static constexpr int kMaxChannels = 16;
  static constexpr int kMaxInputChannels = 8;
  static constexpr int kMaxParams = 7;

  NamA2Core() = default;
  explicit NamA2Core(const ModelDesc& desc) { load(desc); }

  void load(const ModelDesc& desc)
  {
    desc_ = desc;
    layers_.clear();
    rechannel_.clear();
    head_w_.clear();
    head_hist_.clear();
    head_pos_ = 0;
    head_hist_size_ = std::max(1, desc.head_kernel);
    head_hist_.assign(static_cast<size_t>(desc.channels) * head_hist_size_, 0.0f);
    prepared_ = false;
    loaded_ = false;

    if (desc.num_layers <= 0 || desc.channels <= 0 || desc.channels > kMaxChannels
        || desc.input_channels <= 0 || desc.input_channels > kMaxInputChannels
        || desc.head_kernel <= 0 || desc.head_kernel > 64 || desc.kernel_sizes == nullptr
        || desc.dilations == nullptr || desc.weights == nullptr)
    {
      return;
    }

    size_t w = 0;
    rechannel_.resize(static_cast<size_t>(desc.channels) * desc.input_channels);
    for (size_t i = 0; i < rechannel_.size(); ++i) rechannel_[i] = desc.weights[w++];

    layers_.resize(static_cast<size_t>(desc.num_layers));
    for (int li = 0; li < desc.num_layers; ++li)
    {
      Layer& L = layers_[static_cast<size_t>(li)];
      L.kernel_size = desc.kernel_sizes[li];
      L.base_dilation = desc.dilations[li];
      L.dilation = L.base_dilation;
      if (L.kernel_size <= 0 || L.kernel_size > 64 || L.dilation <= 0) return;

      L.conv_w.resize(static_cast<size_t>(desc.channels) * desc.channels * L.kernel_size);
      // NAM order is [out][in][tap]; store [tap][in][out] so the AVX2 path
      // can accumulate all output channels with one FMA per input channel.
      for (int o = 0; o < desc.channels; ++o)
        for (int c = 0; c < desc.channels; ++c)
          for (int tap = 0; tap < L.kernel_size; ++tap)
            L.conv_w[((static_cast<size_t>(tap) * desc.channels + c) * desc.channels) + o] =
              desc.weights[w++];
      L.conv_b.resize(static_cast<size_t>(desc.channels));
      for (size_t i = 0; i < L.conv_b.size(); ++i) L.conv_b[i] = desc.weights[w++];
      L.mix_w.resize(static_cast<size_t>(desc.channels) * desc.input_channels);
      for (size_t i = 0; i < L.mix_w.size(); ++i) L.mix_w[i] = desc.weights[w++];
      L.l1_w.resize(static_cast<size_t>(desc.channels) * desc.channels);
      for (size_t i = 0; i < L.l1_w.size(); ++i) L.l1_w[i] = desc.weights[w++];
      L.l1_b.resize(static_cast<size_t>(desc.channels));
      for (size_t i = 0; i < L.l1_b.size(); ++i) L.l1_b[i] = desc.weights[w++];

      L.hist_size = (L.kernel_size - 1) * L.dilation + 1;
      L.hist.assign(static_cast<size_t>(desc.channels) * L.hist_size, 0.0f);
      L.pos = 0;
    }

    head_w_.resize(static_cast<size_t>(desc.channels) * desc.head_kernel);
    // NAM order [in][tap] -> [tap][in].
    for (int c = 0; c < desc.channels; ++c)
      for (int tap = 0; tap < desc.head_kernel; ++tap)
        head_w_[static_cast<size_t>(tap) * desc.channels + c] = desc.weights[w++];
    head_b_ = desc.weights[w++];
    // The final weight is head_scale in the .nam blob.
    if (desc.weights[w] != desc.head_scale)
      head_scale_ = desc.weights[w];
    else
      head_scale_ = desc.head_scale;

    warmup_samples_ = 1;
    for (int li = 0; li < desc.num_layers; ++li)
      warmup_samples_ += (desc.kernel_sizes[li] - 1) * desc.dilations[li];
    warmup_samples_ += desc.head_kernel - 1;
    warmup_samples_ = std::max(0, warmup_samples_ - 1);

    for (int p = 0; p < kMaxParams; ++p)
    {
      const float lo = desc.param_min ? desc.param_min[p] : 0.0f;
      const float hi = desc.param_max ? desc.param_max[p] : 1.0f;
      params_[p] = 0.5f * (lo + hi);
    }
    loaded_ = true;
  }

  bool loaded() const { return loaded_; }
  int numParams() const { return loaded_ ? desc_.input_channels - 1 : 0; }
  int numLayers() const { return desc_.num_layers; }
  int channels() const { return desc_.channels; }
  int latencySamples() const { return 0; }

  void prepare(double /*sampleRate*/, int /*maxBlockSize*/, int dilationScale = 1)
  {
    if (!loaded_)
      return;

    dilation_scale_ = std::max(1, dilationScale);

    // The model was trained at 48 kHz. Oversampling runs it at 48k * scale,
    // so the dilations are scaled to preserve the receptive field's time
    // constant. History lengths must be rebuilt for the new scale.
    warmup_samples_ = 1;
    for (Layer& L : layers_)
    {
      L.dilation = L.base_dilation * dilation_scale_;
      L.hist_size = (L.kernel_size - 1) * L.dilation + 1;
      L.hist.assign(static_cast<size_t>(desc_.channels) * L.hist_size, 0.0f);
      L.pos = 0;
      warmup_samples_ += (L.kernel_size - 1) * L.dilation;
    }
    warmup_samples_ += desc_.head_kernel - 1;
    warmup_samples_ = std::max(0, warmup_samples_ - 1);

    reset();
    prepared_ = true;
  }

  int dilationScale() const { return dilation_scale_; }

  void reset()
  {
    for (Layer& L : layers_)
    {
      std::fill(L.hist.begin(), L.hist.end(), 0.0f);
      L.pos = 0;
    }
    std::fill(head_hist_.begin(), head_hist_.end(), 0.0f);
    head_pos_ = 0;
    if (!loaded_)
      return;

    // Match NAM's pad_start=True: pre-roll zeros through the full receptive
    // field and discard the outputs. Without this the first samples of the
    // model carry the zero-state transient rather than the true steady state.
    if (warmup_samples_ > 0)
    {
      prepared_ = true;
      float zero = 0.0f;
      float sink = 0.0f;
      for (int i = 0; i < warmup_samples_; ++i)
        process(&zero, &sink, 1);
    }
  }

  void setModelParam(int index, float value)
  {
    if (index < 0 || index >= numParams())
      return;
    const float lo = desc_.param_min ? desc_.param_min[index] : 0.0f;
    const float hi = desc_.param_max ? desc_.param_max[index] : 1.0f;
    params_[index] = std::clamp(value, std::min(lo, hi), std::max(lo, hi));
  }

  float modelParam(int index) const
  {
    return (index >= 0 && index < numParams()) ? params_[index] : 0.0f;
  }

  // Mono in/out. The caller is responsible for the host sample-rate conversion.
  void process(const float* input, float* output, int numSamples) noexcept
  {
    if (!prepared_ || !loaded_)
    {
      if (output != nullptr)
        for (int i = 0; i < numSamples; ++i) output[i] = 0.0f;
      return;
    }

    const int C = desc_.channels;
    const int IC = desc_.input_channels;

    for (int n = 0; n < numSamples; ++n)
    {
      float cond[kMaxInputChannels] = {0.0f};
      cond[0] = input[n];
      for (int p = 0; p < numParams(); ++p)
      {
        const float lo = desc_.param_min ? desc_.param_min[p] : 0.0f;
        const float hi = desc_.param_max ? desc_.param_max[p] : 1.0f;
        const float span = (hi - lo) != 0.0f ? (hi - lo) : 1.0f;
        const float norm = 2.0f * (params_[p] - lo) / span - 1.0f;
        cond[1 + p] = norm;
      }

      float layer_in[kMaxChannels] = {0.0f};
      for (int o = 0; o < C; ++o)
      {
        float v = 0.0f;
        const float* rw = &rechannel_[static_cast<size_t>(o) * IC];
        for (int i = 0; i < IC; ++i) v += rw[i] * cond[i];
        layer_in[o] = v;
      }

      float head_acc[kMaxChannels] = {0.0f};

      for (int li = 0; li < desc_.num_layers; ++li)
      {
        Layer& L = layers_[static_cast<size_t>(li)];
        const int K = L.kernel_size;
        const int D = L.dilation;
        const int HS = L.hist_size;
        const int pos = L.pos;

        for (int c = 0; c < C; ++c)
          L.hist[static_cast<size_t>(pos) * C + c] = layer_in[c];

        float z[kMaxChannels] = {0.0f};
#if defined(__AVX2__)
        if (C == 8)
        {
          __m256 acc = _mm256_loadu_ps(L.conv_b.data());
          for (int tap = 0; tap < K; ++tap)
          {
            int idx = pos - (K - 1 - tap) * D;
            if (idx < 0) idx += HS;
            const float* NAM_RESTRICT hptr = &L.hist[static_cast<size_t>(idx) * C];
            for (int c = 0; c < C; ++c)
            {
              const __m256 w = _mm256_loadu_ps(
                &L.conv_w[(static_cast<size_t>(tap) * C + c) * C]);
              acc = _mm256_fmadd_ps(_mm256_set1_ps(hptr[c]), w, acc);
            }
          }
          float zc[8];
          _mm256_storeu_ps(zc, acc);
          for (int o = 0; o < C; ++o)
          {
            float v = zc[o];
            for (int i = 0; i < IC; ++i)
              v += L.mix_w[static_cast<size_t>(o) * IC + i] * cond[i];
            z[o] = activate(v);
          }
        }
        else if (C == 4)
        {
          __m128 acc = _mm_loadu_ps(L.conv_b.data());
          for (int tap = 0; tap < K; ++tap)
          {
            int idx = pos - (K - 1 - tap) * D;
            if (idx < 0) idx += HS;
            const float* NAM_RESTRICT hptr = &L.hist[static_cast<size_t>(idx) * C];
            for (int c = 0; c < C; ++c)
            {
              const __m128 w = _mm_loadu_ps(
                &L.conv_w[(static_cast<size_t>(tap) * C + c) * C]);
              acc = _mm_fmadd_ps(_mm_set1_ps(hptr[c]), w, acc);
            }
          }
          float zc[4];
          _mm_storeu_ps(zc, acc);
          for (int o = 0; o < C; ++o)
          {
            float v = zc[o];
            for (int i = 0; i < IC; ++i)
              v += L.mix_w[static_cast<size_t>(o) * IC + i] * cond[i];
            z[o] = activate(v);
          }
        }
        else
#endif
        {
          for (int o = 0; o < C; ++o)
          {
            float acc = L.conv_b[o];
            for (int tap = 0; tap < K; ++tap)
            {
              int idx = pos - (K - 1 - tap) * D;
              if (idx < 0) idx += HS;
              const float* NAM_RESTRICT hptr = &L.hist[static_cast<size_t>(idx) * C];
              for (int c = 0; c < C; ++c)
                acc += L.conv_w[(static_cast<size_t>(tap) * C + c) * C + o] * hptr[c];
            }
            for (int i = 0; i < IC; ++i)
              acc += L.mix_w[static_cast<size_t>(o) * IC + i] * cond[i];
            z[o] = activate(acc);
          }
        }

        float next[kMaxChannels] = {0.0f};
        for (int o = 0; o < C; ++o)
        {
          float acc = L.l1_b[o];
          for (int c = 0; c < C; ++c) acc += L.l1_w[static_cast<size_t>(o) * C + c] * z[c];
          next[o] = layer_in[o] + acc;
          head_acc[o] += z[o];
        }

        for (int c = 0; c < C; ++c) layer_in[c] = next[c];
        L.pos = pos + 1;
        if (L.pos >= HS) L.pos = 0;
      }

      const int HP = head_pos_;
      for (int c = 0; c < C; ++c)
        head_hist_[static_cast<size_t>(HP) * C + c] = head_acc[c];

      float y = head_b_;
      for (int tap = 0; tap < head_hist_size_; ++tap)
      {
        int idx = HP - (head_hist_size_ - 1 - tap);
        if (idx < 0) idx += head_hist_size_;
        const float* NAM_RESTRICT hptr = &head_hist_[static_cast<size_t>(idx) * C];
        const float* NAM_RESTRICT wptr = &head_w_[static_cast<size_t>(tap) * C];
        for (int c = 0; c < C; ++c)
          y += wptr[c] * hptr[c];
      }
      output[n] = y * head_scale_;

      head_pos_ = HP + 1;
      if (head_pos_ >= head_hist_size_) head_pos_ = 0;
    }
  }

private:
  struct Layer
  {
    int kernel_size = 0;
    int base_dilation = 1;
    int dilation = 1;
    int hist_size = 1;
    int pos = 0;
    std::vector<float> conv_w;
    std::vector<float> conv_b;
    std::vector<float> mix_w;
    std::vector<float> l1_w;
    std::vector<float> l1_b;
    std::vector<float> hist;
  };

  float activate(float x) const noexcept
  {
    switch (desc_.activation)
    {
      case 1: return std::tanh(x);
      case 2: return x > 0.0f ? x : 0.0f;
      case 3: return 1.0f / (1.0f + std::exp(-x));
      case 0:
      default: return x >= 0.0f ? x : x * desc_.leaky_slope;
    }
  }

  ModelDesc desc_{};
  bool loaded_ = false;
  bool prepared_ = false;
  std::vector<float> rechannel_;
  std::vector<Layer> layers_;
  std::vector<float> head_w_;
  std::vector<float> head_hist_;
  float head_b_ = 0.0f;
  float head_scale_ = 1.0f;
  int head_hist_size_ = 1;
  int head_pos_ = 0;
  int warmup_samples_ = 0;
  int dilation_scale_ = 1;
  float params_[kMaxParams] = {0.0f};
};

} // namespace namforge
