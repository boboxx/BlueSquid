#pragma once

#include <Arduino.h>

#include "OutputController.h"

struct RgbwBleDriverState {
  uint8_t channels[5]{}; // R, G, B, WW, CW (persisted ID order)
  uint8_t color[3]{};
  uint8_t brightness = 0;
  uint8_t options = 0;
  bool independentChannels = false; // Assignment policy, not peripheral feedback.
};

// Hardware-specific adapters queue commands and expose confirmed feedback.
class RgbwBleDriverAdapter {
 public:
  virtual ~RgbwBleDriverAdapter() = default;
  virtual void begin() = 0;
  virtual void update() = 0;
  virtual bool ready() const = 0;
  virtual bool send(const RgbwBleDriverState& state) = 0;
  virtual bool available() const = 0;
  virtual bool reported(RgbwBleDriverState& state, uint32_t& revision) const = 0;
};

class RgbwBleDriverManager {
 public:
  explicit RgbwBleDriverManager(OutputController& outputs);

  void setAdapter(uint8_t zone, RgbwBleDriverAdapter* adapter, uint8_t channel = 255);
  void setAccessoryAdapter(uint8_t accessory, RgbwBleDriverAdapter* adapter,
                           uint8_t channel);
  void clearAssignments();
  void begin();
  bool update(); // True when device feedback changes a published output.
  void availability(uint8_t& assigned, uint8_t& available) const;

 private:
  static constexpr uint8_t kRgbwZoneCount = 4;
  static constexpr uint8_t kAccessoryCount = 4;
  static constexpr uint8_t kAdapterCount = 5;
  static constexpr uint32_t kSettleMs = 40;
  static constexpr uint32_t kRetryMs = 100;

  RgbwBleDriverState collectFor(RgbwBleDriverAdapter* adapter) const;
  static bool equal(const RgbwBleDriverState& left,
                    const RgbwBleDriverState& right);

  OutputController& outputs_;
  struct ChannelAssignment {
    RgbwBleDriverAdapter* adapter = nullptr;
    uint8_t channel = 0;
  };
  RgbwBleDriverAdapter* rgbwAdapters_[kRgbwZoneCount]{};
  uint8_t rgbwChannels_[4]{255,255,255,255};
  ChannelAssignment accessoryAdapters_[kAccessoryCount]{};
  RgbwBleDriverAdapter* adapters_[kAdapterCount]{};
  RgbwBleDriverState desired_[kAdapterCount]{};
  uint32_t changedMs_[kAdapterCount]{};
  uint32_t lastAttemptMs_[kAdapterCount]{};
  bool pending_[kAdapterCount]{};
  uint32_t reportRevision_[kAdapterCount]{};
  void applyReport(RgbwBleDriverAdapter* adapter, const RgbwBleDriverState& state);
  bool hostsAccessory(const RgbwBleDriverAdapter* adapter) const;

  void registerAdapter(RgbwBleDriverAdapter* adapter);
};
