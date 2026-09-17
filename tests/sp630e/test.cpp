#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "Sp630eProtocol.h"
#include "ColourWheel.h"
#include "Sp630eChannels.h"
#include "RgbwBleDriverManager.h"
#include "TouchRemoteStatus.h"

uint32_t clockMs = 10000;
uint32_t millis() { return clockMs; }
struct Adapter : RgbwBleDriverAdapter {
  RgbwBleDriverState feedback{}, sent{};
  bool online = false, valid = false;
  uint32_t revision = 0;
  unsigned sends = 0;
  void begin() override {}
  void update() override {}
  bool ready() const override { return true; }
  bool available() const override { return online; }
  bool reported(RgbwBleDriverState& state, uint32_t& rev) const override {
    state = feedback; rev = revision; return online && valid;
  }
  bool send(const RgbwBleDriverState& state) override {
    sent = state; ++sends; valid = false; return true;
  }
  void report(const RgbwBleDriverState& state) {
    feedback = state; online = valid = true; ++revision;
  }
};

// Model the independent SP630E registers, including a retained colour while off.
void applyPackets(uint8_t* status, const Sp630eProtocol::WarmCommands& commands) {
  for (size_t i = 0; i < commands.count; ++i) {
    const auto& p = commands.packets[i];
    switch (p.data[1]) {
      case 0x50: status[29] = p.data[6]; break;
      case 0x0A: status[24] = p.data[6]; break;
      case 0x53: status[32] = p.data[6]; break;
      case 0x52: memcpy(status + 37, p.data + 6, 3); status[35] = p.data[9]; break;
      case 0x61: status[40] = p.data[6]; status[41] = p.data[7]; break;
      case 0x51: status[p.data[6] ? 36 : 35] = p.data[7]; break;
      default: assert(false);
    }
  }
}

void protocol() {
  uint8_t packet[53]{};
  packet[0] = 0x53; packet[1] = 2; packet[3] = 1; packet[5] = 47;
  packet[19] = 0x87; packet[29] = 1; packet[32] = 1;
  packet[35] = 128; packet[36] = 64; packet[37] = 255; packet[38] = 128;
  Sp630eProtocol::Status status;
  assert(Sp630eProtocol::decode(packet, sizeof packet, status));
  assert(status.power && status.channels[0] == 50 && status.channels[1] == 25);
  assert(status.channels[3] == 0 && status.brightness == 50 && status.options == 1);
  packet[24] = 1;
  assert(Sp630eProtocol::decode(packet, sizeof packet, status));
  assert(status.channels[3] == 25 && status.options == 3);
  packet[29] = 0;
  assert(Sp630eProtocol::decode(packet, sizeof packet, status));
  assert(!status.power && status.channels[0] == 0 && status.channels[3] == 0);
  assert(status.color[0] == 100 && status.brightness == 50);
  packet[29] = 1; packet[32] = 2; packet[24] = 0;
  assert(Sp630eProtocol::decode(packet, sizeof packet, status));
  assert(status.channels[0] == 0 && status.channels[3] == 25 && status.brightness == 25);
  packet[19] = 0x88; packet[32] = 3; packet[47] = 0; packet[48] = 255;
  assert(Sp630eProtocol::decode(packet, sizeof packet, status));
  assert(status.channels[0] == 0 && status.channels[1] == 50 && status.channels[3] == 0);
  packet[32] = 6;
  assert(Sp630eProtocol::decode(packet, sizeof packet, status));
  assert(status.channels[3] == 100 && status.brightness == 100);
  for (unsigned level = 0; level <= 100; ++level)
    assert(Sp630eProtocol::percent(level * 255U / 100U) == level);
  for (size_t length = 0; length < sizeof packet; ++length)
    assert(!Sp630eProtocol::decode(packet, length, status));
  assert(!Sp630eProtocol::decode(nullptr, 53, status));
  packet[2] = 1; assert(!Sp630eProtocol::decode(packet, 53, status)); packet[2] = 0;
  packet[5] = 48; assert(!Sp630eProtocol::decode(packet, 53, status)); packet[5] = 47;
  packet[1] = 0x50; assert(!Sp630eProtocol::decode(packet, 53, status)); packet[1] = 2;
  packet[19] = 0x83; assert(!Sp630eProtocol::decode(packet, 53, status));
  assert(!Sp630eProtocol::fresh(false, 0, 1));
  assert(Sp630eProtocol::fresh(true, 100, 8099));
  assert(!Sp630eProtocol::fresh(true, 100, 8100));
  assert(Sp630eProtocol::fresh(true, UINT32_MAX - 100, 100));
}
void rgbWarm() {
  // Captured V4.0.27 prefix from the user's log. The unlogged tail is padded;
  // all fields consumed by this decoder occur within the captured prefix.
  uint8_t packet[181] = {
    0x53,0x02,0x00,0x01,0x00,0xAF,0x00,0x01,0x11,0x00,0x46,
    0x56,0x34,0x2E,0x30,0x2E,0x32,0x37,0x20,0x8A,0x03,0x03,
    0x00,0x3C,0x01,0x00,0x02,0x49,0x00,0x00,0x00,0x00,0x01,
    0x01,0x01,0xFF,0x03,0xFF,0x84,0x60,0x00,0xFF,0x0A,0x1E,
    0x01,0x10,0x00,0xFF,0x00,0x00,0x00,0xFF,0x01};
  Sp630eProtocol::Status report;
  assert(Sp630eProtocol::decode(packet, sizeof packet, report));
  assert(report.configuration == 0x8A && !report.power);
  assert(report.channels[0] == 0 && report.channels[3] == 0);
  packet[29] = 1;
  assert(Sp630eProtocol::decode(packet, sizeof packet, report));
  assert(report.channels[0] == 100 && report.channels[3] == 100);
  // CW alone must never appear as light on the physically connected WW strip.
  packet[40] = 255; packet[41] = 0;
  assert(Sp630eProtocol::decode(packet, sizeof packet, report));
  assert(report.channels[3] == 0);
  packet[24] = 0; packet[32] = 2; packet[41] = 128; packet[36] = 128;
  assert(Sp630eProtocol::decode(packet, sizeof packet, report));
  assert(report.channels[0] == 0 && report.channels[3] == 25);
  // Exercise the actual command builder and decode its equivalent status,
  // including mixed levels so master intensity is not applied twice.
  const uint8_t scenarios[][4] = {{0,0,0,60}, {20,40,10,80}, {100,0,0,0}, {0,0,0,0}};
  for (const auto& channels : scenarios) {
    const auto commands = Sp630eProtocol::rgbWarmCommands(channels, 3);
    assert(commands.packets[0].data[1] == 0x50);
    if (!commands.packets[0].data[6]) { assert(commands.count == 1); continue; }
    const bool rgbOn = channels[0] || channels[1] || channels[2];
    assert(commands.count == (rgbOn ? (channels[3] ? 6U : 4U) : 5U));
    applyPackets(packet, commands);
    assert(Sp630eProtocol::decode(packet, sizeof packet, report));
    for (unsigned i = 0; i < 4; ++i) {
      const int error = int(report.channels[i]) - int(channels[i]);
      assert(error >= -1 && error <= 1);
    }
  }
  const uint8_t both[] = {100,0,0,100};
  const auto rgbOnly = Sp630eProtocol::rgbWarmCommands(both, 1);
  assert(rgbOnly.count == 4 && rgbOnly.packets[1].data[6] == 0);
  const auto whiteOnly = Sp630eProtocol::rgbWarmCommands(both, 2);
  assert(whiteOnly.packets[2].data[6] == 2 && whiteOnly.packets[3].data[7] == 255);
}
void rapidChangesKeepLinkAlive() {
  Sp630eProtocol::ResponseHealth health;
  assert(!health.available(0));
  assert(health.lastReplyOr(123) == 123);
  // Keep new commands ahead of feedback for a full minute, longer than the
  // old eight-second timeout. Only contact freshness should advance.
  for (uint32_t now = 0; now <= 60000; now += 1000) {
    health.received(now);
    assert(health.available(now + 999));
    assert(health.lastReplyOr(0) == now);
  }
  // A busy UI must not extend freshness after real replies stop.
  assert(health.available(67999));
  assert(!health.available(68000));
  health.received(70000);
  assert(health.available(70000));
  health.disconnected();
  assert(!health.available(70001));
  assert(health.lastReplyOr(71000) == 71000);
  health.received(UINT32_MAX - 100);
  assert(health.available(100));
  assert(!health.available(8000));
}
void manager() {
  OutputController outputs;
  outputs.value.rgbw[0][0] = 100; // A persisted request must not override boot feedback.
  Adapter adapter;
  RgbwBleDriverManager manager(outputs);
  manager.setAdapter(0, &adapter); manager.begin(); manager.update();
  assert(adapter.sends == 0);
  uint8_t assigned, available;
  manager.availability(assigned, available);
  assert(assigned == 1 && available == 0);
  RgbwBleDriverState off{}; off.color[0] = 100; off.brightness = 50; off.options = 1;
  adapter.report(off); manager.update();
  assert(outputs.value.rgbw[0][0] == 0 && outputs.value.rgbwBrightness[0] == 50);
  assert(adapter.sends == 0);
  const unsigned changes = outputs.changes;
  adapter.report(off); clockMs += 2000; manager.update();
  assert(outputs.changes == changes && adapter.sends == 0);
  // A newly received older report cannot overwrite an outbound user change.
  outputs.setRgbwChannel(RgbwZone::Output1, 0, 50);
  adapter.report(off); manager.update();
  assert(outputs.value.rgbw[0][0] == 50);
  clockMs += 101; manager.update();
  assert(adapter.sends == 1 && adapter.sent.channels[0] == 50);
  auto on = adapter.sent;
  manager.update(); assert(outputs.value.rgbw[0][0] == 50);
  adapter.report(on); manager.update(); assert(adapter.sends == 1);
  adapter.online = false; manager.availability(assigned, available);
  assert(assigned == 1 && available == 0);
  adapter.report(off); manager.update();
  assert(outputs.value.rgbw[0][0] == 0 && adapter.sends == 1);
  manager.availability(assigned, available); assert(available == 1);
}
void sharedAssignments() {
  OutputController outputs; Adapter adapter, other; RgbwBleDriverManager manager(outputs);
  manager.setAdapter(1, &other);
  manager.setAdapter(2, &adapter, 0); manager.setAccessoryAdapter(1, &adapter, 3);
  manager.begin();
  RgbwBleDriverState state{}; state.channels[0] = 30; state.channels[3] = 75;
  state.color[0] = 100; state.brightness = 75; state.options = 3;
  other.report(state);
  adapter.report(state); manager.update();
  assert(outputs.value.rgbw[2][3] == 30 && outputs.value.waterPumpEnabled);
  const unsigned changes = outputs.changes;
  adapter.report(state); manager.update(); assert(outputs.changes == changes);
  uint8_t assigned, available; manager.availability(assigned, available);
  assert(assigned == 0x26 && available == 0x26);
  adapter.online = false; manager.availability(assigned, available); assert(available == 2);
  TouchRemoteStatus touch;
  touch.sp630eAssigned = assigned; touch.sp630eAvailable = available;
  assert(!touch.outputAvailable(2) && !touch.outputAvailable(5));
  assert(touch.outputAvailable(0)); // Unassigned outputs remain usable.
  touch.sp630eAvailable = assigned;
  assert(touch.outputAvailable(2) && touch.outputAvailable(5));
}
void changedPacketsOnly() {
  using namespace Sp630eProtocol;
  WarmCommandCache cache;
  uint8_t levels[4] = {100, 0, 0, 25};
  auto desired = rgbWarmCommands(levels, 3);
  auto batch = cache.plan(desired);
  assert(batch.count == 6 && batch.packets[0].data[1] == 0x50);
  cache.committed(desired);
  assert(cache.plan(desired).count == 0);
  levels[1] = 50;
  desired = rgbWarmCommands(levels, 3);
  batch = cache.plan(desired);
  assert(batch.count == 1 && batch.packets[0].data[1] == 0x52);
  // A failed write is not committed: the same update must remain pending.
  assert(cache.plan(desired).count == 1);
  cache.committed(desired);
  levels[3] = 50;
  desired = rgbWarmCommands(levels, 3);
  batch = cache.plan(desired);
  assert(batch.count == 1 && batch.packets[0].data[1] == 0x61);
  assert(batch.packets[0].data[6] == 0); // CW stays off.
  cache.committed(desired);
  uint8_t status[53]{};
  status[24] = 1; status[29] = 1; status[32] = 1;
  status[36] = 255;
  status[35] = 255; status[37] = 255; status[38] = 127; status[41] = 127;
  cache.observe(status);
  assert(cache.plan(desired).count == 0);
  status[32] = 2;
  cache.observe(status);
  batch = cache.plan(desired);
  assert(batch.count == 1 && batch.packets[0].data[1] == 0x53);
  cache.committed(desired);
  status[32] = 1;
  status[29] = 0; // A reported power-off requires full setup after waking.
  cache.observe(status);
  batch = cache.plan(desired);
  assert(batch.count == desired.count && batch.packets[0].data[1] == 0x50);
  cache.committed(desired);
  const uint8_t off[4]{};
  auto offCommands = rgbWarmCommands(off, 3);
  batch = cache.plan(offCommands);
  assert(batch.count == 3 && batch.packets[2].data[1] == 0x50 &&
         batch.packets[2].data[6] == 0);
  cache.committed(offCommands);
  batch = cache.plan(desired);
  assert(batch.count == desired.count && batch.packets[0].data[6] == 1);
  cache.observe(status);
  status[41] = 0;
  cache.observe(status);
  cache.committed(offCommands);
  batch = cache.plan(desired);
  assert(batch.count == desired.count && batch.packets[0].data[1] == 0x50);
  auto different = desired;
  different.packets[3].data[6] = 0;
  batch = cache.plan(different);
  assert(batch.count == different.count && batch.packets[0].data[1] == 0x50);
  cache.reset();
  assert(cache.plan(desired).count == 6);
  cache.committed(offCommands);
  assert(cache.plan(desired).count == 6);
  cache.committed(desired);
  assert(cache.plan(desired).count == 0);

}
void warmWhiteIntensitySweep() {
  using namespace Sp630eProtocol;
  for (const bool withRgb : {false, true}) {
    WarmCommandCache cache;
    uint8_t whiteComponent = 0, whiteBrightness = 3;
    for (int percentLevel = 100; percentLevel > 0; --percentLevel) {
      const uint8_t level = static_cast<uint8_t>(percentLevel);
      const uint8_t channels[] = {static_cast<uint8_t>(withRgb ? level : 0), 0, 0, level};
      const auto desired = rgbWarmCommands(channels, withRgb ? 3 : 2);
      const auto changes = cache.plan(desired);
      bool wroteBrightness = false;
      for (size_t i = 0; i < changes.count; ++i) {
        const auto& p = changes.packets[i];
        if (p.data[1] == 0x61) {
          assert(p.data[6] == 0);
          whiteComponent = p.data[7];
        }
        if (p.data[1] == 0x51) {
          assert(p.data[6] == 1);
          whiteBrightness = p.data[7];
          wroteBrightness = true;
        }
      }
      assert(wroteBrightness);
      const int physicalWhite = (unsigned(whiteComponent) * whiteBrightness * 100 + 32512) / 65025;
      assert(physicalWhite >= percentLevel - 1 && physicalWhite <= percentLevel + 1);
      cache.committed(desired);
    }
  }
}
void continuousGesturesAreNotStarved() {
  OutputController outputs; Adapter adapter;
  RgbwBleDriverManager manager(outputs);
  manager.setAdapter(0, &adapter);
  manager.begin();
  // Keep changing faster than the coalescing window for longer than the
  // availability timeout. A trailing debounce would send nothing at all.
  for (unsigned i = 0; i < 1000; ++i) {
    outputs.value.rgbw[0][0] = 1 + i % 100;
    clockMs += 10;
    manager.update();
  }
  assert(adapter.sends >= 90 && adapter.sends <= 101);
  clockMs += 101;
  manager.update();
  assert(adapter.sent.channels[0] == outputs.value.rgbw[0][0]);
}
void singleChannelRouting() {
  OutputController outputs; Adapter adapter;
  RgbwBleDriverManager manager(outputs);
  manager.setAdapter(2, &adapter, 0);
  assert(outputs.externalRgbw[2] && !outputs.externalRgbw[3]);
  manager.begin();
  outputs.setRgbwChannel(RgbwZone::Output3, 3, 100);
  manager.update(); clockMs += 101; manager.update();
  assert(adapter.sends == 1 && adapter.sent.channels[0] == 100);
  outputs.setRgbwChannel(RgbwZone::Output3, 3, 0);
  manager.update(); clockMs += 101; manager.update();
  assert(adapter.sends == 2 && adapter.sent.channels[0] == 0);
  manager.clearAssignments();
  assert(!outputs.externalRgbw[2]);
}
void separateWhiteAssignments() {
  assert(Sp630eChannels::ids[3] == 4 && Sp630eChannels::ids[4] == 3);
  for (uint8_t i = 0; i < 5; ++i)
    assert(Sp630eChannels::position(Sp630eChannels::ids[i]) == i);
  const uint8_t channels[4] = {0, 0, 0, 25};
  const auto commands = Sp630eProtocol::rgbWarmCommands(channels, 2, 75);
  uint8_t packet[53]{};
  packet[0] = 0x53; packet[1] = 2; packet[5] = 47;
  packet[19] = 0x8A;
  applyPackets(packet, commands);
  Sp630eProtocol::Status status;
  assert(Sp630eProtocol::decode(packet, sizeof packet, status));
  assert(status.channels[4] == 75 && status.channels[3] == 25);
  OutputController outputs; Adapter adapter; RgbwBleDriverManager manager(outputs);
  manager.setAdapter(2, &adapter, 4);
  manager.setAdapter(3, &adapter, 3);
  manager.begin();
  RgbwBleDriverState report{};
  report.channels[4] = 75; report.channels[3] = 25;
  adapter.report(report); manager.update();
  assert(outputs.value.rgbw[2][3] == 75 && outputs.value.rgbw[3][3] == 25);
  outputs.value.rgbw[2][3] = 50;
  manager.update(); clockMs += 101; manager.update();
  assert(adapter.sent.channels[4] == 50 && adapter.sent.channels[3] == 25);
}
void fullColourWheel() {
  const auto centre = ColourWheel::gradientColour(0.24f,0);
  assert(centre.r==255 && centre.g==255 && centre.b==255);
  const auto edge = ColourWheel::gradientColour(0.8f,0);
  assert(edge.r==255 && edge.g==0 && edge.b==0);
  const auto blend1 = ColourWheel::gradientColour(0.4f,0);
  const auto blend2 = ColourWheel::gradientColour(0.41f,0);
  assert(blend1.g>blend2.g && blend1.g-blend2.g<10);
  for (float x : {0.0f, 0.12f, 0.24f}) {
    const auto white = ColourWheel::reducedColour(x,0);
    assert(white.r == 255 && white.g == 255 && white.b == 255);
  }
  for (int h=0; h<360; h+=30) {
    const float angle = h*3.14159265358979323846f/180;
    for (float radius : {0.4f,0.7f,0.95f,1.1f}) {
      const auto selected = ColourWheel::reducedColour(std::cos(angle)*radius,std::sin(angle)*radius);
      float x,y;
      ColourWheel::reducedPosition(selected.r,selected.g,selected.b,x,y);
      assert(std::sqrt(x*x+y*y)<0.9f);
      const auto restored = ColourWheel::reducedColour(x,y);
      assert(selected.r==restored.r && selected.g==restored.g && selected.b==restored.b);
      // A small finger movement stays within the same colour sector.
      const auto nearby = ColourWheel::reducedColour(std::cos(angle+0.08f)*radius,std::sin(angle+0.08f)*radius);
      assert(selected.r==nearby.r && selected.g==nearby.g && selected.b==nearby.b);
    }
  }
  const auto white = ColourWheel::colour(0,0);
  assert(white.r == 255 && white.g == 255 && white.b == 255);
  const auto red = ColourWheel::colour(1,0);
  assert(red.r == 255 && red.g == 0 && red.b == 0);
  const auto cyan = ColourWheel::colour(-1,0);
  assert(cyan.r == 0 && cyan.g == 255 && cyan.b == 255);
  const auto pastel = ColourWheel::colour(0.5f,0);
  assert(pastel.r == 255 && pastel.g == 128 && pastel.b == 128);
  for (int h=0;h<360;h+=15) {
    const float angle = h*3.14159265358979323846f/180;
    for (float saturation : {0.25f,0.5f,1.0f}) {
      const auto first = ColourWheel::colour(std::cos(angle)*saturation,std::sin(angle)*saturation);
      float x,y; ColourWheel::position(first.r,first.g,first.b,x,y);
      const auto second = ColourWheel::colour(x,y);
      assert(std::abs(int(first.r)-second.r)<=1 && std::abs(int(first.g)-second.g)<=1 &&
             std::abs(int(first.b)-second.b)<=1);
    }
  }
}
void whiteOnlyRetainsColour() {
  OutputController outputs; Adapter adapter; RgbwBleDriverManager manager(outputs);
  outputs.value.rgb[0][0] = 100;
  outputs.value.rgb[0][1] = 25;
  manager.setAdapter(0, &adapter); manager.begin();
  RgbwBleDriverState white{};
  white.channels[3] = 60; white.brightness = 60; white.options = 3;
  adapter.report(white); manager.update();
  assert(outputs.value.rgbw[0][0] == 0 && outputs.value.rgbw[0][3] == 60);
  assert(outputs.value.rgbwOptions[0] == 2);
  assert(outputs.value.rgb[0][0] == 100 && outputs.value.rgb[0][1] == 25);
  RgbwBleDriverState off{};
  adapter.report(off); manager.update();
  assert(outputs.value.rgbw[0][3] == 0 && outputs.value.rgb[0][0] == 100);
  assert(adapter.sends == 0); // Feedback must not echo a new output command.
}
void configurableLightTypes() {
  assert(Sp630eChannels::lightChannel(0) == 255);
  for (uint8_t choice = 0; choice < Sp630eChannels::lightChoiceCount; ++choice)
    assert(Sp630eChannels::lightPosition(Sp630eChannels::lightChannel(choice)) == choice);
  // Three lights share independently assigned channels.
  OutputController outputs; Adapter adapter; RgbwBleDriverManager manager(outputs);
  manager.setAdapter(0, &adapter, 4);
  manager.setAdapter(1, &adapter, 0);
  manager.setAdapter(2, &adapter, 3);
  manager.begin();
  RgbwBleDriverState state{};
  state.channels[4] = 35; state.channels[0] = 60; state.channels[3] = 20;
  state.options = 3;
  adapter.report(state); manager.update();
  assert(outputs.value.rgbw[0][3] == 35 && outputs.value.rgbw[1][3] == 60);
  assert(outputs.value.rgbw[2][3] == 20);
  outputs.setRgbwChannel(RgbwZone::Output1, 3, 80);
  manager.update(); clockMs += 101; manager.update();
  assert(adapter.sent.channels[4] == 80);
  assert(adapter.sent.channels[0] == 60 && adapter.sent.channels[3] == 20);
  assert(adapter.sent.channels[1] == 0 && adapter.sent.channels[2] == 0);
  uint8_t assigned, available; manager.availability(assigned, available);
  assert(assigned == 7 && available == 7);

  // Each light slot can own all five channels independently.
  for (uint8_t zone = 0; zone < 4; ++zone) {
    OutputController full; Adapter strip; RgbwBleDriverManager fullManager(full);
    fullManager.setAdapter(zone, &strip); fullManager.begin();
    RgbwBleDriverState feedback{};
    feedback.channels[4] = 45; feedback.options = 2; feedback.brightness = 45;
    strip.report(feedback); fullManager.update();
    assert(full.value.rgbw[zone][3] == 45);
    assert(full.value.rgbwOptions[zone] == 4);
    full.setRgbwChannel(static_cast<RgbwZone>(zone), 3, 75);
    full.setRgbwPresetField(static_cast<RgbwZone>(zone), 4, 6);
    fullManager.update(); clockMs += 101; fullManager.update();
    assert(strip.sent.channels[3] == 75 && strip.sent.channels[4] == 75);
    fullManager.availability(assigned, available);
    assert(assigned == (1U << zone));
    full.setRgbwChannel(static_cast<RgbwZone>(zone), 3, 0);
    fullManager.update(); clockMs += 101; fullManager.update();
    for (uint8_t level : strip.sent.channels) assert(level == 0);
  }
}

void coolWhiteFeedback() {
  uint8_t packet[53]{};
  packet[0] = 0x53; packet[1] = 2; packet[3] = 1; packet[5] = 47;
  packet[19] = 0x8A; packet[29] = 1; packet[32] = 2;
  packet[36] = 128; packet[40] = 255; packet[41] = 0;
  Sp630eProtocol::Status state;
  assert(Sp630eProtocol::decode(packet, sizeof packet, state));
  assert(state.channels[4] == 50 && state.channels[3] == 0 && state.brightness == 50);
}

void wakeAppliesModeAfterPower() {
  using namespace Sp630eProtocol;
  // Capture from Controller 1.0.5: off in mode 2, 53:0101 before 50:01,
  // then powered feedback still in mode 2. Cover both possible causes:
  // mode writes ignored while off, or the old mode restored on power-on.
  for (bool restoreOnWake : {false, true}) {
    for (uint8_t options : {1, 2, 3}) {
      for (bool cached : {false, true}) {
        uint8_t status[53]{};
        status[0] = 0x53; status[1] = 2; status[5] = 47; status[19] = 0x8A;
        status[32] = options == 2 ? 1 : 2;
        status[35] = status[36] = 255;
        status[37] = status[38] = status[39] = status[41] = 255;
        const uint8_t retainedMode = status[32];
        const uint8_t levels[] = {100, 100, 100, 100};
        const auto desired = rgbWarmCommands(levels, options);
        WarmCommandCache cache;
        if (cached) {
          cache.committed(desired);
          const uint8_t off[4]{};
          cache.committed(rgbWarmCommands(off, 0));
        }
        const auto batch = cache.plan(desired);
        for (size_t i = 0; i < batch.count; ++i) {
          const auto& packet = batch.packets[i];
          if (!restoreOnWake && !status[29] && packet.data[1] == 0x53) continue;
          if (restoreOnWake && !status[29] && packet.data[1] == 0x50 && packet.data[6])
            status[32] = retainedMode;
          WarmCommands single{}; single.count = 1; single.packets[0] = packet;
          applyPackets(status, single);
        }
        Status feedback;
        assert(decode(status, sizeof status, feedback));
        assert(feedback.channels[0] == ((options & 1) ? 100 : 0));
        assert(feedback.channels[3] == ((options & 2) ? 100 : 0));
      }
    }
  }
}

void independentLedSelections() {
  for (uint8_t configuration : {0x87, 0x88, 0x8A}) {
    uint8_t packet[53]{};
    packet[0] = 0x53; packet[1] = 2; packet[5] = 47; packet[19] = configuration; packet[32] = 1;
    Sp630eProtocol::WarmCommandCache cache;
    const uint8_t levels[] = {40, 20, 0, 70};
    for (uint8_t options : {0, 2, 3, 1, 2, 0, 1, 3, 2}) {
      const auto commands = configuration == 0x8A
          ? Sp630eProtocol::rgbWarmCommands(levels, options)
          : Sp630eProtocol::rgbwCommands(levels, options);
      if (options == 1) {
        for (size_t i = 0; i < commands.count; ++i) {
          const auto& p = commands.packets[i];
          assert(p.data[1] != 0x61 && p.data[1] != 0x51);
          if (p.data[1] == 0x0A) assert(p.data[6] == 0);
        }
      }
      if (options == 2) {
        for (size_t i = 0; i < commands.count; ++i)
          assert(commands.packets[i].data[1] != 0x52);
      }
      applyPackets(packet, cache.plan(commands)); cache.committed(commands);
      Sp630eProtocol::Status state;
      assert(Sp630eProtocol::decode(packet, sizeof packet, state));
      assert(state.channels[0] == ((options & 1) ? 40 : 0));
      assert(state.channels[1] == ((options & 1) ? 20 : 0));
      assert(state.channels[2] == 0);
      assert(state.channels[3] == ((options & 2) ? 70 : 0));
    }
  }
}

void offToColourStaysColour() {
  for (uint8_t configuration : {0x87, 0x88, 0x8A}) {
    uint8_t packet[53]{};
    packet[0] = 0x53; packet[1] = 2; packet[5] = 47;
    packet[19] = configuration; packet[32] = 2;
    packet[36] = 255; packet[40] = 255; packet[41] = 255;
    OutputController outputs; Adapter adapter; RgbwBleDriverManager manager(outputs);
    manager.setAdapter(0, &adapter); manager.begin();
    outputs.setRgbwChannel(RgbwZone::Output1, 0, 65);
    outputs.setRgbwPresetField(RgbwZone::Output1, 3, 65);
    outputs.setRgbwPresetField(RgbwZone::Output1, 4, 1);
    manager.update(); clockMs += 101; manager.update();
    const auto desired = configuration == 0x8A
        ? Sp630eProtocol::rgbWarmCommands(adapter.sent.channels, adapter.sent.options)
        : Sp630eProtocol::rgbwCommands(adapter.sent.channels, adapter.sent.options);
    Sp630eProtocol::WarmCommandCache cache;
    const auto batch = cache.plan(desired);
    assert(batch.packets[0].data[1] == 0x50);
    applyPackets(packet, batch);
    assert(packet[32] == 1 && packet[24] == 0);
    assert(packet[36] == 255 && packet[41] == 255); // saved white untouched
    for (unsigned poll = 0; poll < 5; ++poll) {
      Sp630eProtocol::Status decoded;
      assert(Sp630eProtocol::decode(packet, sizeof packet, decoded));
      assert(decoded.channels[0] == 65 && decoded.channels[3] == 0 && decoded.channels[4] == 0);
      assert(decoded.options == 1);
      RgbwBleDriverState feedback{};
      memcpy(feedback.channels, decoded.channels, 5);
      memcpy(feedback.color, decoded.color, 3);
      feedback.brightness = decoded.brightness; feedback.options = decoded.options;
      adapter.report(feedback); clockMs += 2000; manager.update();
      assert(outputs.value.rgbw[0][0] == 65 && outputs.value.rgbw[0][3] == 0);
      assert(outputs.value.rgbwOptions[0] == 1);
      assert(adapter.sends == 1);
    }
  }
}

void offToWhiteStaysWhite() {
  for (uint8_t configuration : {0x87, 0x88, 0x8A}) {
    for (uint8_t whiteChannel : {3, 4}) {
      if (configuration != 0x8A && whiteChannel == 4) continue;
      uint8_t packet[53]{};
      packet[0] = 0x53; packet[1] = 2; packet[5] = 47;
      packet[19] = configuration;
      packet[32] = 1; packet[24] = 1;
      packet[35] = 255; packet[37] = 255; // old red remains saved while off
      OutputController outputs; Adapter adapter; RgbwBleDriverManager manager(outputs);
      manager.setAdapter(0, &adapter); manager.begin();
      outputs.setRgbwChannel(RgbwZone::Output1, 3, 65);
      outputs.setRgbwPresetField(RgbwZone::Output1, 3, 65);
      outputs.setRgbwPresetField(RgbwZone::Output1, 4, whiteChannel == 3 ? 2 : 4);
      manager.update(); clockMs += 101; manager.update();
      const auto desired = configuration == 0x8A
          ? Sp630eProtocol::rgbWarmCommands(adapter.sent.channels, adapter.sent.options, adapter.sent.channels[4])
          : Sp630eProtocol::rgbwCommands(adapter.sent.channels, adapter.sent.options);
      Sp630eProtocol::WarmCommandCache cache;
      const auto batch = cache.plan(desired);
      assert(batch.packets[0].data[1] == 0x50);
      for (size_t i = 0; i < batch.count; ++i) assert(batch.packets[i].data[1] != 0x52);
      applyPackets(packet, batch); cache.committed(desired);
      assert(packet[32] == 2 && packet[24] == 0 && packet[37] == 255);
      for (unsigned poll = 0; poll < 5; ++poll) {
        Sp630eProtocol::Status decoded;
        assert(Sp630eProtocol::decode(packet, sizeof packet, decoded));
        assert(decoded.channels[0] == 0 && decoded.channels[1] == 0 && decoded.channels[2] == 0);
        assert(decoded.channels[whiteChannel] == 65 && decoded.options == 2);
        RgbwBleDriverState feedback{};
        memcpy(feedback.channels, decoded.channels, 5);
        memcpy(feedback.color, decoded.color, 3);
        feedback.brightness = decoded.brightness; feedback.options = decoded.options;
        adapter.report(feedback); clockMs += 2000; manager.update();
        assert(outputs.value.rgbw[0][0] == 0 && outputs.value.rgbw[0][3] == 65);
        assert((outputs.value.rgbwOptions[0] & 1) == 0);
        assert(adapter.sends == 1); // no feedback-driven colour command
      }
      // Wake reapplies white setup after power, without touching saved RGB.
      const uint8_t off[4]{};
      const auto offCommands = Sp630eProtocol::rgbWarmCommands(off, 0);
      applyPackets(packet, cache.plan(offCommands)); cache.committed(offCommands);
      const auto onAgain = cache.plan(desired);
      assert(onAgain.count == desired.count && onAgain.packets[0].data[1] == 0x50);
    }
  }
}

void sharedRelayWakeDoesNotRestoreGreen() {
  using namespace Sp630eProtocol;
  uint8_t status[53]{};
  status[0] = 0x53; status[1] = 2; status[5] = 47;
  status[19] = 0x8A; status[32] = 1;
  WarmCommandCache cache;
  const uint8_t green[] = {0, 60, 0, 0};
  const auto on = rgbWarmCommands(green, 1);
  applyPackets(status, cache.plan(on)); cache.committed(on);
  const uint8_t zero[4]{};
  const auto off = rgbWarmCommands(zero, 0);
  applyPackets(status, cache.plan(off)); cache.committed(off);
  const uint8_t relay[] = {100, 0, 0, 0};
  const auto next = rgbWarmCommands(relay, 1);
  const auto wake = cache.plan(next);
  // Inspect every intermediate state, including the power-on before RGB write.
  for (size_t i = 0; i < wake.count; ++i) {
    WarmCommands one{}; one.count = 1; one.packets[0] = wake.packets[i];
    applyPackets(status, one);
    Status decoded{}; assert(decode(status, sizeof status, decoded));
    assert(decoded.channels[1] == 0);
  }
  Status decoded{}; assert(decode(status, sizeof status, decoded));
  assert(decoded.channels[0] == 100);
}

void relayDoesNotRescaleDimmedGreen() {
  using namespace Sp630eProtocol;
  for (bool fiveChannel : {false, true}) {
    for (uint8_t zone = 0; zone < 4; ++zone) {
      OutputController outputs; Adapter adapter; RgbwBleDriverManager manager(outputs);
      manager.setAdapter(zone, &adapter, 1);
      manager.setAccessoryAdapter(0, &adapter, 0);
      manager.begin();
      for (uint8_t intensity : {1, 25, 34, 75, 100}) {
        WarmCommandCache cache;
        for (bool relay : {false, true, false, true, false}) {
          outputs.value.rgbw[zone][3] = intensity;
          outputs.value.usbEnabled = relay;
          manager.update(); clockMs += 101; manager.update();
          assert(adapter.sent.independentChannels);
          const auto& state = adapter.sent;
          const auto commands = fiveChannel
              ? rgbWarmCommands(state.channels, state.options, state.channels[4], state.independentChannels)
              : rgbwCommands(state.channels, state.options, state.independentChannels);
          bool foundRgb = false;
          for (size_t i = 0; i < commands.count; ++i) {
            const auto& packet = commands.packets[i];
            if (packet.data[1] != 0x52) continue;
            foundRgb = true;
            assert(packet.data[6] == (relay ? 255 : 0));
            assert(packet.data[7] == intensity * 255U / 100U);
            assert(packet.data[8] == 0);
            // Neither the green component nor its multiplier changes when R
            // toggles, even if hardware applies RGB and master separately.
            assert(packet.data[9] == 255);
          }
          assert(foundRgb);
          cache.committed(commands);
        }
      }
    }
  }
}

void independentRelayDoesNotWakeEachTime() {
  using namespace Sp630eProtocol;
  for (bool fiveChannel : {false, true}) {
    WarmCommandCache cache;
    uint8_t status[53]{};
    status[0] = 0x53; status[1] = 2; status[5] = 47;
    status[19] = fiveChannel ? 0x8A : 0x88; status[32] = 1;
    const uint8_t zero[4]{};
    auto off = independentCommands(zero, 0, fiveChannel);
    applyPackets(status, cache.plan(off)); cache.committed(off);
    for (bool enabled : {true, false, true, false}) {
      const uint8_t levels[] = {static_cast<uint8_t>(enabled ? 100 : 0), 0, 0, 0};
      const auto next = independentCommands(levels, 0, fiveChannel);
      const auto changes = cache.plan(next);
      assert(changes.count == 1 && changes.packets[0].data[1] == 0x52);
      applyPackets(status, changes); cache.committed(next);
      Status decoded{}; assert(decode(status, sizeof status, decoded));
      assert(decoded.power); // Awake with zero outputs when all assignments are off.
      assert(decoded.channels[0] == (enabled ? 100 : 0));
      for (uint8_t i = 1; i < 5; ++i) assert(decoded.channels[i] == 0);
      cache.observe(status);
      assert(cache.plan(next).count == 0);
    }
  }
}

int main() { independentRelayDoesNotWakeEachTime(); relayDoesNotRescaleDimmedGreen(); sharedRelayWakeDoesNotRestoreGreen(); wakeAppliesModeAfterPower(); offToColourStaysColour(); independentLedSelections(); offToWhiteStaysWhite(); configurableLightTypes(); coolWhiteFeedback(); whiteOnlyRetainsColour(); fullColourWheel(); separateWhiteAssignments(); singleChannelRouting(); warmWhiteIntensitySweep(); changedPacketsOnly(); continuousGesturesAreNotStarved(); protocol(); rgbWarm(); rapidChangesKeepLinkAlive(); manager(); sharedAssignments(); puts("SP630E tests passed"); }
