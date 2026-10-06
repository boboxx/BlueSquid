#include "PendingControl.h"
#include "ConfigurationJson.h"
#include "RvcFanProtocol.h"
#include "Sp630eConfiguration.h"
#include "ColourWheel.h"
#include "TouchHotspot.h"
#include "TouchClock.h"
#include "DisplaySchedule.h"
#include "FirmwareUpdate.h"
#include "Sp630eChannels.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lvgl.h>
#include <FS.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

#include <cmath>

#include "AppConfig.h"
#include "LightbulbFont.h"
#include "SystemTypes.h"

#include "TouchBleClient.h"
#include "lvgl_port.h"

#if !ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED
#error "The touchscreen target requires a supported ESP Panel board configuration"
#endif

#if !defined(BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7)
#error "This touchscreen firmware is configured only for Waveshare ESP32-S3-Touch-LCD-7"
#endif

using namespace esp_panel::board;
using namespace esp_panel::drivers;

namespace {
TouchBleClient transportClient;
constexpr char kTransportName[] = "BLE";
Board* panel = nullptr;
lv_obj_t* tabView = nullptr;
bool sdMounted = false;
const char* sdMountError = "SD card is not initialized.";
constexpr uint32_t kColorBackground = 0x0D1114;
constexpr uint32_t kColorSurface = 0x171C20;
constexpr uint32_t kColorCard = 0x20262B;
constexpr uint32_t kColorCardChecked = 0x26343A;
constexpr uint32_t kColorControlCard = 0x343733;
constexpr uint32_t kColorControlActive = 0xF2F1EE;
constexpr uint32_t kColorControlText = 0x121411;
constexpr uint32_t kColorControlMuted = 0x747773;
constexpr uint32_t kColorIconCircle = 0x292D2B;
constexpr uint32_t kColorBorder = 0x303940;
constexpr uint32_t kColorText = 0xF4F7F8;
constexpr uint32_t kColorMuted = 0x9AA7AE;
constexpr uint32_t kColorCyan = 0x35D4E8;
constexpr uint32_t kColorGreen = 0x4DDD91;
constexpr uint32_t kColorAmber = 0xFFBE55;
constexpr uint32_t kColorLightbulb = 0xFFD600;
constexpr uint32_t kColorRed = 0xFF6B70;
constexpr uint32_t kCommandSettleTimeoutMs = 8000;
constexpr int kSdMosiPin = 11;
constexpr int kSdClockPin = 12;
constexpr int kSdMisoPin = 13;
constexpr int kSdChipSelectExpanderPin = 4;
constexpr char kExportDirectory[] = "/bluesquid";
constexpr char kExportPath[] = "/bluesquid/config-latest.json";
constexpr char kExportTempPath[] = "/bluesquid/config.tmp";
constexpr char kExportBackupPath[] = "/bluesquid/config.bak";
constexpr uint8_t kConfigurationSchemaVersion = ConfigurationJson::version;
constexpr int kMainBrightnessTrackWidth = 304;
constexpr uint16_t kDefaultSleepTimeoutMinutes = 5;
constexpr uint16_t kSleepTimeoutChoices[] = {0, 1, 5, 15, 30, 60};
constexpr char kSleepTimeoutOptions[] =
    "Never\n1 minute\n5 minutes\n15 minutes\n30 minutes\n60 minutes";
constexpr size_t kDeviceLabelLength = 25;

enum DeviceLabelId : uint8_t {
  kLabelAllRgbwLights,
  kLabelRgbwLight1,
  kLabelRgbwLight2,
  kLabelRgbwLight3,
  kLabelRgbwLight4,
  kLabelAccessory1,
  kLabelAccessory2,
  kLabelAccessory3,
  kLabelAccessory4,
  kDeviceLabelCount,
};

constexpr const char* kDeviceJsonKeys[] = {"rgbw_all", "rgbw_1", "rgbw_2", "rgbw_3", "rgbw_4",
    "accessory_1", "accessory_2", "accessory_3", "accessory_4"};

constexpr uint32_t kIconColours[] = {0, kColorCyan, kColorGreen, kColorLightbulb,
    kColorAmber, kColorRed, 0x719BFF, 0xC58AFF, kColorText,
    // Reference palette; retain earlier IDs for saved choices and exports.
    0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x00C7BE, 0x32ADE6,
    0x30B0C7, 0x007AFF, 0x5856D6, 0xAF52DE, 0xFF2D55, 0xA2845E};
constexpr uint8_t kIconColourCount = sizeof(kIconColours) / sizeof(kIconColours[0]);
uint8_t deviceIconColours[kDeviceLabelCount]{};
lv_obj_t* iconPickerPreview = nullptr;
lv_obj_t* iconColourSwatches[kIconColourCount]{};
uint8_t editedDeviceColour = 0;
uint32_t deviceIconColour(uint8_t label, uint32_t fallback) {
  const uint8_t choice = deviceIconColours[label];
  return choice && choice < kIconColourCount ? kIconColours[choice] : fallback;
}

struct DeviceLabelSetting {
  const char* nvsKey;
  const char* iconNvsKey;
  const char* channelName;
  char value[kDeviceLabelLength];
  uint8_t defaultIcon;
  uint8_t icon;
};

struct DeviceIconOption {
  const char* key;
  const char* name;
  const char* symbol;
};

constexpr uint8_t kIconLight = 0;
constexpr uint8_t kIconPlug = 1;
constexpr uint8_t kIconUsb = 2;
constexpr uint8_t kIconWater = 3;
constexpr DeviceIconOption kDeviceIcons[] = {
    {"light", "Light", BLUESQUID_SYMBOL_LIGHTBULB},
    {"plug", "Plug", BLUESQUID_SYMBOL_POWER_CORD},
    {"usb", "USB", LV_SYMBOL_USB},
    {"water", "Water", LV_SYMBOL_TINT},
    {"power", "Power", LV_SYMBOL_POWER},
    {"fan", "Fan", BLUESQUID_SYMBOL_FAN},
    {"home", "Home", LV_SYMBOL_HOME},
    {"charge", "Charging", LV_SYMBOL_CHARGE},
    {"bell", "Alert", LV_SYMBOL_BELL},
    {"settings", "Settings", LV_SYMBOL_SETTINGS},
    {"spigot", "Spigot", BLUESQUID_SYMBOL_SPIGOT},
    {"flames", "Flames", BLUESQUID_SYMBOL_FLAMES},
};
constexpr uint8_t kDeviceIconCount =
    sizeof(kDeviceIcons) / sizeof(kDeviceIcons[0]);
lv_obj_t* iconPickerButtons[kDeviceIconCount]{};
lv_obj_t* iconPickerSymbols[kDeviceIconCount]{};

struct ImportedConfiguration {
  RvcFan::Config rvcFan;
  uint16_t sleepMinutes = kDefaultSleepTimeoutMinutes;
  float batteryCapacityAh = 0.0F;
  float pitchZeroDegrees = 0.0F;
  float rollZeroDegrees = 0.0F;
  char labels[kDeviceLabelCount][kDeviceLabelLength]{};
  uint8_t icons[kDeviceLabelCount]{};
  uint8_t iconColours[kDeviceLabelCount]{};
  uint8_t rgbwOutput[4][4]{};
  uint8_t rgbPreset[4][3]{};
  uint8_t rgbwBrightness[4]{};
  uint8_t rgbwOptions[4]{};
  uint8_t fanSpeed = 0;
  bool accessory1Enabled = false;
  bool accessory4Enabled = false;
};

DeviceLabelSetting deviceLabels[kDeviceLabelCount] = {
    {"rgbw_all", "i_all", "All lights", "All lights", 0, 0},
    {"rgbw_1", "i_rgbw1", "RGB Light 1", "RGB Light 1", 0, 0},
    {"rgbw_2", "i_rgbw2", "RGB Light 2", "RGB Light 2", 0, 0},
    {"rgbw_3", "i_rgbw3", "RGB Light 3", "RGB Light 3", 0, 0},
    {"rgbw_4", "i_rgbw4", "RGB Light 4", "RGB Light 4", 0, 0},
    {"accessory_1", "i_acc1", "Accessory 1", "Accessory 1", 2, 2},
    {"accessory_2", "i_acc2", "Accessory 2", "Accessory 2", 3, 3},
    {"accessory_3", "i_acc3", "Accessory 3", "Accessory 3", 1, 1},
    {"accessory_4", "i_acc4", "Accessory 4", "Accessory 4", 1, 1},
};

using PendingState = PendingControl<bool>;
using PendingLevel = PendingControl<uint8_t>;

lv_obj_t* victronConnectionIcons[5]{};
lv_obj_t* connectionStatusOverlay = nullptr;
lv_obj_t* connectionStatusText = nullptr;
lv_obj_t* headerClimateLabels[5]{};
lv_obj_t* homeBatteryLabel = nullptr;
lv_obj_t* homeSolarLabel = nullptr;
lv_obj_t* homeDcDcLabel = nullptr;
lv_obj_t* shorePowerLabel = nullptr;
lv_obj_t* homeLoadLabel = nullptr;
lv_obj_t* fanButton = nullptr;
lv_obj_t* fanStateLabel = nullptr;
lv_obj_t* fanSpeedLabel = nullptr;
lv_obj_t* fanReverseSwitch = nullptr;
lv_obj_t* fanDirectionLabel = nullptr;
bool fanDirectionWaiting = false, fanRequestedIntake = false;
uint32_t fanDirectionMs = 0;
lv_obj_t* fanSlider = nullptr;
lv_obj_t* rvcOverlay = nullptr;
lv_obj_t* rvcEnabled = nullptr;
lv_obj_t* rvcInstance = nullptr;
lv_obj_t* rvcSource = nullptr;
lv_obj_t* rvcStatusLabel = nullptr;
lv_obj_t* rvcSaveButton = nullptr;
bool rvcConfigLoaded = false;
bool rvcSaving = false;
uint32_t rvcSaveMs = 0;
uint8_t lastFanSpeed = 50;
uint32_t fanCommandMs = 0;
bool fanCommandWaiting = false;
uint8_t fanRequested = 0;
lv_obj_t* allLightsButton = nullptr;
lv_obj_t* allLightsStateLabel = nullptr;
lv_obj_t* allLightsTitleLabel = nullptr;
lv_obj_t* allLightsIconLabel = nullptr;
lv_obj_t* batteryLabel = nullptr;
lv_obj_t* powerLabel = nullptr;
lv_obj_t* remainingLabel = nullptr;
lv_obj_t* zoneButtons[4]{};
lv_obj_t* zoneBrightnessControls[4]{};
lv_obj_t* zoneBrightnessFills[4]{};
lv_obj_t* zoneBrightnessValues[4]{};
lv_obj_t* zoneStateLabels[4]{};
lv_obj_t* zoneIconCircles[4]{};
lv_obj_t* zoneIconLabels[4]{};
lv_obj_t* zoneTitleLabels[4]{};
lv_obj_t* zoneColorCenters[4]{};
lv_obj_t* colorDialogOverlay = nullptr;
lv_obj_t* colorDialogTitle = nullptr;
lv_obj_t* colorDialogWheel = nullptr;
lv_obj_t* colorDialogMarker = nullptr;
lv_obj_t* colorDialogEnableSwitch = nullptr;
lv_obj_t* colorDialogWhiteEnableSwitch = nullptr;
lv_obj_t* colorDialogCoolWhiteSwitch = nullptr;
lv_obj_t* colorDialogChannelLabels[3]{};
// Retain the saved warm/cool channel mask; White LED only toggles its power.
uint8_t desiredWhiteTone[4]{2,2,2,2};
lv_obj_t* colorDialogAvailabilityLabel = nullptr;
lv_obj_t* favoriteButtons[4]{};
lv_obj_t* favoriteStateLabels[4]{};
lv_obj_t* favoriteTitleLabels[4]{};
lv_obj_t* favoriteIconLabels[4]{};
lv_obj_t* homeInverterButton = nullptr;
lv_obj_t* homeInverterStateLabel = nullptr;
lv_obj_t* controlButtons[4]{};
lv_obj_t* inverterButton = nullptr;
lv_obj_t* inverterStateLabel = nullptr;
lv_obj_t* chargerButton = nullptr;
lv_obj_t* chargerStateLabel = nullptr;
lv_obj_t* controlStateLabels[4]{};
lv_obj_t* controlTitleLabels[4]{};
lv_obj_t* controlIconLabels[4]{};
lv_obj_t* settingsOverlay = nullptr;
lv_obj_t* firmwareUpdateOverlay = nullptr;
lv_obj_t* bluetoothControllersOverlay = nullptr;
lv_obj_t* hotspotOverlay = nullptr;
lv_obj_t* hotspotSsid = nullptr;
lv_obj_t* hotspotPassword = nullptr;
lv_obj_t* hotspotKeyboard = nullptr;
lv_obj_t* hotspotStatus = nullptr;
lv_obj_t* cerboWifiOverlay = nullptr;
lv_obj_t* cerboWifiSsid = nullptr;
lv_obj_t* cerboWifiPassword = nullptr;
lv_obj_t* cerboVebusUnitId = nullptr;
lv_obj_t* cerboWifiKeyboard = nullptr;
lv_obj_t* cerboWifiStatus = nullptr;
lv_obj_t* sp630eDropdowns[4]{};
lv_obj_t* accessoryChannelDropdowns[4]{};
lv_obj_t* deviceConfigNameLabels[kDeviceLabelCount]{};
lv_obj_t* deviceConfigIconLabels[kDeviceLabelCount]{};
lv_obj_t* labelEditorOrderDropdown = nullptr;
lv_obj_t* labelEditorOrderLabel = nullptr;
lv_obj_t* sp630eStatusLabel = nullptr;
String discoveredSp630eAddresses[8];
uint8_t discoveredSp630eCount = 0;
String assignedSp630eAddresses[8];
uint8_t assignedSp630eChannels[8]{255, 255, 255, 255, 0, 0, 0, 0};
bool deviceVisible[kDeviceLabelCount - 1]{true, true, true, true,
                                         true, true, true, true};
uint8_t deviceOrder[kDeviceLabelCount - 1]{0, 1, 2, 3, 4, 5, 6, 7};
uint32_t displayedSp630eRevision = 0;
lv_obj_t* camperPositionOverlay = nullptr;
lv_obj_t* displaySettingsOverlay = nullptr;
lv_obj_t* settingsPitchLabel = nullptr;
lv_obj_t* settingsRollLabel = nullptr;
lv_obj_t* calibrationStatusLabel = nullptr;
lv_obj_t* settingsExportStatusLabel = nullptr;
lv_obj_t* sleepTimeoutDropdown = nullptr;
lv_obj_t* displayDimmer = nullptr;
lv_obj_t* displayBrightnessValue = nullptr;
lv_obj_t* overnightBrightnessValue = nullptr;
lv_obj_t* clockStatusLabel = nullptr;
lv_obj_t* clockHourDropdown = nullptr;
lv_obj_t* clockMinuteDropdown = nullptr;
uint8_t displayBrightness = 100, overnightBrightness = 20;
bool overnightEnabled = false;
uint16_t overnightOff = 22 * 60, overnightOn = 7 * 60;
constexpr uint16_t kNightWakeSeconds[] = {15, 30, 60, 120, 300};
uint8_t overnightWakeChoice = 1;
DisplaySchedule::State displaySchedule;

lv_obj_t* labelConfigOverlay = nullptr;
lv_obj_t* labelEditorOverlay = nullptr;
lv_obj_t* labelEditorTitle = nullptr;
lv_obj_t* labelEditorTextArea = nullptr;
lv_obj_t* labelEditorKeyboard = nullptr;
lv_obj_t* labelConfigValueLabels[kDeviceLabelCount]{};
lv_obj_t* labelConfigIconLabels[kDeviceLabelCount]{};
lv_obj_t* labelEditorIconLabel = nullptr;
lv_obj_t* iconPickerOverlay = nullptr;
lv_obj_t* systemInfoOverlay = nullptr;
lv_obj_t* systemConnectionLabel = nullptr;
lv_obj_t* systemRearFirmwareLabel = nullptr;
lv_obj_t* systemUptimeLabel = nullptr;
uint32_t lastUiUpdateMs = 0;
uint32_t calibrationRequestedMs = 0;
bool touchscreenReady = false;
bool displaySleeping = false;
bool uiPreferencesReady = false;
uint16_t sleepTimeoutMinutes = kDefaultSleepTimeoutMinutes;
uint32_t lastUserActivityMs = 0;
uint8_t editedDeviceLabel = 0;
uint8_t editedDeviceIcon = 0;
Preferences uiPreferences;
uint8_t activeColorZone = 0;
uint8_t selectedRgb[4][3]{{100, 0, 0}, {100, 0, 0}, {100, 0, 0}, {100, 0, 0}};
uint8_t desiredBrightness[4]{100, 100, 100, 100};
uint8_t lastZoneBrightness[4]{100, 100, 100, 100};
bool desiredColorEnabled[4]{false, false};
bool desiredWhiteEnabled[4]{true, true, true, true};
bool zoneOutputEnabled[4]{false, false};
PendingState pendingInverter{}, pendingCharger{};
PendingState pendingOutputs[4]{};
PendingState pendingZones[4]{};
PendingState pendingAllLights{};
PendingState pendingColorEnabled[4]{};
PendingState pendingWhiteEnabled[4]{};
PendingLevel pendingWhiteTone[4]{};
PendingLevel pendingBrightness[4]{};
lv_font_t tabFont{};
lv_font_t tabIconFont{};
lv_font_t metricIconFont24{};
lv_font_t iconFont28{};
lv_font_t climateFont16{};

uint8_t labelForRgbZone(uint8_t zone) {
  return kLabelRgbwLight1 + zone;
}

String draftSp630eAddresses[8];
uint8_t draftSp630eChannels[8]{255,255,255,255,0,0,0,0};
uint8_t savedLightGroup = 15, draftLightGroup = 15;
bool sp630eDraftDirty = false, sp630eSavePending = false, sp630eConfigLoaded = false;
lv_obj_t* lightGroupChecks[4]{};
uint32_t sp630eSaveStartedMs = 0;

bool fullLightType(uint8_t target) {
  // Empty slots may retain a default or previously saved full-strip type.
  return target < 4 && !assignedSp630eAddresses[target].isEmpty() &&
         Sp630eChannels::colourType(assignedSp630eChannels[target]);
}

bool rgbOnlyLightType(uint8_t zone) {
  return fullLightType(zone) && assignedSp630eChannels[zone] == Sp630eChannels::rgbOnly;
}

lv_color_t zoneColor(uint8_t zone);

// Waveshare's 154.88 x 86.72 mm viewing area has non-square pixels at
// 800 x 480. 233 x 250 pixels is approximately circular on the actual panel.
constexpr int kColourWheelWidth = 233;
constexpr int kColourWheelHeight = 250;

lv_obj_t* createColourWheel(lv_obj_t* parent, int width, int height, bool interactive) {
  lv_obj_t* canvas = lv_canvas_create(parent);
  const size_t bufferSize = static_cast<size_t>(width) * height * 4;
  void* buffer = heap_caps_calloc(
      1, bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buffer == nullptr) {
    buffer = heap_caps_calloc(
        1, bufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (buffer == nullptr) {
    lv_obj_delete(canvas);
    lv_obj_t* fallback = lv_obj_create(parent);
    lv_obj_set_size(fallback, width, height);
    lv_obj_set_style_radius(fallback, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(fallback, lv_color_hex(kColorBorder), 0);
    lv_obj_set_style_border_width(fallback, 0, 0);
    return fallback;
  }
  lv_canvas_set_buffer(canvas, buffer, width, height, LV_COLOR_FORMAT_ARGB8888);
  const float radiusX = (width - 1) * 0.5F;
  const float radiusY = (height - 1) * 0.5F;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float dx = (x-radiusX)/radiusX, dy = (y-radiusY)/radiusY;
      if (dx*dx+dy*dy > 1) continue;
      const auto rgb = ColourWheel::gradientColour(dx, dy);
      lv_canvas_set_px(canvas, x, y, lv_color_make(rgb.r,rgb.g,rgb.b), LV_OPA_COVER);
    }
  }
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  if (interactive) {
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  }
  return canvas;
}

lv_obj_t* createHueMarker(lv_obj_t* wheel, int size) {
  lv_obj_t* marker = lv_obj_create(wheel);
  lv_obj_remove_style_all(marker);
  lv_obj_set_size(marker, size, size);
  lv_obj_remove_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(marker, size / 2, 0);
  lv_obj_set_style_bg_color(marker, lv_color_hex(kColorBackground), 0);
  lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(marker, 0, 0);
  lv_obj_set_style_pad_all(marker, 0, 0);

  lv_obj_t* center = lv_obj_create(marker);
  lv_obj_remove_style_all(center);
  const int centerSize = size >= 18 ? 14 : 5;
  lv_obj_set_size(center, centerSize, centerSize);
  lv_obj_remove_flag(center, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(center, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(center, centerSize / 2, 0);
  lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(center, 0, 0);
  lv_obj_set_style_pad_all(center, 0, 0);
  lv_obj_center(center);
  return marker;
}

void positionHueMarkerForZone(lv_obj_t* marker, lv_obj_t* wheel,
                              uint8_t zone, int) {
  if (!marker || !wheel) return;
  lv_obj_update_layout(wheel);
  float x, y;
  ColourWheel::reducedPosition(selectedRgb[zone][0], selectedRgb[zone][1],
                        selectedRgb[zone][2], x, y);
  const float radiusX = (lv_obj_get_width(wheel)-1)*0.5f;
  const float radiusY = (lv_obj_get_height(wheel)-1)*0.5f;
  lv_obj_set_pos(marker, std::lround(radiusX+x*radiusX-lv_obj_get_width(marker)*0.5f),
                         std::lround(radiusY+y*radiusY-lv_obj_get_height(marker)*0.5f));
  lv_obj_set_style_bg_color(lv_obj_get_child(marker,0), zoneColor(zone),0);
  lv_obj_move_foreground(marker);
}

void syncButton(lv_obj_t* button, lv_obj_t* stateLabel, bool enabled,
                uint32_t accent);
void holdZoneState(uint8_t zone, bool enabled);
bool sendZoneOutputs(uint8_t zone, bool enabled);
bool sendZonePreset(uint8_t zone);
void openLabelConfiguration(lv_event_t* event);
void openDeviceLabelEditor(lv_event_t* event);
void applyDeviceDisplayLayout();
void openConnectionStatus(lv_event_t* event);

void styleTabButtons(lv_obj_t* tabs) {
  static const char* const icons[] = {
      LV_SYMBOL_HOME,
      BLUESQUID_SYMBOL_LIGHTBULB,
      LV_SYMBOL_POWER,
      LV_SYMBOL_CHARGE,
      LV_SYMBOL_BARS,
  };
  const uint32_t tabCount = lv_tabview_get_tab_count(tabs);
  for (uint32_t index = 0; index < tabCount; ++index) {
    lv_obj_t* button = lv_tabview_get_tab_button(tabs, index);
    if (button == nullptr) continue;
    lv_obj_set_style_radius(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorSurface), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorCard),
                              LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorCard),
                              LV_STATE_PRESSED);
    lv_obj_set_style_text_font(button, &tabFont, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(kColorMuted), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(kColorCyan),
                                LV_STATE_CHECKED);
    lv_obj_set_style_color_filter_opa(button, LV_OPA_TRANSP,
                                      LV_STATE_PRESSED);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, 6, 0);

    lv_obj_t* textLabel = lv_obj_get_child(button, 0);
    if (textLabel != nullptr) {
      lv_obj_set_style_text_font(textLabel, &tabFont, 0);
    }
    lv_obj_t* iconLabel = lv_label_create(button);
    lv_label_set_text(iconLabel, icons[index]);
    lv_obj_set_style_text_font(iconLabel, &tabIconFont, 0);
    lv_obj_move_to_index(iconLabel, 0);
  }
}

void syncTabButtonLabels(lv_obj_t* tabs) {
  const uint32_t active = lv_tabview_get_tab_active(tabs);
  const uint32_t tabCount = lv_tabview_get_tab_count(tabs);
  for (uint32_t index = 0; index < tabCount; ++index) {
    lv_obj_t* button = lv_tabview_get_tab_button(tabs, index);
    if (button == nullptr) continue;
    const uint32_t childCount = lv_obj_get_child_count(button);
    for (uint32_t childIndex = 0; childIndex < childCount; ++childIndex) {
      lv_obj_t* label = lv_obj_get_child(button, childIndex);
      lv_obj_set_style_text_color(
          label, lv_color_hex(index == active ? kColorCyan : kColorMuted), 0);
    }
  }
}

void beginPending(PendingState& pending, bool desired) {
  pending.begin(desired, millis());
}
bool displayState(PendingState& pending, bool remoteState) {
  return pending.display(remoteState, millis(), kCommandSettleTimeoutMs);
}
void beginPending(PendingLevel& pending, uint8_t desired) {
  pending.begin(desired, millis());
}
uint8_t displayLevel(PendingLevel& pending, uint8_t remoteLevel) {
  return pending.display(remoteLevel, millis(), kCommandSettleTimeoutMs);
}

const char* chargerState(uint8_t state) {
  switch (state) {
    case 0: return "Off"; case 2: return "Fault"; case 3: return "Bulk";
    case 4: return "Absorption"; case 5: return "Float";
    case 6: return "Storage"; case 7: return "Equalize";
    case 1: return "Low power"; case 8: return "Pass-through";
    case 9: return "Inverting"; case 10: return "Power assist";
    case 11: return "Power supply"; case 244: return "Sustain";
    case 252: return "ESS"; default: return "--";
  }
}

void styleScreen() {
  lv_obj_set_style_bg_color(lv_screen_active(),
                            lv_color_hex(kColorBackground), 0);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(kColorText), 0);
}

void stylePage(lv_obj_t* page) {
  lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(page, lv_color_hex(kColorBackground), 0);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(page, 0, 0);
  lv_obj_set_style_pad_all(page, 0, 0);
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, int x, int y,
                    const lv_font_t* font, uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

uint8_t sleepTimeoutChoiceIndex(uint16_t minutes) {
  for (uint8_t index = 0;
       index < sizeof(kSleepTimeoutChoices) / sizeof(kSleepTimeoutChoices[0]);
       ++index) {
    if (kSleepTimeoutChoices[index] == minutes) return index;
  }
  return 2;  // Five minutes.
}

uint8_t deviceIconIndexForKey(const char* key, uint8_t fallback) {
  if (key != nullptr) {
    for (uint8_t index = 0; index < kDeviceIconCount; ++index) {
      if (strcmp(key, kDeviceIcons[index].key) == 0) return index;
    }
  }
  return fallback;
}

void setDeviceIcon(lv_obj_t* label, uint8_t icon) {
  if (label == nullptr) return;
  const uint8_t safeIcon = icon < kDeviceIconCount ? icon : kIconLight;
  lv_label_set_text(label, kDeviceIcons[safeIcon].symbol);
  lv_obj_set_style_text_font(label, &iconFont28, 0);
}

void loadDeviceLabels() {
  if (!uiPreferencesReady) return;
  for (uint8_t index = 0; index < kDeviceLabelCount; ++index) {
    String saved = uiPreferences.getString(
        deviceLabels[index].nvsKey, deviceLabels[index].channelName);
    if (saved.length() != 0) {
      saved.substring(0, kDeviceLabelLength - 1).toCharArray(
          deviceLabels[index].value, kDeviceLabelLength);
    }
    char colourKey[16]{}; snprintf(colourKey, sizeof(colourKey), "icon_col_%u", index);
    const uint8_t colour = uiPreferences.getUChar(colourKey, 0);
    deviceIconColours[index] = colour < kIconColourCount ? colour : 0;
    const uint8_t savedIcon = uiPreferences.getUChar(
        deviceLabels[index].iconNvsKey, deviceLabels[index].defaultIcon);
    deviceLabels[index].icon = savedIcon < kDeviceIconCount
        ? savedIcon : deviceLabels[index].defaultIcon;
  }
  for (uint8_t index = 0; index < kDeviceLabelCount - 1; ++index) {
    char visibleKey[12]{};
    char orderKey[12]{};
    snprintf(visibleKey, sizeof(visibleKey), "dev_vis_%u", index);
    snprintf(orderKey, sizeof(orderKey), "dev_ord_%u", index);
    deviceVisible[index] = uiPreferences.getBool(visibleKey, true);
    deviceOrder[index] = min(uiPreferences.getUChar(orderKey, index),
                             static_cast<uint8_t>(kDeviceLabelCount - 2));
  }
}

void applyDeviceLabels() {
  if (allLightsTitleLabel != nullptr) {
    lv_label_set_text(allLightsTitleLabel,
                      deviceLabels[kLabelAllRgbwLights].value);
  }
  setDeviceIcon(allLightsIconLabel,
                deviceLabels[kLabelAllRgbwLights].icon);
  for (uint8_t zone = 0; zone < 4; ++zone) {
    if (zoneTitleLabels[zone]) lv_label_set_text(zoneTitleLabels[zone], deviceLabels[labelForRgbZone(zone)].value);
    setDeviceIcon(zoneIconLabels[zone], deviceLabels[labelForRgbZone(zone)].icon);
  }
  for (uint8_t output = 0; output < 4; ++output) {
    const char* value = deviceLabels[kLabelAccessory1 + output].value;
    if (favoriteTitleLabels[output] != nullptr) {
      lv_label_set_text(favoriteTitleLabels[output], value);
    }
    if (controlTitleLabels[output] != nullptr) {
      lv_label_set_text(controlTitleLabels[output], value);
    }
    setDeviceIcon(favoriteIconLabels[output],
                  deviceLabels[kLabelAccessory1 + output].icon);
    setDeviceIcon(controlIconLabels[output],
                  deviceLabels[kLabelAccessory1 + output].icon);
  }
  for (uint8_t index = 0; index < kDeviceLabelCount; ++index) {
    if (deviceConfigNameLabels[index] != nullptr)
      lv_label_set_text(deviceConfigNameLabels[index], deviceLabels[index].value);
    setDeviceIcon(deviceConfigIconLabels[index], deviceLabels[index].icon);
    if (labelConfigValueLabels[index] != nullptr) {
      lv_label_set_text(labelConfigValueLabels[index],
                        deviceLabels[index].value);
    }
    setDeviceIcon(labelConfigIconLabels[index], deviceLabels[index].icon);
    for (auto* icon : {deviceConfigIconLabels[index], labelConfigIconLabels[index]})
      if (icon) lv_obj_set_style_text_color(icon, lv_color_hex(deviceIconColour(index, kColorCyan)), 0);
  }
  applyDeviceDisplayLayout();
}

bool overnightNow() {
  const auto clock = TouchClock::read();
  return DisplaySchedule::overnight(overnightEnabled, clock.valid, clock.minute,
                                    overnightOff, overnightOn);
}
void applyDisplayBrightness() {
  if (!displayDimmer) return;
  const unsigned value = overnightNow() ? overnightBrightness : displayBrightness;
  static unsigned previous = 101;
  if (value != previous) {
    lv_obj_set_style_bg_opa(displayDimmer, (100 - value) * 255 / 100, 0);
    previous = value;
  }
}
void saveDisplaySettings() {
  if (!uiPreferencesReady) return;
  uiPreferences.putUChar("disp_level", displayBrightness);
  uiPreferences.putUChar("night_level", overnightBrightness);
  uiPreferences.putBool("night_enabled", overnightEnabled);
  uiPreferences.putUShort("night_off", overnightOff);
  uiPreferences.putUShort("night_on", overnightOn);
  uiPreferences.putUChar("night_wake", overnightWakeChoice);
}


void wakeDisplay() {
  if (!displaySleeping || panel == nullptr || panel->getBacklight() == nullptr) {
    return;
  }
  applyDisplayBrightness();
  if (panel->getBacklight()->on()) {
    displaySleeping = false;
    lvgl_port_set_display_sleeping(false);
    lastUserActivityMs = millis();
    Serial.println("Display awake");
  } else {
    Serial.println("Display wake failed: backlight unavailable");
  }
}

void sleepDisplay() {
  if (displaySleeping || panel == nullptr || panel->getBacklight() == nullptr) {
    return;
  }
  // Arm touch suppression before the backlight goes dark so the wake touch
  // cannot also activate the control underneath it.
  lvgl_port_set_display_sleeping(true);
  if (panel->getBacklight()->off()) {
    displaySleeping = true;
    Serial.println("Display asleep; touch screen to wake");
  } else {
    lvgl_port_set_display_sleeping(false);
    Serial.println("Display sleep failed: backlight unavailable");
  }
}

void sleepNowClicked(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) sleepDisplay();
}

void sleepTimeoutChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
  const uint16_t selected = lv_dropdown_get_selected(dropdown);
  if (selected >= sizeof(kSleepTimeoutChoices) /
                      sizeof(kSleepTimeoutChoices[0])) {
    return;
  }
  sleepTimeoutMinutes = kSleepTimeoutChoices[selected];
  lastUserActivityMs = millis();
  if (uiPreferencesReady) {
    uiPreferences.putUShort("sleep_min", sleepTimeoutMinutes);
  }
  Serial.printf("Display sleep timeout: %u minute(s)\n", sleepTimeoutMinutes);
}

lv_obj_t* makeMoonButton(lv_obj_t* parent) {
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_pos(button, 762, 4);
  lv_obj_set_size(button, 32, 34);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_ext_click_area(button, 7);

  lv_obj_t* moon = lv_obj_create(button);
  lv_obj_remove_style_all(moon);
  lv_obj_set_pos(moon, 5, 7);
  lv_obj_set_size(moon, 19, 19);
  lv_obj_set_style_radius(moon, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(moon, lv_color_hex(kColorMuted), 0);
  lv_obj_set_style_bg_opa(moon, LV_OPA_COVER, 0);

  lv_obj_t* cutout = lv_obj_create(button);
  lv_obj_remove_style_all(cutout);
  lv_obj_set_pos(cutout, 11, 3);
  lv_obj_set_size(cutout, 18, 18);
  lv_obj_set_style_radius(cutout, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(cutout, lv_color_hex(kColorBackground), 0);
  lv_obj_set_style_bg_opa(cutout, LV_OPA_COVER, 0);
  lv_obj_remove_flag(moon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(cutout, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, sleepNowClicked, LV_EVENT_CLICKED, nullptr);
  return button;
}

void headerHomeClicked(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED && tabView != nullptr) {
    lv_tabview_set_active(tabView, 0, LV_ANIM_OFF);
  }
}

lv_obj_t* makeVictronConnectionIcon(lv_obj_t* parent, int x, int y) {
  // A single glyph keeps the header memory footprint small. The previous
  // multi-object drawing exhausted LVGL's heap on the 800x480 display.
  return makeLabel(parent, LV_SYMBOL_SHUFFLE, x, y, &iconFont28,
                   kColorMuted);
}

void setVictronConnectionIcon(lv_obj_t* icon, bool rearConnected,
                              bool cerboConnected) {
  if (icon == nullptr) return;
  const uint32_t color = !rearConnected ? kColorMuted
      : (cerboConnected ? kColorGreen : kColorAmber);
  lv_obj_set_style_text_color(icon, lv_color_hex(color), 0);
  lv_obj_set_style_opa(icon, rearConnected ? LV_OPA_COVER : LV_OPA_50, 0);
}

void addHeader(lv_obj_t* page, const char* title, int connectionIndex) {
  lv_obj_t* homeIcon =
      makeLabel(page, LV_SYMBOL_HOME, 18, 15, &lv_font_montserrat_22,
                kColorText);
  lv_obj_add_flag(homeIcon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(homeIcon, 14);
  lv_obj_add_event_cb(homeIcon, headerHomeClicked, LV_EVENT_CLICKED, nullptr);
  makeLabel(page, title, 52, 12, &lv_font_montserrat_24, kColorText);

  headerClimateLabels[connectionIndex] =
      makeLabel(page,
                "P --.-°  R --.-°  |  "
                BLUESQUID_SYMBOL_THERMOMETER " --.-°C  |  "
                BLUESQUID_SYMBOL_HUMIDITY " --%  |  --:--",
                207, 7, &climateFont16, kColorMuted);
  lv_obj_set_size(headerClimateLabels[connectionIndex], 500, 30);
  lv_obj_set_style_text_align(headerClimateLabels[connectionIndex],
                              LV_TEXT_ALIGN_RIGHT, 0);
  victronConnectionIcons[connectionIndex] =
      makeVictronConnectionIcon(page, 718, 6);
  lv_obj_add_flag(victronConnectionIcons[connectionIndex],
                  LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(victronConnectionIcons[connectionIndex], 10);
  lv_obj_add_event_cb(victronConnectionIcons[connectionIndex],
                      openConnectionStatus, LV_EVENT_CLICKED, nullptr);
  makeMoonButton(page);
}

lv_obj_t* makeCard(lv_obj_t* parent, int x, int y, int width, int height) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, height);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(kColorControlCard), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(kColorBorder), 0);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  return card;
}

void closeConnectionStatus(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED &&
      connectionStatusOverlay != nullptr)
    lv_obj_add_flag(connectionStatusOverlay, LV_OBJ_FLAG_HIDDEN);
}

void refreshConnectionStatusCard() {
  if (connectionStatusText == nullptr) return;
  const auto& status = transportClient.status();
  const bool rearConnected = transportClient.connected();
  const bool cerboConnected = rearConnected && status.energyValid;
  const bool vebusConnected = cerboConnected && status.inverterValid;
  String text = "Controller: ";
  text += rearConnected ? "#4DDD91 Connected#" : "Off Line";
  text += "\nCerbo GX: ";
  text += cerboConnected ? "#4DDD91 Connected#" : "Off Line";
  text += "\nVE.Bus: ";
  text += vebusConnected ? "#4DDD91 Connected#" : "Off Line";
  text += "\nRV-C: ";
  if (!rearConnected)
    text += "Controller offline";
  else if (!(status.fanFlags & 1))
    text += "Disabled";
  else if (status.fanError == 1)
    text += "CAN connection fault";
  else if (status.fanError == 2)
    text += "CAN address conflict";
  else if (status.fanFlags & 2)
    text += "#4DDD91 Connected#";
  else
    text += "Waiting for fan";

  String linkedControllers[8];
  uint8_t linkedControllerCount = 0;
  for (uint8_t target = 0; target < 8; ++target) {
    if (assignedSp630eAddresses[target].isEmpty()) continue;
    bool alreadyListed = false;
    for (uint8_t device = 0; device < linkedControllerCount; ++device)
      alreadyListed |= linkedControllers[device].equalsIgnoreCase(
          assignedSp630eAddresses[target]);
    if (!alreadyListed)
      linkedControllers[linkedControllerCount++] =
          assignedSp630eAddresses[target];
  }
  if (linkedControllerCount == 0) {
    text += "\nSP630E: None linked";
  } else {
    for (uint8_t device = 0; device < linkedControllerCount; ++device) {
      text += "\nSP630E ";
      text += String(device + 1);
      text += ": ";
      text += linkedControllers[device];
      text += " / on demand";
    }
  }
  lv_label_set_text(connectionStatusText, text.c_str());
}

void openConnectionStatus(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  transportClient.requestSp630eConfiguration();
  if (connectionStatusOverlay == nullptr) {
    connectionStatusOverlay = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(connectionStatusOverlay, 0, 0);
    lv_obj_set_size(connectionStatusOverlay, 800, 416);
    lv_obj_remove_flag(connectionStatusOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(connectionStatusOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(connectionStatusOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(connectionStatusOverlay, 0, 0);
    lv_obj_set_style_pad_all(connectionStatusOverlay, 0, 0);

    lv_obj_t* card = makeCard(connectionStatusOverlay, 300, 38, 470, 350);
    makeLabel(card, LV_SYMBOL_SHUFFLE "  Connections", 20, 16,
              &lv_font_montserrat_18, kColorText);
    connectionStatusText = makeLabel(
        card, "Loading...", 20, 52, &lv_font_montserrat_14, kColorMuted);
    lv_label_set_recolor(connectionStatusText, true);
    lv_obj_set_width(connectionStatusText, 415);
    lv_label_set_long_mode(connectionStatusText, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(connectionStatusText, 5, 0);

    lv_obj_t* close = lv_button_create(card);
    lv_obj_set_pos(close, 414, 10);
    lv_obj_set_size(close, 40, 38);
    lv_obj_set_style_radius(close, 12, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(kColorSurface), 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_add_event_cb(close, closeConnectionStatus,
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_t* closeLabel = lv_label_create(close);
    lv_label_set_text(closeLabel, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(closeLabel, lv_color_hex(kColorText), 0);
    lv_obj_center(closeLabel);
  }
  refreshConnectionStatusCard();
  lv_obj_remove_flag(connectionStatusOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(connectionStatusOverlay);
}

lv_obj_t* makeIconCircle(lv_obj_t* parent, int x, int y, int size,
                         const char* icon, const lv_font_t* font,
                         uint32_t iconColor) {
  lv_obj_t* circle = lv_obj_create(parent);
  lv_obj_set_pos(circle, x, y);
  lv_obj_set_size(circle, size, size);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(circle, lv_color_hex(kColorIconCircle), 0);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(circle, 0, 0);
  lv_obj_set_style_pad_all(circle, 0, 0);
  lv_obj_t* iconLabel = lv_label_create(circle);
  lv_label_set_text(iconLabel, icon);
  lv_obj_set_style_text_font(iconLabel, font, 0);
  lv_obj_set_style_text_color(iconLabel, lv_color_hex(iconColor), 0);
  lv_obj_center(iconLabel);
  return circle;
}

void styleControlCard(lv_obj_t* button) {
  const lv_style_selector_t checkedPressed =
      LV_STATE_CHECKED | LV_STATE_PRESSED;
  lv_obj_set_style_radius(button, 18, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorControlCard), 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorControlCard),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorControlActive),
                            LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorControlActive),
                            checkedPressed);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, checkedPressed);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(kColorBorder), 0);
  lv_obj_set_style_border_color(button, lv_color_hex(kColorBorder),
                                LV_STATE_PRESSED);
  lv_obj_set_style_border_color(button, lv_color_hex(kColorControlActive),
                                LV_STATE_CHECKED);
  lv_obj_set_style_border_color(button, lv_color_hex(kColorControlActive),
                                checkedPressed);
  lv_obj_set_style_color_filter_opa(button, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_color_filter_opa(button, LV_OPA_TRANSP, checkedPressed);
  lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(button, 0, checkedPressed);
  lv_obj_set_style_transform_height(button, 0, checkedPressed);
  lv_obj_set_style_shadow_width(button, 0, 0);
}

lv_obj_t* addMetricCard(lv_obj_t* parent, int x, const char* icon,
                        const char* title, uint32_t accent) {
  lv_obj_t* card = makeCard(parent, x, 82, 180, 102);
  makeIconCircle(card, 11, 9, 42, icon, &metricIconFont24, accent);
  makeLabel(card, title, 62, 16, &lv_font_montserrat_14, kColorMuted);
  lv_obj_t* value = makeLabel(card, "--", 15, 49, &lv_font_montserrat_18,
                              kColorText);
  return value;
}

void addHomePowerSummary(lv_obj_t* parent) {
  lv_obj_t* card = makeCard(parent, 16, 82, 768, 102);
  const char* icons[] = {LV_SYMBOL_BATTERY_3, BLUESQUID_SYMBOL_SUN,
                         LV_SYMBOL_REFRESH, LV_SYMBOL_CHARGE, LV_SYMBOL_CHARGE};
  const char* titles[] = {"Battery", "Solar", "DC/DC", "AC charger", "Loads"};
  const uint32_t accents[] = {kColorGreen, kColorCyan, kColorAmber,
                              kColorCyan, kColorRed};
  lv_obj_t** values[] = {&homeBatteryLabel, &homeSolarLabel,
                         &homeDcDcLabel, &shorePowerLabel, &homeLoadLabel};
  for (uint8_t index = 0; index < 5; ++index) {
    const int x = index * 153;
    makeIconCircle(card, x + 11, 9, 42, icons[index],
                   &metricIconFont24, accents[index]);
    makeLabel(card, titles[index], x + 62, 16,
              &lv_font_montserrat_14, kColorMuted);
    *values[index] = makeLabel(card, "--", x + 15, 49,
                               &lv_font_montserrat_18, kColorText);
    if (index < 4) {
      lv_obj_t* separator = lv_obj_create(card);
      lv_obj_set_pos(separator, x + 152, 14);
      lv_obj_set_size(separator, 1, 74);
      lv_obj_set_style_bg_color(separator, lv_color_hex(kColorBorder), 0);
      lv_obj_set_style_border_width(separator, 0, 0);
    }
  }
}

lv_obj_t* addDetailCard(lv_obj_t* parent, int x, int y, int width, int height,
                        const char* icon, const char* title, uint32_t accent) {
  lv_obj_t* card = makeCard(parent, x, y, width, height);
  makeIconCircle(card, 14, 10, 46, icon, &lv_font_montserrat_24, accent);
  makeLabel(card, title, 72, 16, &lv_font_montserrat_16, kColorMuted);
  return makeLabel(card, "--", 18, 57, &lv_font_montserrat_20, kColorText);
}

lv_color_t zoneColor(uint8_t zone) {
  return lv_color_make(
      static_cast<uint8_t>(selectedRgb[zone][0] * 255 / 100),
      static_cast<uint8_t>(selectedRgb[zone][1] * 255 / 100),
      static_cast<uint8_t>(selectedRgb[zone][2] * 255 / 100));
}

void syncMainLightButtons(bool enabled) {
  if (allLightsButton != nullptr) {
    syncButton(allLightsButton, allLightsStateLabel, enabled,
               kColorLightbulb);
  }
}

uint32_t lastColourGestureMs[4]{};
uint8_t rememberedZoneOptions[4]{3,3,3,3};

void setZoneBrightnessDisplay(uint8_t zone, uint8_t value) {
  lv_label_set_text_fmt(zoneBrightnessValues[zone], "%u%%", value);
  if (value == 0) lv_obj_add_flag(zoneBrightnessFills[zone], LV_OBJ_FLAG_HIDDEN);
  else {
    lv_obj_remove_flag(zoneBrightnessFills[zone], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(zoneBrightnessFills[zone],
        (value * kMainBrightnessTrackWidth + 99) / 100);
  }
}

void zoneBrightnessChanged(lv_event_t* event) {
  const auto code = lv_event_get_code(event);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
      code != LV_EVENT_RELEASED) return;
  const uint8_t zone = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  auto* input = lv_indev_active();
  if (!input) return;
  lv_point_t point{};
  lv_indev_get_point(input, &point);
  lv_area_t area{};
  lv_obj_get_coords(zoneBrightnessControls[zone], &area);
  const int width = area.x2 - area.x1 + 1;
  desiredBrightness[zone] = constrain(
      ((point.x - area.x1) * 100 + width / 2) / width, 0, 100);
  if (desiredBrightness[zone]) lastZoneBrightness[zone] = desiredBrightness[zone];
  setZoneBrightnessDisplay(zone, desiredBrightness[zone]);
  if (code != LV_EVENT_RELEASED) return;
  if (desiredBrightness[zone] && !desiredColorEnabled[zone] &&
      !desiredWhiteEnabled[zone]) {
    desiredColorEnabled[zone] = (rememberedZoneOptions[zone] & 1) != 0;
    desiredWhiteEnabled[zone] = (rememberedZoneOptions[zone] & 6) != 0;
    if (rememberedZoneOptions[zone] & 6) desiredWhiteTone[zone] = rememberedZoneOptions[zone] & 6;
  }
  const bool enabled = desiredBrightness[zone] != 0;
  lastColourGestureMs[zone] = millis();
  if (sendZoneOutputs(zone, enabled)) {
    beginPending(pendingBrightness[zone], desiredBrightness[zone]);
    holdZoneState(zone, enabled);
  }
  sendZonePreset(zone);
}

bool sendZoneColor(uint8_t zone, bool enabled) {
  bool sent = true;
  for (uint8_t channel = 0; channel < 3; ++channel) {
    const uint8_t target = static_cast<uint8_t>((zone << 4) | channel);
    const uint8_t level = enabled
        ? static_cast<uint8_t>((selectedRgb[zone][channel] *
                                desiredBrightness[zone] + 50) / 100)
        : 0;
    sent = transportClient.send(BlueSquidControl::Command::SetRgbw, target,
                                level) && sent;
  }
  return sent;
}

bool sendZonePreset(uint8_t zone) {
  bool sent = true;
  for (uint8_t field = 0; field < 3; ++field) {
    const uint8_t target = static_cast<uint8_t>((zone << 4) | field);
    sent = transportClient.send(BlueSquidControl::Command::SetRgbwPreset,
                                target, selectedRgb[zone][field]) && sent;
  }
  const uint8_t brightnessTarget =
      static_cast<uint8_t>((zone << 4) | 3);
  sent = transportClient.send(BlueSquidControl::Command::SetRgbwPreset,
                              brightnessTarget,
                              desiredBrightness[zone]) && sent;
  const uint8_t options = (desiredColorEnabled[zone] ? 1 : 0) |
                          (desiredWhiteEnabled[zone] ? desiredWhiteTone[zone] : 0);
  const uint8_t optionsTarget = static_cast<uint8_t>((zone << 4) | 4);
  sent = transportClient.send(BlueSquidControl::Command::SetRgbwPreset,
                              optionsTarget, options) && sent;
  return sent;
}

bool applyZoneSelection(uint8_t zone) {
  const bool enabled = desiredColorEnabled[zone] || desiredWhiteEnabled[zone];
  if (enabled) {
    rememberedZoneOptions[zone] = (desiredColorEnabled[zone] ? 1 : 0) |
                                  (desiredWhiteEnabled[zone] ? desiredWhiteTone[zone] : 0);
    if (!desiredBrightness[zone]) desiredBrightness[zone] = lastZoneBrightness[zone];
  }
  if (sendZoneOutputs(zone, enabled) && sendZonePreset(zone)) {
    lastColourGestureMs[zone] = millis();
    beginPending(pendingColorEnabled[zone], desiredColorEnabled[zone]);
    beginPending(pendingWhiteEnabled[zone], desiredWhiteEnabled[zone]);
    beginPending(pendingWhiteTone[zone], desiredWhiteEnabled[zone] ? desiredWhiteTone[zone] : 0);
    beginPending(pendingBrightness[zone], enabled ? desiredBrightness[zone] : 0);
    holdZoneState(zone, enabled);
    return true;
  }
  return false;
}

// Capture both visible selections together before building the combined request.
void ledSelectionChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  desiredColorEnabled[activeColorZone] =
      lv_obj_has_state(colorDialogEnableSwitch, LV_STATE_CHECKED);
  desiredWhiteTone[activeColorZone] =
      (lv_obj_has_state(colorDialogWhiteEnableSwitch, LV_STATE_CHECKED) ? 2 : 0) |
      (lv_obj_has_state(colorDialogCoolWhiteSwitch, LV_STATE_CHECKED) ? 4 : 0);
  desiredWhiteEnabled[activeColorZone] = desiredWhiteTone[activeColorZone] != 0;
  applyZoneSelection(activeColorZone);
}

void syncZoneButton(uint8_t zone, bool enabled) {
  zoneOutputEnabled[zone] = enabled;
  if (enabled) lv_obj_add_state(zoneButtons[zone], LV_STATE_CHECKED);
  else lv_obj_remove_state(zoneButtons[zone], LV_STATE_CHECKED);
  lv_label_set_text(zoneStateLabels[zone], "");
  lv_obj_set_style_text_color(zoneBrightnessValues[zone],
      lv_color_hex(enabled ? kColorControlMuted : kColorMuted), 0);
  const uint32_t primary = enabled ? kColorControlText : kColorText;
  const uint32_t accent = deviceIconColour(labelForRgbZone(zone), kColorLightbulb);
  lv_obj_set_style_bg_color(zoneIconCircles[zone],
      lv_color_hex(enabled ? accent : kColorIconCircle), 0);
  lv_obj_set_style_text_color(zoneIconLabels[zone],
      lv_color_hex(enabled ? kColorControlText : accent), 0);
  lv_obj_set_style_text_color(zoneTitleLabels[zone], lv_color_hex(primary), 0);
  lv_obj_set_style_text_color(zoneStateLabels[zone],
      lv_color_hex(enabled ? kColorControlText : kColorMuted), 0);
}

void holdZoneState(uint8_t zone, bool enabled) {
  beginPending(pendingZones[zone], enabled);
  syncZoneButton(zone, enabled);
}

bool sendZoneOutputs(uint8_t zone, bool enabled) {
  if (rgbOnlyLightType(zone)) {
    if (desiredWhiteEnabled[zone]) desiredColorEnabled[zone] = true;
    desiredWhiteEnabled[zone] = false;
    desiredWhiteTone[zone] = 0;
  }
  if (!fullLightType(zone)) {
    desiredColorEnabled[zone] = false;
    desiredWhiteEnabled[zone] = enabled;
    desiredWhiteTone[zone] = 2;
  }
  bool sent = sendZoneColor(zone, enabled && desiredColorEnabled[zone]);
  const uint8_t whiteTarget = static_cast<uint8_t>((zone << 4) | 3);
  sent = transportClient.send(BlueSquidControl::Command::SetRgbw, whiteTarget,
                              enabled && desiredWhiteEnabled[zone]
                                  ? desiredBrightness[zone] : 0) && sent;
  return sent;
}

void zoneToggleChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  const uint8_t zone = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  const bool enabled = lv_obj_has_state(zoneButtons[zone], LV_STATE_CHECKED);
  if (enabled) {
    desiredColorEnabled[zone] = (rememberedZoneOptions[zone] & 1) != 0;
    desiredWhiteEnabled[zone] = (rememberedZoneOptions[zone] & 6) != 0;
    if (rememberedZoneOptions[zone] & 6) desiredWhiteTone[zone] = rememberedZoneOptions[zone] & 6;
  } else {
    const uint8_t options = (desiredColorEnabled[zone] ? 1 : 0) |
                            (desiredWhiteEnabled[zone] ? desiredWhiteTone[zone] : 0);
    if (options) rememberedZoneOptions[zone] = options;
    desiredColorEnabled[zone] = desiredWhiteEnabled[zone] = false;
  }
  applyZoneSelection(zone);
}

void closeColorDialog(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    lv_obj_add_flag(colorDialogOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

void colorWheelChanged(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
  lv_indev_t* inputDevice = lv_indev_active();
  if (inputDevice == nullptr) return;
  lv_point_t point{};
  lv_indev_get_point(inputDevice, &point);
  lv_area_t area{};
  lv_obj_get_coords(colorDialogWheel, &area);
  const float centerX = (area.x1 + area.x2) * 0.5F;
  const float centerY = (area.y1 + area.y2) * 0.5F;
  const float radiusX = (area.x2-area.x1)*0.5f;
  const float radiusY = (area.y2-area.y1)*0.5f;
  if (radiusX <= 0 || radiusY <= 0) return;
  const auto rgb = ColourWheel::reducedColour((point.x-centerX)/radiusX, (point.y-centerY)/radiusY);
  const lv_color_t color = lv_color_make(rgb.r,rgb.g,rgb.b);
  lastColourGestureMs[activeColorZone] = millis();
  holdZoneState(activeColorZone, zoneOutputEnabled[activeColorZone]);

  const lv_color32_t color32 = lv_color_to_32(color, LV_OPA_COVER);
  selectedRgb[activeColorZone][0] =
      static_cast<uint8_t>((color32.red * 100 + 127) / 255);
  selectedRgb[activeColorZone][1] =
      static_cast<uint8_t>((color32.green * 100 + 127) / 255);
  selectedRgb[activeColorZone][2] =
      static_cast<uint8_t>((color32.blue * 100 + 127) / 255);
  positionHueMarkerForZone(colorDialogMarker, colorDialogWheel, activeColorZone, 0);
  lv_obj_set_style_bg_color(zoneColorCenters[activeColorZone], color, 0);
  desiredColorEnabled[activeColorZone] = true;
  lv_obj_add_state(colorDialogEnableSwitch, LV_STATE_CHECKED);
  applyZoneSelection(activeColorZone);
}

void syncWhiteSelections(uint8_t zone) {
  lv_obj_t* switches[] = {colorDialogWhiteEnableSwitch, colorDialogCoolWhiteSwitch};
  for (uint8_t i = 0; i < 2; ++i) {
    for (lv_obj_t* object : {switches[i], colorDialogChannelLabels[i + 1]}) {
      if (rgbOnlyLightType(zone)) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    if (!rgbOnlyLightType(zone) && desiredWhiteEnabled[zone] && (desiredWhiteTone[zone] & (2U << i)))
      lv_obj_add_state(switches[i], LV_STATE_CHECKED);
    else lv_obj_remove_state(switches[i], LV_STATE_CHECKED);
  }
}

void openColorDialog(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  activeColorZone = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (!fullLightType(activeColorZone)) return;
  lv_label_set_text_fmt(colorDialogTitle, "%s colour",
                        deviceLabels[labelForRgbZone(activeColorZone)].value);
  positionHueMarkerForZone(colorDialogMarker, colorDialogWheel,
                           activeColorZone, 24);
  if (desiredColorEnabled[activeColorZone]) {
    lv_obj_add_state(colorDialogEnableSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(colorDialogEnableSwitch, LV_STATE_CHECKED);
  }
  syncWhiteSelections(activeColorZone);
  lv_obj_remove_flag(colorDialogOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(colorDialogOverlay);
}

void addLightZone(lv_obj_t* parent, const char* title, int x,
                  uint8_t zoneIndex) {
  lv_obj_t* card = lv_button_create(parent);
  zoneButtons[zoneIndex] = card;
  lv_obj_add_flag(card, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_pos(card, x, 222);
  lv_obj_set_size(card, 360, 128);
  styleControlCard(card);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_add_event_cb(card, zoneToggleChanged, LV_EVENT_VALUE_CHANGED,
                      reinterpret_cast<void*>(
                          static_cast<uintptr_t>(zoneIndex)));
  zoneIconCircles[zoneIndex] =
      makeIconCircle(
          card, 14, 22, 46,
          kDeviceIcons[deviceLabels[labelForRgbZone(zoneIndex)].icon].symbol,
          &iconFont28, kColorLightbulb);
  zoneIconLabels[zoneIndex] = lv_obj_get_child(zoneIconCircles[zoneIndex], 0);
  zoneTitleLabels[zoneIndex] =
      makeLabel(card, title, 72, 25, &lv_font_montserrat_16, kColorText);
  zoneStateLabels[zoneIndex] =
      makeLabel(card, "Off", 72, 49, &lv_font_montserrat_14, kColorMuted);

  lv_obj_set_width(zoneTitleLabels[zoneIndex], 220);
  lv_label_set_long_mode(zoneTitleLabels[zoneIndex], LV_LABEL_LONG_DOT);
  lv_obj_t* colorButton = lv_button_create(card);
  lv_obj_set_pos(colorButton, 316, 22);
  lv_obj_set_size(colorButton, 42, 42);
  lv_obj_set_style_radius(colorButton, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(colorButton, lv_color_hex(kColorIconCircle), 0);
  lv_obj_set_style_bg_opa(colorButton, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(colorButton, 0, 0);
  lv_obj_set_style_pad_all(colorButton, 0, 0);
  lv_obj_set_style_shadow_width(colorButton, 0, 0);
  lv_obj_set_style_transform_width(colorButton, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(colorButton, 0, LV_STATE_PRESSED);
  lv_obj_add_event_cb(colorButton, openColorDialog, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(
                          static_cast<uintptr_t>(zoneIndex)));

  zoneColorCenters[zoneIndex] = lv_obj_create(colorButton);
  lv_obj_set_size(zoneColorCenters[zoneIndex], 26, 26);
  lv_obj_center(zoneColorCenters[zoneIndex]);
  lv_obj_remove_flag(zoneColorCenters[zoneIndex], LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(zoneColorCenters[zoneIndex], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(zoneColorCenters[zoneIndex], LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(zoneColorCenters[zoneIndex],
                            zoneColor(zoneIndex), 0);
  lv_obj_set_style_bg_opa(zoneColorCenters[zoneIndex], LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(zoneColorCenters[zoneIndex], 0, 0);
  zoneBrightnessValues[zoneIndex] = makeLabel(card, "100%", 260, 49,
      &lv_font_montserrat_14, kColorMuted);
  zoneBrightnessControls[zoneIndex] = lv_obj_create(card);
  lv_obj_remove_style_all(zoneBrightnessControls[zoneIndex]);
  lv_obj_set_pos(zoneBrightnessControls[zoneIndex], 14, 96);
  lv_obj_set_size(zoneBrightnessControls[zoneIndex],
                  kMainBrightnessTrackWidth, 14);
  lv_obj_remove_flag(zoneBrightnessControls[zoneIndex], LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(zoneBrightnessControls[zoneIndex], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(zoneBrightnessControls[zoneIndex], 10);
  lv_obj_set_style_radius(zoneBrightnessControls[zoneIndex], 7, 0);
  lv_obj_set_style_bg_color(zoneBrightnessControls[zoneIndex],
                            lv_color_hex(kColorBorder), 0);
  lv_obj_set_style_bg_opa(zoneBrightnessControls[zoneIndex], LV_OPA_COVER, 0);

  zoneBrightnessFills[zoneIndex] = lv_obj_create(card);
  lv_obj_remove_style_all(zoneBrightnessFills[zoneIndex]);
  lv_obj_set_pos(zoneBrightnessFills[zoneIndex], 14, 96);
  lv_obj_set_size(zoneBrightnessFills[zoneIndex],
                  kMainBrightnessTrackWidth, 14);
  lv_obj_remove_flag(zoneBrightnessFills[zoneIndex], LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(zoneBrightnessFills[zoneIndex], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(zoneBrightnessFills[zoneIndex], 7, 0);
  lv_obj_set_style_bg_color(zoneBrightnessFills[zoneIndex],
                            lv_color_hex(kColorLightbulb), 0);
  lv_obj_set_style_bg_opa(zoneBrightnessFills[zoneIndex], LV_OPA_COVER, 0);

  lv_obj_add_event_cb(zoneBrightnessControls[zoneIndex], zoneBrightnessChanged,
                      LV_EVENT_PRESSED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(zoneIndex)));
  lv_obj_add_event_cb(zoneBrightnessControls[zoneIndex], zoneBrightnessChanged,
                      LV_EVENT_PRESSING,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(zoneIndex)));
  lv_obj_add_event_cb(zoneBrightnessControls[zoneIndex], zoneBrightnessChanged,
                      LV_EVENT_RELEASED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(zoneIndex)));
  setZoneBrightnessDisplay(zoneIndex, 100);

}

void createColorDialog() {
  colorDialogOverlay = lv_obj_create(lv_screen_active());
  lv_obj_set_pos(colorDialogOverlay, 0, 0);
  lv_obj_set_size(colorDialogOverlay, 800, 480);
  lv_obj_remove_flag(colorDialogOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(colorDialogOverlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(colorDialogOverlay, LV_OPA_70, 0);
  lv_obj_set_style_border_width(colorDialogOverlay, 0, 0);
  lv_obj_set_style_pad_all(colorDialogOverlay, 0, 0);

  lv_obj_t* panel = makeCard(colorDialogOverlay, 90, 38, 620, 404);
  colorDialogTitle =
      makeLabel(panel, "Front light colour", 24, 18,
                &lv_font_montserrat_22, kColorText);

  lv_obj_t* closeButton = lv_button_create(panel);
  lv_obj_set_pos(closeButton, 556, 12);
  lv_obj_set_size(closeButton, 46, 42);
  lv_obj_set_style_radius(closeButton, 14, 0);
  lv_obj_set_style_bg_color(closeButton, lv_color_hex(kColorSurface), 0);
  lv_obj_set_style_shadow_width(closeButton, 0, 0);
  lv_obj_add_event_cb(closeButton, closeColorDialog, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* closeLabel = lv_label_create(closeButton);
  lv_label_set_text(closeLabel, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(closeLabel, lv_color_hex(kColorText), 0);
  lv_obj_center(closeLabel);

  colorDialogWheel = createColourWheel(panel, kColourWheelWidth, kColourWheelHeight, true);
  lv_obj_set_pos(colorDialogWheel, 34 + (250-kColourWheelWidth)/2, 82);
  lv_obj_set_size(colorDialogWheel, kColourWheelWidth, kColourWheelHeight);
  colorDialogMarker = createHueMarker(colorDialogWheel, 28);
  positionHueMarkerForZone(colorDialogMarker, colorDialogWheel, 0, 24);
  lv_obj_add_event_cb(colorDialogWheel, colorWheelChanged,
                      LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(colorDialogWheel, colorWheelChanged,
                      LV_EVENT_RELEASED, nullptr);

  lv_obj_t* selections = makeCard(panel, 320, 118, 272, 220);
  makeLabel(selections, "Light channels", 16, 12, &lv_font_montserrat_16, kColorText);
  const char* names[] = {"Colour", "Warm White", "Cool White"};
  lv_obj_t** switches[] = {&colorDialogEnableSwitch, &colorDialogWhiteEnableSwitch, &colorDialogCoolWhiteSwitch};
  for (uint8_t i = 0; i < 3; ++i) {
    colorDialogChannelLabels[i] = makeLabel(selections, names[i], 16, 57 + i * 52, &lv_font_montserrat_14, kColorText);
    *switches[i] = lv_switch_create(selections);
    lv_obj_set_pos(*switches[i], 200, 50 + i * 52);
    lv_obj_set_size(*switches[i], 52, 28);
    lv_obj_set_style_bg_color(*switches[i], lv_color_hex(kColorBorder), LV_PART_MAIN);
    lv_obj_set_style_bg_color(*switches[i], lv_color_hex(kColorLightbulb), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(*switches[i], lv_color_hex(kColorText), LV_PART_KNOB);
    lv_obj_add_event_cb(*switches[i], ledSelectionChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  }

  colorDialogAvailabilityLabel =
      makeLabel(panel, "", 340, 78, &lv_font_montserrat_14, kColorAmber);

  lv_obj_add_flag(colorDialogOverlay, LV_OBJ_FLAG_HIDDEN);
}

void toggleChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* object = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (lv_obj_has_state(object, LV_STATE_DISABLED)) return;
  const auto logicalCommand = static_cast<BlueSquidControl::Command>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  const bool desired = lv_obj_has_state(object, LV_STATE_CHECKED);
  const auto command = logicalCommand;
  const bool sent = transportClient.send(command, 0, desired ? 1 : 0);

  if (logicalCommand == BlueSquidControl::Command::SetInverter ||
      logicalCommand == BlueSquidControl::Command::SetCharger) {
    Serial.printf("Power control tap: command=%u desired=%s BLE=%s\n",
                  static_cast<unsigned>(logicalCommand),
                  desired ? "on" : "off", sent ? "sent" : "failed");
  }

  if (logicalCommand == BlueSquidControl::Command::SetInverter ||
      logicalCommand == BlueSquidControl::Command::SetCharger) {
    const auto& power = transportClient.status();
    const bool inverter = logicalCommand == BlueSquidControl::Command::SetInverter;
    PendingState& pending = inverter ? pendingInverter : pendingCharger;
    if (sent) beginPending(pending, desired);
    const bool previous = power.inverterValid &&
        (power.inverterMode == (inverter ? 2 : 1) || power.inverterMode == 3);
    const bool displayed = displayState(pending, previous);
    if (inverter) {
      syncButton(inverterButton, inverterStateLabel, displayed, kColorGreen);
      syncButton(homeInverterButton, homeInverterStateLabel, displayed, kColorGreen);
    } else syncButton(chargerButton, chargerStateLabel, displayed, kColorCyan);
    return;
  }

  int outputIndex = -1;
  uint32_t accent = kColorCyan;
  switch (logicalCommand) {
    case BlueSquidControl::Command::SetUsb:
      outputIndex = 0;
      break;
    case BlueSquidControl::Command::SetPump:
      outputIndex = 1;
      break;
    case BlueSquidControl::Command::SetAccessory3:
      outputIndex = 2;
      accent = kColorAmber;
      break;
    case BlueSquidControl::Command::SetAccessory4:
      outputIndex = 3;
      accent = kColorAmber;
      break;
    case BlueSquidControl::Command::SetAllLights: {
      const bool previous = transportClient.status().anyLightsEnabled(savedLightGroup);
      if (sent) {
        beginPending(pendingAllLights, desired);
        syncMainLightButtons(desired);
        // The Controller applies the entire group atomically. Let its snapshot
        // update individual cards, clearing older per-light optimistic edits.
        for (uint8_t zone = 0; zone < 4; ++zone) {
          pendingZones[zone] = {};
          pendingColorEnabled[zone] = {};
          pendingWhiteEnabled[zone] = {};
          pendingWhiteTone[zone] = {};
          pendingBrightness[zone] = {};
        }
      } else {
        syncMainLightButtons(previous);
      }
      return;
    }
    default:
      return;
  }

  const auto& status = transportClient.status();
  const bool remoteStates[] = {status.usb, status.pump, status.accessory3,
                               status.accessory4};
  const bool displayed = sent ? desired : remoteStates[outputIndex];
  if (sent) beginPending(pendingOutputs[outputIndex], desired);
  syncButton(favoriteButtons[outputIndex], favoriteStateLabels[outputIndex],
             displayed, accent);
  syncButton(controlButtons[outputIndex], controlStateLabels[outputIndex],
             displayed, accent);
}

lv_obj_t* addToggle(lv_obj_t* parent, const char* icon, const char* name,
                    int x, int y, int width, int height,
                    BlueSquidControl::Command command, uint32_t accent,
                    lv_obj_t** stateLabel, lv_obj_t** titleLabel = nullptr,
                    lv_obj_t** iconLabel = nullptr) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  styleControlCard(button);
  lv_obj_set_style_pad_all(button, 14, 0);
  lv_obj_add_event_cb(button, toggleChanged, LV_EVENT_VALUE_CHANGED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(command)));
  const int contentY = (height - 46) / 2 - 14;
  lv_obj_t* iconCircle =
      makeIconCircle(button, 0, contentY, 46, icon, &iconFont28, accent);
  if (iconLabel != nullptr) *iconLabel = lv_obj_get_child(iconCircle, 0);
  lv_obj_t* title = makeLabel(button, name, 58, contentY + 3,
                              &lv_font_montserrat_16, kColorText);
  if (titleLabel != nullptr) *titleLabel = title;
  *stateLabel = makeLabel(button, "Off", 58, contentY + 27,
                          &lv_font_montserrat_14, kColorMuted);
  return button;
}

void styleHomeQuickButton(lv_obj_t* button, lv_obj_t* title,
                          lv_obj_t* state, int x, int y,
                          int width, int height) {
  if (button == nullptr) return;
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  // Match addToggle() on the Control page: the title/status pair and the
  // 46 px icon form one vertically centred content group.
  const int contentY = (height - 46) / 2 - 14;
  lv_obj_t* iconCircle = lv_obj_get_child(button, 0);
  if (iconCircle != nullptr) lv_obj_set_pos(iconCircle, 14, contentY);
  if (title != nullptr) {
    lv_obj_set_pos(title, 74, contentY + 3);
    lv_obj_set_width(title, width - 88);
    lv_obj_set_height(title, 22);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  }
  if (state != nullptr) {
    lv_obj_set_pos(state, 74, contentY + 27);
    lv_obj_set_width(state, width - 88);
    lv_obj_set_style_text_align(state, LV_TEXT_ALIGN_LEFT, 0);
  }
}

void applyDeviceDisplayLayout() {
  for (uint8_t zone = 0; zone < 4; ++zone) {
    if (zoneColorCenters[zone]) {
      auto* colour = lv_obj_get_parent(zoneColorCenters[zone]);
      if (fullLightType(zone)) lv_obj_remove_flag(colour, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(colour, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (colorDialogOverlay && !fullLightType(activeColorZone))
    lv_obj_add_flag(colorDialogOverlay, LV_OBJ_FLAG_HIDDEN);
  uint8_t lightRank = 0;
  for (uint8_t order = 0; order < 8; ++order) {
    for (uint8_t device = 0; device < 4; ++device) {
      if (!deviceVisible[device] || deviceOrder[device] != order) continue;
      lv_obj_t* card = zoneButtons[device];
      if (card != nullptr) {
        lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(card, lightRank % 2 == 0 ? 16 : 408,
                       lightRank < 2 ? 82 : 222);
        lv_obj_set_width(card, 376);
      }
      ++lightRank;
    }
  }
  for (uint8_t device = 0; device < 4; ++device) {
    lv_obj_t* card = zoneButtons[device];
    if (card != nullptr && !deviceVisible[device])
      lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
  }

  uint8_t accessoryCount = 0;
  for (uint8_t accessory = 0; accessory < 4; ++accessory)
    if (deviceVisible[accessory + 4]) ++accessoryCount;
  uint8_t accessoryRank = 0;
  for (uint8_t order = 0; order < 8; ++order) {
    for (uint8_t accessory = 0; accessory < 4; ++accessory) {
      const uint8_t device = accessory + 4;
      if (!deviceVisible[device] || deviceOrder[device] != order) continue;
      const int gap = 10;
      const int width = accessoryCount == 0
          ? 0 : (768 - gap * (accessoryCount - 1)) / accessoryCount;
      if (controlButtons[accessory] != nullptr) {
        lv_obj_remove_flag(controlButtons[accessory], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(controlButtons[accessory], 16 + accessoryRank * (width + gap),
                       147);
        lv_obj_set_size(controlButtons[accessory], width, 120);
        for (uint8_t child = 0; child < 3; ++child)
          lv_obj_set_y(lv_obj_get_child(controlButtons[accessory], child), child == 0 ? 23 : child == 1 ? 26 : 50);
      }
      ++accessoryRank;
    }
  }
  for (uint8_t accessory = 0; accessory < 4; ++accessory) {
    if (controlButtons[accessory] != nullptr &&
        !deviceVisible[accessory + 4])
      lv_obj_add_flag(controlButtons[accessory], LV_OBJ_FLAG_HIDDEN);
  }

  const uint8_t homeCount = 2 + accessoryCount;
  const uint8_t homeColumns = min(static_cast<uint8_t>(3), homeCount);
  const uint8_t homeRows = (homeCount + homeColumns - 1) / homeColumns;
  const int homeGap = 10;
  const int homeWidth =
      (768 - homeGap * (homeColumns - 1)) / homeColumns;
  const int homeHeight = homeRows == 1 ? 220 : 140;
  const int homeTop = homeRows == 1 ? 100 : 82;
  uint8_t homeRank = 0;
  const auto homeX = [homeColumns, homeWidth, homeGap](uint8_t rank) {
    return 16 + (rank % homeColumns) * (homeWidth + homeGap);
  };
  const auto homeY = [homeColumns, homeHeight, homeGap, homeTop](uint8_t rank) {
    return homeTop + (rank / homeColumns) * (homeHeight + homeGap);
  };
  styleHomeQuickButton(allLightsButton, allLightsTitleLabel,
                       allLightsStateLabel, homeX(homeRank), homeY(homeRank),
                       homeWidth, homeHeight);
  ++homeRank;
  lv_obj_t* inverterTitle = homeInverterButton == nullptr
      ? nullptr : lv_obj_get_child(homeInverterButton, 1);
  styleHomeQuickButton(homeInverterButton, inverterTitle,
                       homeInverterStateLabel,
                       homeX(homeRank), homeY(homeRank),
                       homeWidth, homeHeight);
  ++homeRank;
  for (uint8_t order = 0; order < 8; ++order) {
    for (uint8_t accessory = 0; accessory < 4; ++accessory) {
      const uint8_t device = accessory + 4;
      if (!deviceVisible[device] || deviceOrder[device] != order) continue;
      lv_obj_remove_flag(favoriteButtons[accessory], LV_OBJ_FLAG_HIDDEN);
      styleHomeQuickButton(favoriteButtons[accessory],
                           favoriteTitleLabels[accessory],
                           favoriteStateLabels[accessory],
                           homeX(homeRank), homeY(homeRank),
                           homeWidth, homeHeight);
      ++homeRank;
    }
  }
  for (uint8_t accessory = 0; accessory < 4; ++accessory) {
    if (favoriteButtons[accessory] != nullptr &&
        !deviceVisible[accessory + 4])
      lv_obj_add_flag(favoriteButtons[accessory], LV_OBJ_FLAG_HIDDEN);
  }
}

void addMenuRow(lv_obj_t* panel, int y, const char* icon, const char* title,
                const char* detail, uint32_t accent, bool separator) {
  makeLabel(panel, icon, 22, y + 16, &lv_font_montserrat_24, accent);
  makeLabel(panel, title, 72, y + 13, &lv_font_montserrat_18, kColorText);
  lv_obj_t* detailLabel = makeLabel(panel, detail, 72, y + 39,
                                    &lv_font_montserrat_12, kColorMuted);
  lv_obj_align(detailLabel, LV_ALIGN_TOP_LEFT, 72, y + 39);
  if (separator) {
    lv_obj_t* line = lv_obj_create(panel);
    lv_obj_set_pos(line, 72, y + 67);
    lv_obj_set_size(line, 652, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(kColorBorder), 0);
    lv_obj_set_style_border_width(line, 0, 0);
  }
}

void closeOverlay(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_t* overlay = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
  if (overlay != nullptr) lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

void tabNavigationChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  lv_obj_t* tabs = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (tabs != nullptr) syncTabButtonLabels(tabs);
  if (firmwareUpdateOverlay != nullptr)
    lv_obj_add_flag(firmwareUpdateOverlay, LV_OBJ_FLAG_HIDDEN);
  if (settingsOverlay != nullptr) {
    lv_obj_add_flag(settingsOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (bluetoothControllersOverlay != nullptr) {
    lv_obj_add_flag(bluetoothControllersOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (cerboWifiOverlay != nullptr)
    lv_obj_add_flag(cerboWifiOverlay, LV_OBJ_FLAG_HIDDEN);
  if (camperPositionOverlay != nullptr) {
    lv_obj_add_flag(camperPositionOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (displaySettingsOverlay != nullptr) {
    lv_obj_add_flag(displaySettingsOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (systemInfoOverlay != nullptr) {
    lv_obj_add_flag(systemInfoOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (labelConfigOverlay != nullptr) {
    lv_obj_add_flag(labelConfigOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (labelEditorOverlay != nullptr) {
    lv_obj_add_flag(labelEditorOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (iconPickerOverlay != nullptr) {
    lv_obj_add_flag(iconPickerOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

lv_obj_t* createPageOverlay(const char* title) {
  lv_obj_t* overlay = lv_obj_create(lv_screen_active());
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_size(overlay, 800, 416);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(kColorBackground), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);

  lv_obj_t* back = makeLabel(overlay, LV_SYMBOL_LEFT, 18, 15,
                             &lv_font_montserrat_22, kColorText);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(back, 14);
  lv_obj_add_event_cb(back, closeOverlay, LV_EVENT_CLICKED, overlay);
  makeLabel(overlay, title, 52, 12, &lv_font_montserrat_24, kColorText);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  return overlay;
}

void openSettings(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_remove_flag(settingsOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(settingsOverlay);
}

bool sp630eSeenInScan[8]{};

String sp630eAssignmentLabel(uint8_t device, uint8_t target, uint8_t channel,
                              const String& type) {
  const bool saved = sp630eConfigLoaded &&
      assignedSp630eChannels[target] == channel &&
      assignedSp630eAddresses[target].equalsIgnoreCase(discoveredSp630eAddresses[device]);
  return discoveredSp630eAddresses[device] + " / " + type +
      (saved ? " [saved]" : sp630eSeenInScan[device] ? " [seen]" : "");
}

void refreshSp630eDropdowns() {
  const auto& channels = Sp630eChannels::labels;
  for (uint8_t target = 0; target < 4; ++target) {
    if (sp630eDropdowns[target] == nullptr) continue;
    String options = "Not assigned";
    for (uint8_t device = 0; device < discoveredSp630eCount; ++device) {
      options += "\n" + sp630eAssignmentLabel(device, target, Sp630eChannels::fullStrip, "Full RGBCWWW");
      options += "\n" + sp630eAssignmentLabel(device, target, Sp630eChannels::rgbOnly, "Full RGB");
      for (uint8_t channel = 0; channel < Sp630eChannels::count; ++channel)
        options += "\n" + sp630eAssignmentLabel(device, target, Sp630eChannels::ids[channel], String("Single ") + channels[channel]);
    }
    lv_dropdown_set_options(sp630eDropdowns[target], options.c_str());
  }
  for (uint8_t accessory = 0; accessory < 4; ++accessory) {
    if (accessoryChannelDropdowns[accessory] == nullptr) continue;
    String options = "Not assigned";
    for (uint8_t device = 0; device < discoveredSp630eCount; ++device) {
      for (uint8_t channel = 0; channel < Sp630eChannels::count; ++channel) {
        options += "\n" + sp630eAssignmentLabel(device, accessory + 4, Sp630eChannels::ids[channel], channels[channel]);
      }
    }
    lv_dropdown_set_options(accessoryChannelDropdowns[accessory],
                            options.c_str());
  }
}

void processSp630ePayload() {
  const uint32_t revision = transportClient.sp630ePayloadRevision();
  if (revision == displayedSp630eRevision) return;
  displayedSp630eRevision = revision;
  const String payload = transportClient.sp630ePayload();
  if (payload.startsWith("rvc.config=")) {
    unsigned enabled, instance, source;
    if (sscanf(payload.c_str(), "rvc.config=%u|%u|%u", &enabled, &instance, &source) == 3 &&
        enabled <= 1 && instance >= 1 && instance <= 250 && source >= 151 && source <= 159) {
      lv_dropdown_set_selected(rvcEnabled, enabled);
      lv_dropdown_set_selected(rvcInstance, instance - 1);
      lv_dropdown_set_selected(rvcSource, source - 151);
      rvcConfigLoaded = true; rvcSaving = false;
      lv_label_set_text(rvcStatusLabel, "Dometic FA75 • Roof vent fan");
    }
    return;
  }
  if (payload.startsWith("rvc.error=")) {
    rvcSaving = false; lv_label_set_text(rvcStatusLabel, payload.c_str() + 10); return;
  }
  if (payload == "rvc.saved") {
    rvcSaving = false; lv_label_set_text(rvcStatusLabel, "Saved. Connecting to fan..."); return;
  }
  if (payload.startsWith("wifi.config=")) {
    const int first = payload.indexOf('|', 12);
    const int second = payload.indexOf('|', first + 1);
    const int third = payload.indexOf('|', second + 1);
    const int fourth = payload.indexOf('|', third + 1);
    if (first > 0 && second > first && third > second) {
      if (cerboWifiSsid != nullptr)
        lv_textarea_set_text(cerboWifiSsid,
                             payload.substring(12, first).c_str());
      if (cerboWifiStatus != nullptr) {
        const bool active = payload.substring(first + 1, second) == "1";
        const String ipAddress = fourth < 0
                                     ? payload.substring(third + 1)
                                     : payload.substring(third + 1, fourth);
        const String message = active
            ? "Controller connected - IP " + ipAddress
            : String("Not connected to Cerbo hotspot");
        lv_label_set_text(cerboWifiStatus, message.c_str());
      }
      if (fourth > third && cerboVebusUnitId != nullptr)
        lv_textarea_set_text(cerboVebusUnitId,
                             payload.substring(fourth + 1).c_str());
    }
    return;
  }
  if (payload.startsWith("sp630e.error=")) {
    sp630eSavePending = false;
    lv_label_set_text(sp630eStatusLabel, payload.substring(13).c_str());
    return;
  }
  if (payload.startsWith("sp630e.map=")) {
    sp630eConfigLoaded = true;
    const int groupStart = payload.indexOf(";group,");
    if (groupStart >= 0) savedLightGroup = payload.substring(groupStart + 7).toInt() & 15;

    int start = 11;
    while (start < static_cast<int>(payload.length())) {
      int end = payload.indexOf(';', start);
      if (end < 0) end = payload.length();
      int comma1 = payload.indexOf(',', start);
      int comma2 = payload.indexOf(',', comma1 + 1);
      if (comma1 < 0 || comma2 < 0 || comma2 > end) break;
      const uint8_t target = payload.substring(start, comma1).toInt();
      if (target < 8) {
        assignedSp630eChannels[target] =
            payload.substring(comma1 + 1, comma2).toInt();
        assignedSp630eAddresses[target] =
            payload.substring(comma2 + 1, end);
        if (!assignedSp630eAddresses[target].isEmpty()) {
          bool known = false;
          for (uint8_t index = 0; index < discoveredSp630eCount; ++index)
            known |= discoveredSp630eAddresses[index].equalsIgnoreCase(
                assignedSp630eAddresses[target]);
          if (!known && discoveredSp630eCount < 8)
            discoveredSp630eAddresses[discoveredSp630eCount++] =
                assignedSp630eAddresses[target];
        }
      }
      start = end + 1;
    }
    if (sp630eSavePending) {
      bool matched = savedLightGroup == draftLightGroup;
      for (uint8_t i = 0; i < 8; ++i)
        matched &= assignedSp630eChannels[i] == draftSp630eChannels[i] &&
                   assignedSp630eAddresses[i].equalsIgnoreCase(draftSp630eAddresses[i]);
      if (matched) {
        sp630eDraftDirty = false; sp630eSavePending = false;
        lv_label_set_text(sp630eStatusLabel, "Configuration saved");
      }
    }
    if (!sp630eDraftDirty) {
      draftLightGroup = savedLightGroup;
      for (uint8_t i = 0; i < 8; ++i) {
        draftSp630eAddresses[i] = assignedSp630eAddresses[i];
        draftSp630eChannels[i] = assignedSp630eChannels[i];
      }
    }
    for (uint8_t i = 0; i < 4; ++i) if (lightGroupChecks[i]) {
      if (draftLightGroup & (1U << i)) lv_obj_add_state(lightGroupChecks[i], LV_STATE_CHECKED);
      else lv_obj_remove_state(lightGroupChecks[i], LV_STATE_CHECKED);
    }
    refreshSp630eDropdowns();
    for (uint8_t target = 0; target < 8; ++target) {
      uint16_t selection = 0;
      for (uint8_t device = 0; device < discoveredSp630eCount; ++device) {
        if (!discoveredSp630eAddresses[device].equalsIgnoreCase(
                draftSp630eAddresses[target])) continue;
        selection = target < 4
            ? device * Sp630eChannels::lightChoiceCount + Sp630eChannels::lightPosition(draftSp630eChannels[target]) + 1
            : device * Sp630eChannels::count + Sp630eChannels::position(draftSp630eChannels[target]) + 1;
      }
      lv_obj_t* dropdown = target < 4 ? sp630eDropdowns[target]
                                      : accessoryChannelDropdowns[target - 4];
      if (dropdown != nullptr) lv_dropdown_set_selected(dropdown, selection);
    }
    applyDeviceDisplayLayout();
    return;
  }
  if (!payload.startsWith("sp630e=")) return;
  discoveredSp630eCount = 0;
  memset(sp630eSeenInScan, 0, sizeof(sp630eSeenInScan));
  uint8_t scannedCount = 0;
  for (uint8_t target = 0; target < 8; ++target) {
    if (draftSp630eAddresses[target].isEmpty()) continue;
    bool known = false;
    for (uint8_t index = 0; index < discoveredSp630eCount; ++index)
      known |= discoveredSp630eAddresses[index].equalsIgnoreCase(
          draftSp630eAddresses[target]);
    if (!known && discoveredSp630eCount < 8)
      discoveredSp630eAddresses[discoveredSp630eCount++] =
          draftSp630eAddresses[target];
  }
  int start = 7;
  while (start < static_cast<int>(payload.length())) {
    int comma = payload.indexOf(',', start);
    int end = payload.indexOf(';', start);
    if (end < 0) end = payload.length();
    if (comma < 0 || comma > end) break;
    const String address = payload.substring(start, comma);
    uint8_t index = 0;
    while (index < discoveredSp630eCount &&
           !discoveredSp630eAddresses[index].equalsIgnoreCase(address)) ++index;
    if (index < 8) {
      if (index == discoveredSp630eCount)
        discoveredSp630eAddresses[discoveredSp630eCount++] = address;
      if (!sp630eSeenInScan[index]) ++scannedCount;
      sp630eSeenInScan[index] = true;
    }
    start = end + 1;
  }
  refreshSp630eDropdowns();
  for (uint8_t target = 0; target < 8; ++target) {
    uint16_t selection = 0;
    for (uint8_t device = 0; device < discoveredSp630eCount; ++device) {
      if (discoveredSp630eAddresses[device].equalsIgnoreCase(
              draftSp630eAddresses[target]))
        selection = target < 4
            ? device * Sp630eChannels::lightChoiceCount + Sp630eChannels::lightPosition(draftSp630eChannels[target]) + 1
            : device * Sp630eChannels::count + Sp630eChannels::position(draftSp630eChannels[target]) + 1;
    }
    lv_obj_t* dropdown = target < 4 ? sp630eDropdowns[target]
                                    : accessoryChannelDropdowns[target - 4];
    if (dropdown != nullptr) lv_dropdown_set_selected(dropdown, selection);
  }
  if (sp630eSavePending)
    lv_label_set_text(sp630eStatusLabel, "Saving; waiting for Controller confirmation");
  else if (sp630eDraftDirty)
    lv_label_set_text(sp630eStatusLabel, "Unsaved changes - tap " LV_SYMBOL_OK);
  else
    lv_label_set_text_fmt(sp630eStatusLabel, "%u devices seen. [saved] marks confirmed assignments", scannedCount);
}

void scanSp630eClicked(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (transportClient.requestSp630eDiscovery())
    lv_label_set_text(sp630eStatusLabel, "Scanning for 5 seconds...");
  else
    lv_label_set_text(sp630eStatusLabel, "Controller is offline");
}

void sp630eAssignmentChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  if (!sp630eConfigLoaded) {
    lv_label_set_text(sp630eStatusLabel, "Waiting for Controller configuration"); return;
  }
  const uint8_t target = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(
      lv_event_get_user_data(event)));
  const uint16_t selected = lv_dropdown_get_selected(
      static_cast<lv_obj_t*>(lv_event_get_target(event)));
  String address;
  uint8_t channel = target < 4 ? 0xFF : 0;
  if (selected != 0) {
    const uint16_t choice = selected - 1;
    const uint8_t perDevice = target < 4 ? Sp630eChannels::lightChoiceCount : Sp630eChannels::count;
    const uint8_t device = choice / perDevice;
    if (device >= discoveredSp630eCount) return;
    const uint8_t position = choice % perDevice;
    channel = target < 4 ? Sp630eChannels::lightChannel(position)
                         : Sp630eChannels::ids[position];
    address = discoveredSp630eAddresses[device];
  }
  draftSp630eChannels[target] = channel;
  draftSp630eAddresses[target] = address;
  sp630eDraftDirty = true;
  lv_label_set_text(sp630eStatusLabel, "Unsaved changes - tap " LV_SYMBOL_OK);
}

void lightGroupChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  if (!sp630eConfigLoaded) {
    lv_label_set_text(sp630eStatusLabel, "Waiting for Controller configuration"); return;
  }
  draftLightGroup = 0;
  for (uint8_t i = 0; i < 4; ++i)
    if (lv_obj_has_state(lightGroupChecks[i], LV_STATE_CHECKED)) draftLightGroup |= 1U << i;
  sp630eDraftDirty = true;
  lv_label_set_text(sp630eStatusLabel, "Unsaved changes - tap " LV_SYMBOL_OK);
}
void saveSp630eClicked(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (!sp630eConfigLoaded) {
    lv_label_set_text(sp630eStatusLabel, "Read Controller configuration before saving"); return;
  }
  if (!sp630eDraftDirty) {
    lv_label_set_text(sp630eStatusLabel, "No unsaved changes"); return;
  }
  if (sp630eSavePending && millis() - sp630eSaveStartedMs < 15000) {
    lv_label_set_text(sp630eStatusLabel, "Waiting for Controller to reconnect"); return;
  }
  const auto& status = transportClient.status();
  if (status.rearFirmwareMajor == 1 && status.rearFirmwareMinor == 0 && status.rearFirmwarePatch < 12) {
    lv_label_set_text(sp630eStatusLabel, "Update Controller to 1.0.12 before saving"); return;
  }
  String body = String(draftLightGroup) + "|";
  for (uint8_t i = 0; i < 8; ++i)
    body += String(i) + "," + String(draftSp630eChannels[i]) + "," +
            (draftSp630eAddresses[i].isEmpty() ? String("none") : draftSp630eAddresses[i]) + ";";
  Sp630eAssignment rows[8]{}; uint8_t group;
  if (!parseSp630eConfiguration(body.c_str(), rows, group)) {
    lv_label_set_text(sp630eStatusLabel, "Conflicting assignments - check channels"); return;
  }
  if (transportClient.saveSp630eConfiguration(body)) {
    sp630eSavePending = true;
    sp630eSaveStartedMs = millis();
    lv_label_set_text(sp630eStatusLabel, "Saving; Controller will restart once");
  } else lv_label_set_text(sp630eStatusLabel, "Controller offline - changes kept");
}

void openBluetoothControllers(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_remove_flag(bluetoothControllersOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(bluetoothControllersOverlay);
  transportClient.requestSp630eConfiguration();
  scanSp630eClicked(event);
}

void addDeviceConfigurationIdentity(lv_obj_t* parent, uint8_t labelId,
                                    int y, int nameWidth) {
  lv_obj_t* iconCircle = makeIconCircle(
      parent, 18, y + 1, 44,
      kDeviceIcons[deviceLabels[labelId].icon].symbol,
      &iconFont28, kColorCyan);
  deviceConfigIconLabels[labelId] = lv_obj_get_child(iconCircle, 0);
  lv_obj_add_flag(iconCircle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(iconCircle, openDeviceLabelEditor, LV_EVENT_CLICKED,
      reinterpret_cast<void*>(static_cast<uintptr_t>(labelId)));

  lv_obj_t* name = makeLabel(parent, deviceLabels[labelId].value,
                             70, y + 13,
                             &lv_font_montserrat_16, kColorText);
  lv_obj_set_width(name, nameWidth);
  deviceConfigNameLabels[labelId] = name;
  lv_obj_add_flag(name, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(name, 10);
  lv_obj_add_event_cb(name, openDeviceLabelEditor, LV_EVENT_CLICKED,
      reinterpret_cast<void*>(static_cast<uintptr_t>(labelId)));
}

lv_obj_t* checkButtons[6]{};
lv_obj_t* checkButtonLabels[6]{};
uint8_t checkButtonCount = 0;
void registerCheckButton(lv_obj_t* button, lv_obj_t* label) {
  lv_label_set_text(label, LV_SYMBOL_OK);
  checkButtons[checkButtonCount] = button;
  checkButtonLabels[checkButtonCount++] = label;
}
void matchKeyboardCheckButtons() {
  // Match the last key of the standard 800 x 258 editor keyboard. LVGL's
  // bottom row has 14 width units, with 2 units assigned to the confirm key.
  auto* keyboard = labelEditorKeyboard;
  const int width = 800 - lv_obj_get_style_space_left(keyboard, LV_PART_MAIN)
      - lv_obj_get_style_space_right(keyboard, LV_PART_MAIN)
      - 4 * lv_obj_get_style_pad_column(keyboard, LV_PART_MAIN);
  const int height = 258 - lv_obj_get_style_space_top(keyboard, LV_PART_MAIN)
      - lv_obj_get_style_space_bottom(keyboard, LV_PART_MAIN)
      - 3 * lv_obj_get_style_pad_row(keyboard, LV_PART_MAIN);
  const int keyWidth = width - width * 12 / 14;
  const int keyHeight = height - height * 3 / 4;
  const lv_font_t* font = lv_obj_get_style_text_font(keyboard, LV_PART_ITEMS);
  for (uint8_t i = 0; i < checkButtonCount; ++i) {
    auto* button = checkButtons[i];
    lv_obj_set_size(button, keyWidth, keyHeight);
    lv_obj_set_pos(button, 780 - keyWidth, 8);
    lv_obj_add_flag(button, LV_OBJ_FLAG_FLOATING);
    lv_obj_move_foreground(button);
    lv_obj_set_style_radius(button, lv_obj_get_style_radius(keyboard, LV_PART_ITEMS), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorCyan), 0);
    lv_obj_set_style_text_font(checkButtonLabels[i], font, 0);
    lv_obj_set_style_text_color(checkButtonLabels[i], lv_color_hex(kColorControlText), 0);
    lv_obj_center(checkButtonLabels[i]);
  }
}

void createBluetoothControllersOverlay() {
  bluetoothControllersOverlay = createPageOverlay("Device Configuration");
  lv_obj_add_flag(bluetoothControllersOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(bluetoothControllersOverlay, LV_DIR_VER);
  lv_obj_t* save = lv_button_create(bluetoothControllersOverlay);
  lv_obj_set_pos(save, 210, 50); lv_obj_set_size(save, 180, 46);
  lv_obj_set_style_bg_color(save, lv_color_hex(kColorCyan), 0);
  lv_obj_t* saveLabel = lv_label_create(save); registerCheckButton(save, saveLabel); lv_obj_center(saveLabel);
  lv_obj_add_event_cb(save, saveSp630eClicked, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* scan = lv_button_create(bluetoothControllersOverlay);
  lv_obj_set_pos(scan, 18, 50);
  lv_obj_set_size(scan, 180, 46);
  lv_obj_set_style_bg_color(scan, lv_color_hex(kColorCyan), 0);
  lv_obj_add_event_cb(scan, scanSp630eClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* scanLabel = lv_label_create(scan);
  lv_label_set_text(scanLabel, LV_SYMBOL_REFRESH "  Scan");
  lv_obj_center(scanLabel);
  sp630eStatusLabel = makeLabel(bluetoothControllersOverlay,
      "Discover SP630E Device · tap a name or icon to edit", 20, 118,
      &lv_font_montserrat_14, kColorText);
  const uint8_t targetLabels[] = {kLabelRgbwLight1, kLabelRgbwLight2,
                                  kLabelRgbwLight3, kLabelRgbwLight4};
  for (uint8_t target = 0; target < 4; ++target) {
    const int y = 140 + target * 57;
    addDeviceConfigurationIdentity(bluetoothControllersOverlay,
                                   targetLabels[target], y, 130);
    sp630eDropdowns[target] = lv_dropdown_create(bluetoothControllersOverlay);
    lv_obj_set_pos(sp630eDropdowns[target], 210, y);
    lv_obj_set_size(sp630eDropdowns[target], 570, 46);
    lv_dropdown_set_options(sp630eDropdowns[target], "Not assigned");
    lv_obj_add_event_cb(sp630eDropdowns[target], sp630eAssignmentChanged,
        LV_EVENT_VALUE_CHANGED,
        reinterpret_cast<void*>(static_cast<uintptr_t>(target)));
  }
  makeLabel(bluetoothControllersOverlay, "Accessory assignments", 20, 374,
            &lv_font_montserrat_16, kColorText);
  makeLabel(bluetoothControllersOverlay,
            "Channel PWM output is either 0% (Off) or 100% (On) at 12v",
            230, 396, &lv_font_montserrat_12, kColorText);
  for (uint8_t accessory = 0; accessory < 4; ++accessory) {
    const int y = 424 + accessory * 60;
    addDeviceConfigurationIdentity(bluetoothControllersOverlay,
        kLabelAccessory1 + accessory, y, 155);

    accessoryChannelDropdowns[accessory] =
        lv_dropdown_create(bluetoothControllersOverlay);
    lv_obj_set_pos(accessoryChannelDropdowns[accessory], 240, y);
    lv_obj_set_size(accessoryChannelDropdowns[accessory], 540, 46);
    lv_dropdown_set_options(accessoryChannelDropdowns[accessory],
                            "Not assigned");
    lv_obj_add_event_cb(accessoryChannelDropdowns[accessory],
        sp630eAssignmentChanged, LV_EVENT_VALUE_CHANGED,
        reinterpret_cast<void*>(static_cast<uintptr_t>(accessory + 4)));


  }
  makeLabel(bluetoothControllersOverlay, "Dashboard group", 20, 676,
            &lv_font_montserrat_16, kColorText);
  addDeviceConfigurationIdentity(bluetoothControllersOverlay,
                                 kLabelAllRgbwLights, 700, 300);
  lv_obj_t* lightGroupCard = makeCard(bluetoothControllersOverlay, 18, 756, 762, 146);
  makeLabel(lightGroupCard, "Choose the lights included in this group", 18, 14,
            &lv_font_montserrat_14, kColorText);
  for (uint8_t i = 0; i < 4; ++i) {
    lightGroupChecks[i] = lv_checkbox_create(lightGroupCard);
    lv_checkbox_set_text(lightGroupChecks[i], deviceLabels[labelForRgbZone(i)].value);
    lv_obj_set_pos(lightGroupChecks[i], 18 + (i % 2) * 380, 50 + (i / 2) * 44);
    lv_obj_set_style_text_color(lightGroupChecks[i], lv_color_hex(kColorText), 0);
    lv_obj_add_state(lightGroupChecks[i], LV_STATE_CHECKED);
    lv_obj_add_event_cb(lightGroupChecks[i], lightGroupChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  }
  lv_obj_t* bottomClearance = lv_obj_create(bluetoothControllersOverlay);
  lv_obj_remove_style_all(bottomClearance);
  lv_obj_set_pos(bottomClearance, 0, 916);
  lv_obj_set_size(bottomClearance, 1, 24);
}

void openCamperPosition(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_remove_flag(camperPositionOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(camperPositionOverlay);
}

void openDisplaySettings(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_remove_flag(displaySettingsOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(displaySettingsOverlay);
}

void openSystemInfo(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_remove_flag(systemInfoOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(systemInfoOverlay);
}

void openLabelConfiguration(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  applyDeviceLabels();
  lv_obj_remove_flag(labelConfigOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(labelConfigOverlay);
}

void openDeviceLabelEditor(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  editedDeviceLabel = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (editedDeviceLabel >= kDeviceLabelCount) return;
  editedDeviceIcon = deviceLabels[editedDeviceLabel].icon;
  editedDeviceColour = deviceIconColours[editedDeviceLabel];
  lv_obj_set_style_text_color(labelEditorIconLabel,
      lv_color_hex(deviceIconColour(editedDeviceLabel, kColorCyan)), 0);
  lv_label_set_text_fmt(labelEditorTitle, "Name for %s",
                        deviceLabels[editedDeviceLabel].channelName);
  lv_textarea_set_text(labelEditorTextArea,
                       deviceLabels[editedDeviceLabel].value);
  lv_keyboard_set_textarea(labelEditorKeyboard, labelEditorTextArea);
  setDeviceIcon(labelEditorIconLabel, editedDeviceIcon);
  const bool configurable = editedDeviceLabel != kLabelAllRgbwLights;
  lv_obj_t* layoutObjects[] = {labelEditorOrderLabel, labelEditorOrderDropdown};
  for (lv_obj_t* object : layoutObjects) {
    if (object == nullptr) continue;
    if (configurable) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
  if (configurable) {
    const uint8_t device = editedDeviceLabel - 1;
    lv_dropdown_set_selected(labelEditorOrderDropdown,
        deviceVisible[device] ? deviceOrder[device] + 1 : 0);
  }
  lv_obj_remove_flag(labelEditorOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(labelEditorOverlay);
  lv_obj_send_event(labelEditorTextArea, LV_EVENT_FOCUSED, nullptr);
}

void refreshIconPicker() {
  const uint32_t colour = editedDeviceColour ? kIconColours[editedDeviceColour] : kColorCyan;
  setDeviceIcon(iconPickerPreview, editedDeviceIcon);
  setDeviceIcon(labelEditorIconLabel, editedDeviceIcon);
  lv_obj_set_style_text_color(iconPickerPreview, lv_color_hex(colour), 0);
  lv_obj_set_style_text_color(labelEditorIconLabel, lv_color_hex(colour), 0);
  for (uint8_t i = 0; i < kIconColourCount; ++i) if (iconColourSwatches[i]) {
    lv_obj_set_style_outline_width(iconColourSwatches[i], i == editedDeviceColour ? 3 : 0, 0);
  }
  for (uint8_t i = 0; i < kDeviceIconCount; ++i) {
    const bool selected = i == editedDeviceIcon;
    lv_obj_set_style_bg_opa(iconPickerButtons[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(iconPickerSymbols[i], lv_color_hex(selected ? colour : kColorMuted), 0);
  }
}
void openIconPicker(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  refreshIconPicker();
  lv_obj_remove_flag(iconPickerOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(iconPickerOverlay);
}
void selectDeviceIcon(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const uint8_t icon = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (icon >= kDeviceIconCount) return;
  editedDeviceIcon = icon;
  refreshIconPicker();
}
void deviceIconColourChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const uint8_t choice = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (choice >= kIconColourCount) return;
  editedDeviceColour = choice;
  refreshIconPicker();
}

void saveIconSelection(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || editedDeviceLabel >= kDeviceLabelCount) return;
  deviceLabels[editedDeviceLabel].icon = editedDeviceIcon;
  deviceIconColours[editedDeviceLabel] = editedDeviceColour;
  if (uiPreferencesReady) {
    uiPreferences.putUChar(deviceLabels[editedDeviceLabel].iconNvsKey, editedDeviceIcon);
    char key[16]{}; snprintf(key, sizeof(key), "icon_col_%u", editedDeviceLabel);
    uiPreferences.putUChar(key, editedDeviceColour);
  }
  applyDeviceLabels();
  lv_obj_add_flag(iconPickerOverlay, LV_OBJ_FLAG_HIDDEN);
}

void saveDeviceLabel(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED &&
      lv_event_get_code(event) != LV_EVENT_READY) {
    return;
  }
  if (editedDeviceLabel >= kDeviceLabelCount) return;
  String value = lv_textarea_get_text(labelEditorTextArea);
  value.trim();
  if (value.length() == 0) value = deviceLabels[editedDeviceLabel].channelName;
  value.substring(0, kDeviceLabelLength - 1).toCharArray(
      deviceLabels[editedDeviceLabel].value, kDeviceLabelLength);
  deviceLabels[editedDeviceLabel].icon = editedDeviceIcon;
  deviceIconColours[editedDeviceLabel] = editedDeviceColour;
  if (editedDeviceLabel != kLabelAllRgbwLights) {
    const uint8_t device = editedDeviceLabel - 1;
    const uint8_t previousOrder = deviceOrder[device];
    const uint8_t selection = lv_dropdown_get_selected(labelEditorOrderDropdown);
    deviceVisible[device] = selection != 0;
    if (selection) {
      const uint8_t desiredOrder = selection - 1;
      for (uint8_t other = 0; other < kDeviceLabelCount - 1; ++other) {
        if (other != device && deviceOrder[other] == desiredOrder) {
          deviceOrder[other] = previousOrder;
          break;
        }
      }
      deviceOrder[device] = desiredOrder;
    } // Hidden devices retain their last position until explicitly placed.

  }
  if (uiPreferencesReady) {
    uiPreferences.putString(deviceLabels[editedDeviceLabel].nvsKey,
                            deviceLabels[editedDeviceLabel].value);
    uiPreferences.putUChar(deviceLabels[editedDeviceLabel].iconNvsKey,
                           deviceLabels[editedDeviceLabel].icon);
    char colourKey[16]{}; snprintf(colourKey, sizeof(colourKey), "icon_col_%u", editedDeviceLabel);
    uiPreferences.putUChar(colourKey, deviceIconColours[editedDeviceLabel]);
    for (uint8_t index = 0; index < kDeviceLabelCount - 1; ++index) {
      char visibleKey[12]{};
      char orderKey[12]{};
      snprintf(visibleKey, sizeof(visibleKey), "dev_vis_%u", index);
      snprintf(orderKey, sizeof(orderKey), "dev_ord_%u", index);
      uiPreferences.putBool(visibleKey, deviceVisible[index]);
      uiPreferences.putUChar(orderKey, deviceOrder[index]);
    }
  }
  applyDeviceLabels();
  lv_obj_add_flag(labelEditorOverlay, LV_OBJ_FLAG_HIDDEN);
  Serial.printf("Device label %s: %s\n",
                deviceLabels[editedDeviceLabel].channelName,
                deviceLabels[editedDeviceLabel].value);
}

void calibrateLevelClicked(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (!transportClient.connected()) {
    lv_label_set_text(calibrationStatusLabel,
                      "Controller is offline.");
    lv_obj_set_style_text_color(calibrationStatusLabel,
                                lv_color_hex(kColorRed), 0);
    return;
  }
  if (transportClient.send(BlueSquidControl::Command::CalibrateLevel, 0, 0)) {
    calibrationRequestedMs = millis();
    lv_label_set_text(calibrationStatusLabel,
                      "Calibrating and saving on the controller...");
    lv_obj_set_style_text_color(calibrationStatusLabel,
                                lv_color_hex(kColorCyan), 0);
  } else {
    lv_label_set_text(calibrationStatusLabel,
                      "Unable to send the calibration command.");
    lv_obj_set_style_text_color(calibrationStatusLabel,
                                lv_color_hex(kColorRed), 0);
  }
}

void setExportStatus(const char* message, uint32_t color) {
  if (settingsExportStatusLabel == nullptr) return;
  lv_label_set_text(settingsExportStatusLabel, message);
  lv_obj_set_style_text_color(settingsExportStatusLabel,
                              lv_color_hex(color), 0);
}

bool mountSdCard() {
  if (sdMounted && SD.cardType() != CARD_NONE) return true;
  sdMounted = false;
  sdMountError = "SD card is not initialized.";
  if (panel == nullptr || panel->getIO_Expander() == nullptr) {
    sdMountError = "SD control expander is unavailable.";
    Serial.println("SD mount failed: CH422G expander unavailable");
    return false;
  }

  auto* expander = panel->getIO_Expander()->getBase();
  if (expander == nullptr) {
    sdMountError = "SD control expander is unavailable.";
    Serial.println("SD mount failed: CH422G base unavailable");
    return false;
  }

  // The socket has no direct CS GPIO: CH422G EXIO4 enables it, while the
  // ESP32 SPI driver uses SS=-1. Rebind the global bus in case another
  // component initialized it first, then supply the SD-required idle clocks
  // while the real (expander) CS is high. The Arduino SD driver cannot toggle
  // an expander pin and Waveshare's basic example simply holds this CS low.
  SD.end();
  SPI.end();
  if (!expander->digitalWrite(kSdChipSelectExpanderPin, HIGH)) {
    sdMountError = "Unable to control the SD card-select pin.";
    Serial.println("SD mount failed: could not set EXIO4 high");
    return false;
  }
  SPI.setHwCs(false);
  SPI.begin(kSdClockPin, kSdMisoPin, kSdMosiPin, -1);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (uint8_t i = 0; i < 20; ++i) SPI.transfer(0xFF);
  SPI.endTransaction();
  if (!expander->digitalWrite(kSdChipSelectExpanderPin, LOW)) {
    sdMountError = "Unable to select the SD card.";
    Serial.println("SD mount failed: could not set EXIO4 low");
    return false;
  }
  delay(100);
  Serial.printf("SD: begin CLK=%d MISO=%d MOSI=%d CS=EXIO%d\n",
                kSdClockPin, kSdMisoPin, kSdMosiPin,
                kSdChipSelectExpanderPin);
  if (!SD.begin(-1, SPI, 4000000)) {
    Serial.println("SD: 4 MHz mount failed; retrying at 1 MHz");
    SD.end();
    expander->digitalWrite(kSdChipSelectExpanderPin, HIGH);
    delay(10);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < 20; ++i) SPI.transfer(0xFF);
    SPI.endTransaction();
    expander->digitalWrite(kSdChipSelectExpanderPin, LOW);
    delay(100);
    if (!SD.begin(-1, SPI, 1000000)) {
      sdMountError = "SD communication failed. See the serial diagnostic.";
      Serial.println("SD mount failed: both 4 MHz and 1 MHz attempts failed");
      return false;
    }
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    sdMountError = "No SD card was detected in the socket.";
    Serial.println("SD mount failed: card type is CARD_NONE");
    return false;
  }
  sdMounted = true;
  sdMountError = "";
  Serial.printf("SD card mounted: %llu MB\n",
                SD.cardSize() / (1024ULL * 1024ULL));
  return true;
}

void writeJsonString(File& file, const char* value) {
  file.print('"');
  for (const char* cursor = value; *cursor != '\0'; ++cursor) {
    switch (*cursor) {
      case '"': file.print("\\\""); break;
      case '\\': file.print("\\\\"); break;
      case '\n': file.print("\\n"); break;
      case '\r': file.print("\\r"); break;
      case '\t': file.print("\\t"); break;
      default: file.print(*cursor); break;
    }
  }
  file.print('"');
}

bool writeConfigurationExport(const TouchRemoteStatus& status) {
  if (!SD.exists(kExportDirectory) && !SD.mkdir(kExportDirectory)) {
    return false;
  }
  if (SD.exists(kExportTempPath)) SD.remove(kExportTempPath);
  File file = SD.open(kExportTempPath, FILE_WRITE);
  if (!file) return false;


  file.println("{");
  file.printf("  \"schema_version\": %u,\n", kConfigurationSchemaVersion);
  file.printf("  \"settings\": {\"battery_capacity_ah\": %.1f, "
              "\"pitch_zero_degrees\": %.2f, "
              "\"roll_zero_degrees\": %.2f, "
              "\"touchscreen_sleep_minutes\": %u, \"rvc_fan\": {\"enabled\": %s, \"instance\": %u, \"source_address\": %u}},\n",
              status.batteryCapacityAh, status.pitchZeroDegrees,
              status.rollZeroDegrees, sleepTimeoutMinutes, (status.fanFlags & 1) ? "true" : "false", status.fanInstance, status.fanSource);
  file.println("  \"device\": {");
  for (uint8_t index = 0; index < kDeviceLabelCount; ++index) {
    file.printf("    \"%s\": {\"label\": ", kDeviceJsonKeys[index]);
    writeJsonString(file, deviceLabels[index].value);
    file.print(", \"icon\": ");
    writeJsonString(file, kDeviceIcons[deviceLabels[index].icon].key);
    file.printf(", \"colour\": %u}%s\n", deviceIconColours[index], index + 1 < kDeviceLabelCount ? "," : "");
  }
  file.println("  },");
  file.println("  \"outputs\": {");
  for (uint8_t zone = 0; zone < 4; ++zone) {
    const uint8_t* levels = status.rgbwChannels(zone);
    file.printf("    \"rgbw_%u\": {\"on\": %s, "
                "\"output_rgbw_percent\": [%u, %u, %u, %u], "
                "\"preset_rgb_percent\": [%u, %u, %u], "
                "\"preset_brightness_percent\": %u, "
                "\"colour_selected\": %s, \"warm_white_selected\": %s, \"cool_white_selected\": %s},\n",
                zone + 1, (levels[0] || levels[1] || levels[2] || levels[3]) ? "true" : "false",
                levels[0], levels[1], levels[2], levels[3],
                status.rgb[zone][0], status.rgb[zone][1], status.rgb[zone][2],
                status.rgbwBrightness[zone],
                (status.rgbwOptions[zone] & 1) ? "true" : "false",
                (status.rgbwOptions[zone] & 2) ? "true" : "false",
                (status.rgbwOptions[zone] & 4) ? "true" : "false");
  }
  file.printf("    \"fan\": {\"speed_percent\": %u},\n", status.fan);
  file.printf("    \"accessory_1\": {\"enabled\": %s},\n",
              status.usb ? "true" : "false");
  file.printf("    \"accessory_2\": {\"enabled\": %s, "
              "\"restart_policy\": \"always_off\"},\n",
              status.pump ? "true" : "false");
  file.printf("    \"accessory_3\": {\"enabled\": %s, "
              "\"restart_policy\": \"always_off\"},\n",
              status.accessory3 ? "true" : "false");
  file.printf("    \"accessory_4\": {\"enabled\": %s}\n", status.accessory4 ? "true" : "false");
  file.println("  }");
  file.println("}");
  file.flush();
  const bool writeSucceeded = file.getWriteError() == 0;
  file.close();
  if (!writeSucceeded) {
    SD.remove(kExportTempPath);
    return false;
  }

  if (SD.exists(kExportBackupPath)) SD.remove(kExportBackupPath);
  const bool hadPrevious = SD.exists(kExportPath);
  if (hadPrevious && !SD.rename(kExportPath, kExportBackupPath)) {
    SD.remove(kExportTempPath);
    return false;
  }
  if (!SD.rename(kExportTempPath, kExportPath)) {
    if (hadPrevious) SD.rename(kExportBackupPath, kExportPath);
    return false;
  }
  if (hadPrevious) SD.remove(kExportBackupPath);
  return true;
}

bool readConfigurationImport(File& file, ImportedConfiguration& imported,
                             String& errorMessage) {
  if (file.size() == 0 || file.size() > 16384) {
    errorMessage = "Configuration file is empty or too large.";
    return false;
  }

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(document, file);
  if (jsonError) {
    errorMessage = String("Invalid JSON: ") + jsonError.c_str();
    return false;
  }
  if (!ConfigurationJson::validSections(document)) {
    errorMessage = "Schema version 1 with settings, device and outputs is required.";
    return false;
  }
  JsonObjectConst settings = document["settings"].as<JsonObjectConst>();
  JsonObjectConst devices = document["device"].as<JsonObjectConst>();
  JsonObjectConst outputs = document["outputs"].as<JsonObjectConst>();

  imported.batteryCapacityAh = settings["battery_capacity_ah"] | 0.0F;
  imported.pitchZeroDegrees = settings["pitch_zero_degrees"] | 999.0F;
  imported.rollZeroDegrees = settings["roll_zero_degrees"] | 999.0F;
  if (!ConfigurationJson::readRvcFan(settings["rvc_fan"].as<JsonObjectConst>(), imported.rvcFan)) {
    errorMessage = "Invalid RV-C fan configuration."; return false;
  }
  const int sleepMinutes = settings["touchscreen_sleep_minutes"] | -1;
  if (!isfinite(imported.batteryCapacityAh) ||
      imported.batteryCapacityAh < 10.0F ||
      imported.batteryCapacityAh > 2000.0F ||
      !isfinite(imported.pitchZeroDegrees) ||
      !isfinite(imported.rollZeroDegrees) ||
      imported.pitchZeroDegrees < -180.0F ||
      imported.pitchZeroDegrees > 180.0F ||
      imported.rollZeroDegrees < -180.0F ||
      imported.rollZeroDegrees > 180.0F || sleepMinutes < 0 ||
      sleepTimeoutChoiceIndex(static_cast<uint16_t>(sleepMinutes)) >=
          sizeof(kSleepTimeoutChoices) / sizeof(kSleepTimeoutChoices[0]) ||
      kSleepTimeoutChoices[sleepTimeoutChoiceIndex(
          static_cast<uint16_t>(sleepMinutes))] != sleepMinutes) {
    errorMessage = "One or more saved settings are out of range.";
    return false;
  }
  imported.sleepMinutes = static_cast<uint16_t>(sleepMinutes);

  for (uint8_t index = 0; index < kDeviceLabelCount; ++index) {
    JsonObjectConst device = devices[kDeviceJsonKeys[index]].as<JsonObjectConst>();
    const char* iconKey = nullptr;
    if (!ConfigurationJson::readDevice(device, imported.labels[index], kDeviceLabelLength,
        iconKey, imported.iconColours[index], kIconColourCount)) {
      errorMessage = String("Invalid device: ") + kDeviceJsonKeys[index]; return false;
    }
    const uint8_t icon = deviceIconIndexForKey(iconKey, kDeviceIconCount);
    if (icon >= kDeviceIconCount) {
      errorMessage = String("Invalid icon: ") + kDeviceJsonKeys[index]; return false;
    }
    imported.icons[index] = icon;
  }

  const char* rgbwKeys[4] = {"rgbw_1", "rgbw_2", "rgbw_3", "rgbw_4"};

  for (uint8_t zone = 0; zone < 4; ++zone) {
    JsonObjectConst output = outputs[rgbwKeys[zone]].as<JsonObjectConst>();
    if (!ConfigurationJson::readLight(output, imported.rgbwOutput[zone], imported.rgbPreset[zone],
        imported.rgbwBrightness[zone], imported.rgbwOptions[zone])) {
      errorMessage = String("Invalid light: ") + rgbwKeys[zone]; return false;
    }
  }

  JsonObjectConst fan = outputs["fan"].as<JsonObjectConst>();
  JsonObjectConst accessory1 = outputs["accessory_1"].as<JsonObjectConst>();
  if (fan.isNull() ||
      !ConfigurationJson::percent(fan["speed_percent"], imported.fanSpeed) ||
      accessory1.isNull() || !accessory1["enabled"].is<bool>()) {
    errorMessage = "Invalid fan or accessory configuration.";
    return false;
  }
  imported.accessory1Enabled = accessory1["enabled"].as<bool>();
  for (const char* key : {"accessory_2", "accessory_3", "accessory_4"}) {
    if (!outputs[key]["enabled"].is<bool>()) {
      errorMessage = "Invalid accessory configuration"; return false;
    }
  }
  imported.accessory4Enabled = outputs["accessory_4"]["enabled"].as<bool>();
  return true;
}

bool sendImportedRearConfiguration(const ImportedConfiguration& imported) {
  bool sent = true;
  const uint16_t capacity = static_cast<uint16_t>(
      constrain(lroundf(imported.batteryCapacityAh * 10.0F), 100L, 20000L));
  sent = transportClient.send(BlueSquidControl::Command::SetBatteryCapacity,
                              0, capacity) && sent;
  sent = transportClient.send(
      BlueSquidControl::Command::SetLevelCalibration, 0,
      static_cast<uint16_t>(BlueSquidControl::scaled(
          imported.pitchZeroDegrees, 100.0F))) && sent;
  sent = transportClient.send(
      BlueSquidControl::Command::SetLevelCalibration, 1,
      static_cast<uint16_t>(BlueSquidControl::scaled(
          imported.rollZeroDegrees, 100.0F))) && sent;

  for (uint8_t zone = 0; zone < 4; ++zone) {
    for (uint8_t channel = 0; channel < 4; ++channel) {
      sent = transportClient.send(
          BlueSquidControl::Command::SetRgbw,
          static_cast<uint8_t>((zone << 4) | channel),
          imported.rgbwOutput[zone][channel]) && sent;
    }
    for (uint8_t channel = 0; channel < 3; ++channel) {
      sent = transportClient.send(
          BlueSquidControl::Command::SetRgbwPreset,
          static_cast<uint8_t>((zone << 4) | channel),
          imported.rgbPreset[zone][channel]) && sent;
    }
    sent = transportClient.send(
        BlueSquidControl::Command::SetRgbwPreset,
        static_cast<uint8_t>((zone << 4) | 3),
        imported.rgbwBrightness[zone]) && sent;
    sent = transportClient.send(
        BlueSquidControl::Command::SetRgbwPreset,
        static_cast<uint8_t>((zone << 4) | 4),
        imported.rgbwOptions[zone]) && sent;
  }
  sent = transportClient.saveRvcFanConfiguration(imported.rvcFan.enabled,
      imported.rvcFan.instance, imported.rvcFan.source) && sent;
  // Import config without changing the externally controlled fan's power state.
  sent = transportClient.send(BlueSquidControl::Command::SetUsb, 0,
                              imported.accessory1Enabled ? 1 : 0) && sent;
  sent = transportClient.send(BlueSquidControl::Command::SetAccessory4, 0,
                              imported.accessory4Enabled ? 1 : 0) && sent;
  // Never energize these safety-sensitive outputs as a side effect of import.
  sent = transportClient.send(BlueSquidControl::Command::SetPump, 0, 0) && sent;
  sent = transportClient.send(BlueSquidControl::Command::SetAccessory3, 0, 0) && sent;
  return sent;
}

void importConfigurationClicked(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (!transportClient.connected()) {
    setExportStatus("Controller is offline; import cancelled.",
                    kColorRed);
    return;
  }
  if (!mountSdCard()) {
    setExportStatus(sdMountError, kColorRed);
    return;
  }
  File file = SD.open(kExportPath, FILE_READ);
  if (!file) {
    setExportStatus("No /bluesquid/config-latest.json file found.", kColorRed);
    return;
  }
  ImportedConfiguration imported{};
  String errorMessage;
  const bool valid = readConfigurationImport(file, imported, errorMessage);
  file.close();
  if (!valid) {
    setExportStatus(errorMessage.c_str(), kColorRed);
    Serial.printf("Configuration import rejected: %s\n", errorMessage.c_str());
    return;
  }
  if (!sendImportedRearConfiguration(imported)) {
    setExportStatus("Import transmission failed; verify controller connection.",
                    kColorRed);
    return;
  }

  sleepTimeoutMinutes = imported.sleepMinutes;
  lastUserActivityMs = millis();
  if (sleepTimeoutDropdown != nullptr) {
    lv_dropdown_set_selected(
        sleepTimeoutDropdown, sleepTimeoutChoiceIndex(sleepTimeoutMinutes));
  }
  for (uint8_t index = 0; index < kDeviceLabelCount; ++index) {
    strlcpy(deviceLabels[index].value, imported.labels[index],
            kDeviceLabelLength);
    deviceLabels[index].icon = imported.icons[index];
    deviceIconColours[index] = imported.iconColours[index];
  }
  if (uiPreferencesReady) {
    uiPreferences.putUShort("sleep_min", sleepTimeoutMinutes);
    for (uint8_t index = 0; index < kDeviceLabelCount; ++index) {
      uiPreferences.putString(deviceLabels[index].nvsKey,
                              deviceLabels[index].value);
      uiPreferences.putUChar(deviceLabels[index].iconNvsKey,
                             deviceLabels[index].icon);
      char colourKey[16]{}; snprintf(colourKey, sizeof(colourKey), "icon_col_%u", index);
      uiPreferences.putUChar(colourKey, deviceIconColours[index]);
    }
  }
  applyDeviceLabels();
  transportClient.send(BlueSquidControl::Command::RequestStatus, 0, 0);
  setExportStatus("Configuration imported. Pump and accessory 3 remain off.",
                  kColorGreen);
  Serial.printf("Configuration imported from %s\n", kExportPath);
}

void exportConfigurationClicked(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (!transportClient.connected()) {
    setExportStatus("Controller is offline; export cancelled.",
                    kColorRed);
    return;
  }
  const auto& status = transportClient.status();
  if (!status.rgbwPresetValid[0] || !status.rgbwPresetValid[1] ||
      !status.settingsValid) {
    transportClient.send(BlueSquidControl::Command::RequestStatus, 0, 0);
    setExportStatus("Waiting for complete controller configuration. Try again.",
                    kColorAmber);
    return;
  }
  if (!mountSdCard()) {
    setExportStatus(sdMountError, kColorRed);
    return;
  }
  if (!writeConfigurationExport(status)) {
    setExportStatus("Unable to write the configuration backup.", kColorRed);
    return;
  }
  setExportStatus("Saved /bluesquid/config-latest.json", kColorGreen);
  Serial.printf("Configuration exported to %s\n", kExportPath);
}

void addMenuHitTarget(lv_obj_t* panel, int y, lv_event_cb_t callback) {
  lv_obj_t* target = lv_obj_create(panel);
  lv_obj_set_pos(target, 0, y);
  lv_obj_set_size(target, 744, 67);
  lv_obj_remove_flag(target, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(target, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(target, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(target, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(target, 0, 0);
  lv_obj_set_style_pad_all(target, 0, 0);
  lv_obj_add_event_cb(target, callback, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t* addSystemInfoRow(lv_obj_t* panel, int y, const char* title,
                           const char* value, bool separator) {
  makeLabel(panel, title, 24, y + 13, &lv_font_montserrat_16, kColorMuted);
  lv_obj_t* valueLabel =
      makeLabel(panel, value, 365, y + 13, &lv_font_montserrat_16, kColorText);
  lv_obj_set_width(valueLabel, 375);
  lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
  if (separator) {
    lv_obj_t* line = lv_obj_create(panel);
    lv_obj_set_pos(line, 24, y + 47);
    lv_obj_set_size(line, 716, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(kColorBorder), 0);
    lv_obj_set_style_border_width(line, 0, 0);
  }
  return valueLabel;
}

void cerboWifiFieldFocused(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_FOCUSED &&
      cerboWifiKeyboard != nullptr)
    lv_keyboard_set_textarea(
        cerboWifiKeyboard, static_cast<lv_obj_t*>(lv_event_get_target(event)));
}

void saveCerboWifi(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_READY) return;
  const String ssid = lv_textarea_get_text(cerboWifiSsid);
  const String password = lv_textarea_get_text(cerboWifiPassword);
  const int vebusUnitId = String(lv_textarea_get_text(cerboVebusUnitId)).toInt();
  if (ssid.isEmpty() || password.length() < 8 || vebusUnitId < 1 ||
      vebusUnitId > 247) {
    lv_label_set_text(cerboWifiStatus,
                      "SSID and 8-63 character password required; VE.Bus ID 1-247");
    return;
  }
  if (transportClient.configureCerboWifi(
          ssid, password, static_cast<uint8_t>(vebusUnitId))) {
    lv_textarea_set_text(cerboWifiPassword, "");
    lv_label_set_text(cerboWifiStatus, "Queued - connecting to Cerbo hotspot");
  } else {
    lv_label_set_text(cerboWifiStatus, "Controller is not connected");
  }
}

void openCerboWifi(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_remove_flag(cerboWifiOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cerboWifiOverlay);
  transportClient.requestCerboWifiConfiguration();
}

void createCerboWifiOverlay() {
  cerboWifiOverlay = createPageOverlay("Victron Cerbo GX");
  makeLabel(cerboWifiOverlay, "Cerbo hotspot name (SSID)", 20, 53,
            &lv_font_montserrat_14, kColorMuted);
  cerboWifiSsid = lv_textarea_create(cerboWifiOverlay);
  lv_obj_set_pos(cerboWifiSsid, 20, 76);
  lv_obj_set_size(cerboWifiSsid, 300, 48);
  lv_textarea_set_one_line(cerboWifiSsid, true);
  lv_textarea_set_max_length(cerboWifiSsid, 32);
  lv_textarea_set_placeholder_text(cerboWifiSsid, "Cerbo hotspot SSID");
  lv_obj_add_event_cb(cerboWifiSsid, cerboWifiFieldFocused,
                      LV_EVENT_FOCUSED, nullptr);

  makeLabel(cerboWifiOverlay, "Password", 340, 53,
            &lv_font_montserrat_14, kColorMuted);
  cerboWifiPassword = lv_textarea_create(cerboWifiOverlay);
  lv_obj_set_pos(cerboWifiPassword, 340, 76);
  lv_obj_set_size(cerboWifiPassword, 210, 48);
  lv_textarea_set_one_line(cerboWifiPassword, true);
  lv_textarea_set_password_mode(cerboWifiPassword, true);
  lv_textarea_set_max_length(cerboWifiPassword, 63);
  lv_textarea_set_placeholder_text(cerboWifiPassword, "Enter new password");
  lv_obj_add_event_cb(cerboWifiPassword, cerboWifiFieldFocused,
                      LV_EVENT_FOCUSED, nullptr);

  makeLabel(cerboWifiOverlay, "VE.Bus ID", 570, 53,
            &lv_font_montserrat_14, kColorMuted);
  cerboVebusUnitId = lv_textarea_create(cerboWifiOverlay);
  lv_obj_set_pos(cerboVebusUnitId, 570, 76);
  lv_obj_set_size(cerboVebusUnitId, 80, 48);
  lv_textarea_set_one_line(cerboVebusUnitId, true);
  lv_textarea_set_accepted_chars(cerboVebusUnitId, "0123456789");
  lv_textarea_set_max_length(cerboVebusUnitId, 3);
  lv_textarea_set_text(cerboVebusUnitId, "227");
  lv_obj_add_event_cb(cerboVebusUnitId, cerboWifiFieldFocused,
                      LV_EVENT_FOCUSED, nullptr);

  lv_obj_t* save = lv_button_create(cerboWifiOverlay);
  lv_obj_set_pos(save, 655, 76);
  lv_obj_set_size(save, 125, 48);
  lv_obj_set_style_bg_color(save, lv_color_hex(kColorCyan), 0);
  lv_obj_set_style_radius(save, 14, 0);
  lv_obj_add_event_cb(save, saveCerboWifi, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* saveText = lv_label_create(save);
  registerCheckButton(save, saveText);
  lv_obj_set_style_text_color(saveText, lv_color_hex(kColorControlText), 0);
  lv_obj_center(saveText);

  cerboWifiStatus = makeLabel(cerboWifiOverlay, "Loading status...", 20, 144,
                              &lv_font_montserrat_14, kColorMuted);
  cerboWifiKeyboard = lv_keyboard_create(cerboWifiOverlay);
  lv_obj_set_size(cerboWifiKeyboard, 800, 230);
  lv_obj_align(cerboWifiKeyboard, LV_ALIGN_TOP_LEFT, 0, 170);
  lv_keyboard_set_textarea(cerboWifiKeyboard, cerboWifiSsid);
  lv_obj_add_event_cb(cerboWifiKeyboard, saveCerboWifi, LV_EVENT_READY,
                      nullptr);
  lv_obj_add_flag(cerboWifiOverlay, LV_OBJ_FLAG_HIDDEN);
}

void hotspotFieldFocused(lv_event_t* event) {
  if (lv_event_get_code(event)==LV_EVENT_FOCUSED)
    lv_keyboard_set_textarea(hotspotKeyboard, static_cast<lv_obj_t*>(lv_event_get_target(event)));
}
void saveHotspot(lv_event_t* event) {
  if (lv_event_get_code(event)!=LV_EVENT_CLICKED && lv_event_get_code(event)!=LV_EVENT_READY) return;
  if (TouchHotspot::configure(lv_textarea_get_text(hotspotSsid), lv_textarea_get_text(hotspotPassword))) {
    lv_textarea_set_text(hotspotPassword, "");
    lv_label_set_text(hotspotStatus, "Queued - reconnect your phone after the hotspot restarts");
  } else lv_label_set_text(hotspotStatus, "SSID and 8-63 character password required");
}
void openHotspot(lv_event_t* event) {
  if (lv_event_get_code(event)!=LV_EVENT_CLICKED) return;
  const auto status=TouchHotspot::status();
  lv_textarea_set_text(hotspotSsid,status.ssid);
  lv_label_set_text_fmt(hotspotStatus, "%s - %s - %u phone(s)",
      status.active ? "Active" : "Unavailable", status.ip, status.clients);
  lv_obj_remove_flag(hotspotOverlay,LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(hotspotOverlay);
}
void createHotspotOverlay() {
  hotspotOverlay=createPageOverlay("System hotspot");
  makeLabel(hotspotOverlay,"Hotspot name (SSID)",20,53,&lv_font_montserrat_16,kColorText);
  makeLabel(hotspotOverlay,"Password",340,53,&lv_font_montserrat_16,kColorText);
  hotspotSsid=lv_textarea_create(hotspotOverlay);
  hotspotPassword=lv_textarea_create(hotspotOverlay);
  for (lv_obj_t* field : {hotspotSsid,hotspotPassword}) {
    lv_obj_set_size(field,300,48);
    lv_textarea_set_one_line(field,true);
    lv_obj_add_event_cb(field,hotspotFieldFocused,LV_EVENT_FOCUSED,nullptr);
  }
  lv_obj_set_pos(hotspotSsid,20,76); lv_obj_set_pos(hotspotPassword,340,76);
  lv_textarea_set_max_length(hotspotSsid,32);
  lv_textarea_set_max_length(hotspotPassword,63);
  lv_textarea_set_password_mode(hotspotPassword,true);
  lv_textarea_set_placeholder_text(hotspotPassword,"Enter new password");
  lv_obj_t* save=lv_button_create(hotspotOverlay);
  lv_obj_set_pos(save,670,76); lv_obj_set_size(save,100,48);
  lv_obj_t* label=lv_label_create(save); registerCheckButton(save, label); lv_obj_center(label);
  lv_obj_add_event_cb(save,saveHotspot,LV_EVENT_CLICKED,nullptr);
  hotspotStatus=makeLabel(hotspotOverlay,"",20,144,&lv_font_montserrat_14,kColorMuted);
  hotspotKeyboard=lv_keyboard_create(hotspotOverlay);
  lv_obj_set_size(hotspotKeyboard,800,230);
  lv_obj_align(hotspotKeyboard,LV_ALIGN_TOP_LEFT,0,170);
  lv_keyboard_set_textarea(hotspotKeyboard,hotspotSsid);
  lv_obj_add_event_cb(hotspotKeyboard,saveHotspot,LV_EVENT_READY,nullptr);
}

// Walk visible UI objects so this applies to every keyboard, including new
// settings pages. Overlay back buttons remain available with navigation hidden.
void syncKeyboardNavigation(lv_obj_t* object, bool& visible) {
  if (lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN)) return;
  if (lv_obj_check_type(object,&lv_keyboard_class)) {
    visible=true;
    lv_obj_t* page=lv_obj_get_parent(object);
    lv_obj_set_height(page,480);
    lv_obj_set_y(object,480-lv_obj_get_height(object)-16);
    return;
  }
  for (uint32_t i=0;i<lv_obj_get_child_count(object);++i)
    syncKeyboardNavigation(lv_obj_get_child(object,i),visible);
}

void saveRvcFan(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || !rvcConfigLoaded || rvcSaving) return;
  if (transportClient.saveRvcFanConfiguration(lv_dropdown_get_selected(rvcEnabled),
      lv_dropdown_get_selected(rvcInstance) + 1, lv_dropdown_get_selected(rvcSource) + 151)) {
    rvcSaving = true; rvcSaveMs = millis();
    lv_label_set_text(rvcStatusLabel, "Saving configuration...");
  } else lv_label_set_text(rvcStatusLabel, "Controller offline - configuration not sent");
}
void openRvcFan(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  rvcConfigLoaded = false; rvcSaving = false;
  lv_label_set_text(rvcStatusLabel, "Loading Controller configuration...");
  if (!transportClient.requestRvcFanConfiguration()) lv_label_set_text(rvcStatusLabel, "Controller offline");
  lv_obj_remove_flag(rvcOverlay, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(rvcOverlay);
}
void createRvcFanOverlay() {
  rvcOverlay = createPageOverlay("RV-C");
  auto* card = makeCard(rvcOverlay, 16, 76, 768, 240);
  makeIconCircle(card, 18, 15, 46, BLUESQUID_SYMBOL_FAN, &iconFont28, kColorLightbulb);
  makeLabel(card, "Dometic FA75", 80, 23, &lv_font_montserrat_18, kColorText);
  makeLabel(card, "Connection", 20, 86, &lv_font_montserrat_14, kColorText);
  makeLabel(card, "Fan instance", 275, 86, &lv_font_montserrat_14, kColorText);
  makeLabel(card, "Preferred controller address", 488, 86, &lv_font_montserrat_14, kColorText);
  rvcEnabled = lv_dropdown_create(card); rvcInstance = lv_dropdown_create(card); rvcSource = lv_dropdown_create(card);
  lv_dropdown_set_options(rvcEnabled, "Disabled\nEnabled");
  String instances, sources;
  for (unsigned i = 1; i <= 250; ++i) { if (i > 1) instances += '\n'; instances += i; }
  for (unsigned i = 151; i <= 159; ++i) { if (i > 151) sources += '\n'; sources += i; }
  lv_dropdown_set_options(rvcInstance, instances.c_str()); lv_dropdown_set_options(rvcSource, sources.c_str());
  lv_dropdown_set_selected(rvcSource, 8);
  lv_obj_set_pos(rvcEnabled, 20, 112); lv_obj_set_width(rvcEnabled, 225);
  lv_obj_set_pos(rvcInstance, 275, 112); lv_obj_set_width(rvcInstance, 175);
  lv_obj_set_pos(rvcSource, 488, 112); lv_obj_set_width(rvcSource, 250);
  auto* description = makeLabel(card, "Enable after connecting the CAN transceiver. Match the fan instance to your installation.\nSpeed uses manual control. Reverse air selects intake; lid and rain settings remain unchanged.", 20, 178,
      &lv_font_montserrat_14, kColorText);
  lv_obj_set_width(description, 724);
  rvcStatusLabel = makeLabel(rvcOverlay, "", 20, 336, &lv_font_montserrat_14, kColorMuted);
  rvcSaveButton = lv_button_create(rvcOverlay);
  lv_obj_set_style_bg_color(rvcSaveButton, lv_color_hex(kColorCyan), 0);
  lv_obj_set_style_radius(rvcSaveButton, 14, 0);
  auto* label = lv_label_create(rvcSaveButton); registerCheckButton(rvcSaveButton, label); lv_obj_center(label);
  lv_obj_set_style_text_color(label, lv_color_hex(kColorControlText), 0);
  lv_obj_add_event_cb(rvcSaveButton, saveRvcFan, LV_EVENT_CLICKED, nullptr);
}

void showFirmwareUpdate(lv_event_t*) {
  if (!firmwareUpdateOverlay) firmwareUpdateOverlay = createPageOverlay("Firmware updates");
  auto* overlay = firmwareUpdateOverlay;
  const auto hotspot = TouchHotspot::status();
  static auto* text = makeLabel(overlay, "", 24, 64, &lv_font_montserrat_16, kColorText);
  lv_obj_set_width(text, 744);
  lv_label_set_text_fmt(text,
      "Touchscreen %s\nConnect to Wi-Fi: %s\nOpen http://%s:8080 in your browser\n"
      "No separate login is needed on the firmware update page.\n\n"
      "Choose Controller or Touchscreen on the update page.\n"
      "Stay connected to this hotspot for both updates.\n"
      "Update the Controller first, then the touchscreen.\n"
      "Cerbo readings pause during a Controller update.\n\n"
      "Choose the matching BlueSquid .bsfw file. Keep power on.\n%s",
      AppConfig::kFirmwareVersion, hotspot.ssid, hotspot.ip,
      FirmwareUpdate::available() ? "" :
      "Touchscreen OTA unavailable: install by USB first.");
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
}

void createSettingsOverlay() {
  settingsOverlay = createPageOverlay("System Configuration");
  lv_obj_add_flag(settingsOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(settingsOverlay, LV_DIR_VER);
  makeLabel(settingsOverlay, "Device configuration", 18, 52,
            &lv_font_montserrat_16, kColorText);
  lv_obj_t* bluetoothCard = makeCard(settingsOverlay, 16, 76, 768, 74);
  lv_obj_add_flag(bluetoothCard, LV_OBJ_FLAG_CLICKABLE);
  makeIconCircle(bluetoothCard, 16, 11, 52, LV_SYMBOL_BLUETOOTH,
                 &lv_font_montserrat_24, kColorCyan);
  makeLabel(bluetoothCard, "Bluetooth Module", 84, 13,
            &lv_font_montserrat_18, kColorText);
  makeLabel(bluetoothCard, "Discover SP630E Device",
            84, 40, &lv_font_montserrat_12, kColorMuted);
  makeLabel(bluetoothCard, LV_SYMBOL_RIGHT, 724, 26,
            &lv_font_montserrat_18, kColorMuted);
  lv_obj_add_event_cb(bluetoothCard, openBluetoothControllers,
                      LV_EVENT_CLICKED, nullptr);

  lv_obj_t* wifiCard = makeCard(settingsOverlay, 16, 158, 768, 74);
  lv_obj_add_flag(wifiCard, LV_OBJ_FLAG_CLICKABLE);
  makeIconCircle(wifiCard, 16, 11, 52, LV_SYMBOL_WIFI,
                 &lv_font_montserrat_24, kColorGreen);
  makeLabel(wifiCard, "Victron Cerbo GX", 84, 13,
            &lv_font_montserrat_18, kColorText);
  makeLabel(wifiCard, "Connect Controller to the Cerbo hotspot", 84, 40,
            &lv_font_montserrat_12, kColorMuted);
  makeLabel(wifiCard, LV_SYMBOL_RIGHT, 724, 26,
            &lv_font_montserrat_18, kColorMuted);
  lv_obj_add_event_cb(wifiCard, openCerboWifi, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* hotspotCard = makeCard(settingsOverlay, 16, 240, 768, 74);
  lv_obj_add_flag(hotspotCard, LV_OBJ_FLAG_CLICKABLE);
  makeIconCircle(hotspotCard, 16, 11, 52, LV_SYMBOL_WIFI,
                 &lv_font_montserrat_24, kColorCyan);
  makeLabel(hotspotCard, "System hotspot", 84, 13, &lv_font_montserrat_18, kColorText);
  makeLabel(hotspotCard, "Phone access through the touchscreen", 84, 40,
            &lv_font_montserrat_12, kColorMuted);
  lv_obj_add_event_cb(hotspotCard, openHotspot, LV_EVENT_CLICKED, nullptr);

  auto* rvcCard = makeCard(settingsOverlay, 16, 322, 768, 74);
  lv_obj_add_flag(rvcCard, LV_OBJ_FLAG_CLICKABLE);
  makeIconCircle(rvcCard, 16, 11, 52, BLUESQUID_SYMBOL_FAN, &iconFont28, kColorLightbulb);
  makeLabel(rvcCard, "RV-C", 84, 13, &lv_font_montserrat_18, kColorText);
  makeLabel(rvcCard, "Dometic FA75 vent fan", 84, 40, &lv_font_montserrat_12, kColorMuted);
  lv_obj_add_event_cb(rvcCard, openRvcFan, LV_EVENT_CLICKED, nullptr);
  makeLabel(settingsOverlay, "Configuration backup", 18, 412,
            &lv_font_montserrat_16, kColorText);
  lv_obj_t* card = makeCard(settingsOverlay, 16, 436, 768, 170);
  lv_obj_t* exportButton = lv_button_create(card);
  lv_obj_set_pos(exportButton, 20, 22);
  lv_obj_set_size(exportButton, 215, 55);
  lv_obj_set_style_radius(exportButton, 18, 0);
  lv_obj_set_style_bg_color(exportButton, lv_color_hex(kColorSurface), 0);
  lv_obj_set_style_bg_color(exportButton, lv_color_hex(kColorSurface),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(exportButton, 1, 0);
  lv_obj_set_style_border_color(exportButton, lv_color_hex(kColorBorder), 0);
  lv_obj_set_style_color_filter_opa(exportButton, LV_OPA_TRANSP,
                                    LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(exportButton, 0, 0);
  lv_obj_add_event_cb(exportButton, exportConfigurationClicked,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* exportButtonLabel = lv_label_create(exportButton);
  lv_label_set_text(exportButtonLabel, LV_SYMBOL_SD_CARD "  Export");
  lv_obj_set_style_text_font(exportButtonLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(exportButtonLabel, lv_color_hex(kColorText), 0);
  lv_obj_center(exportButtonLabel);

  lv_obj_t* importButton = lv_button_create(card);
  lv_obj_set_pos(importButton, 247, 22);
  lv_obj_set_size(importButton, 215, 55);
  lv_obj_set_style_radius(importButton, 18, 0);
  lv_obj_set_style_bg_color(importButton, lv_color_hex(kColorSurface), 0);
  lv_obj_set_style_bg_color(importButton, lv_color_hex(kColorSurface),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(importButton, 1, 0);
  lv_obj_set_style_border_color(importButton, lv_color_hex(kColorBorder), 0);
  lv_obj_set_style_color_filter_opa(importButton, LV_OPA_TRANSP,
                                    LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(importButton, 0, 0);
  lv_obj_add_event_cb(importButton, importConfigurationClicked,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* importButtonLabel = lv_label_create(importButton);
  lv_label_set_text(importButtonLabel, LV_SYMBOL_DOWNLOAD "  Import");
  lv_obj_set_style_text_font(importButtonLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(importButtonLabel, lv_color_hex(kColorText), 0);
  lv_obj_center(importButtonLabel);

  settingsExportStatusLabel = makeLabel(
      card, "Backup or restore using the touchscreen SD card.",
      482, 25, &lv_font_montserrat_14, kColorMuted);
  lv_obj_set_width(settingsExportStatusLabel, 260);

  lv_obj_t* backupDescription = makeLabel(
      card,
      "The backup includes device labels and icons, display timeout, "
      "battery settings, calibration and saved output presets.",
      20, 100, &lv_font_montserrat_14, kColorMuted);
  lv_obj_set_width(backupDescription, 720);

  auto* updateCard = makeCard(settingsOverlay, 16, 618, 768, 74);
  lv_obj_add_flag(updateCard, LV_OBJ_FLAG_CLICKABLE);
  makeLabel(updateCard, "Firmware updates", 20, 13, &lv_font_montserrat_18, kColorText);
  makeLabel(updateCard, "Install firmware from a browser over Wi-Fi", 20, 40,
            &lv_font_montserrat_12, kColorMuted);
  lv_obj_add_event_cb(updateCard, showFirmwareUpdate, LV_EVENT_CLICKED, nullptr);

  camperPositionOverlay = createPageOverlay("Camper Position");
  makeLabel(camperPositionOverlay, "Level calibration", 18, 55,
            &lv_font_montserrat_16, kColorText);
  lv_obj_t* positionCard =
      makeCard(camperPositionOverlay, 16, 82, 768, 230);
  makeIconCircle(positionCard, 20, 20, 52, LV_SYMBOL_REFRESH,
                 &lv_font_montserrat_24, kColorCyan);
  makeLabel(positionCard, "Current pitch", 92, 21,
            &lv_font_montserrat_14, kColorMuted);
  settingsPitchLabel = makeLabel(positionCard, "--.-°", 92, 44,
                                 &lv_font_montserrat_20, kColorText);
  makeLabel(positionCard, "Current roll", 260, 21,
            &lv_font_montserrat_14, kColorMuted);
  settingsRollLabel = makeLabel(positionCard, "--.-°", 260, 44,
                                &lv_font_montserrat_20, kColorText);

  lv_obj_t* levelButton = lv_button_create(positionCard);
  lv_obj_set_pos(levelButton, 20, 105);
  lv_obj_set_size(levelButton, 330, 68);
  lv_obj_set_style_radius(levelButton, 18, 0);
  lv_obj_set_style_bg_color(levelButton, lv_color_hex(kColorCyan), 0);
  lv_obj_set_style_bg_color(levelButton, lv_color_hex(kColorCyan),
                            LV_STATE_PRESSED);
  lv_obj_set_style_color_filter_opa(levelButton, LV_OPA_TRANSP,
                                    LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(levelButton, 0, 0);
  lv_obj_add_event_cb(levelButton, calibrateLevelClicked,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* levelButtonLabel = lv_label_create(levelButton);
  lv_label_set_text(levelButtonLabel, "Set current position as level");
  lv_obj_set_style_text_font(levelButtonLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(levelButtonLabel,
                              lv_color_hex(kColorControlText), 0);
  lv_obj_center(levelButtonLabel);
  calibrationStatusLabel = makeLabel(
      positionCard,
      "Park on a level surface, let the readings stabilize, then tap the "
      "button. Calibration is stored in controller flash.",
      380, 108, &lv_font_montserrat_14, kColorMuted);
  lv_obj_set_width(calibrationStatusLabel, 350);

  displaySettingsOverlay = createPageOverlay("Display");
  lv_obj_add_flag(displaySettingsOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(displaySettingsOverlay, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(displaySettingsOverlay, LV_SCROLLBAR_MODE_AUTO);
  auto dropdown = [](lv_obj_t* parent, const char* options, int x, int y, int width,
                     unsigned selected, lv_event_cb_t callback) {
    auto* control = lv_dropdown_create(parent);
    lv_dropdown_set_options(control, options); lv_dropdown_set_selected(control, selected);
    lv_obj_set_pos(control, x, y); lv_obj_set_size(control, width, 42);
    lv_obj_set_style_bg_color(control, lv_color_hex(kColorSurface), 0);
    lv_obj_set_style_text_color(control, lv_color_hex(kColorText), 0);
    lv_obj_set_style_text_font(control, &lv_font_montserrat_16, 0);
    if (callback) lv_obj_add_event_cb(control, callback, LV_EVENT_VALUE_CHANGED, nullptr);
    return control;
  };
  auto slider = [](lv_obj_t* parent, int x, int y, int width, int value, lv_event_cb_t callback) {
    auto* control = lv_slider_create(parent); lv_obj_set_pos(control, x, y);
    lv_obj_set_size(control, width, 12); lv_slider_set_range(control, 5, 100);
    lv_slider_set_value(control, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(control, lv_color_hex(kColorCyan), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(control, lv_color_hex(kColorCyan), LV_PART_KNOB);
    lv_obj_add_event_cb(control, callback, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(control, [](lv_event_t*) { saveDisplaySettings(); }, LV_EVENT_RELEASED, nullptr);
  };
  auto* displayCard = makeCard(displaySettingsOverlay, 16, 55, 376, 130);
  makeLabel(displayCard, LV_SYMBOL_EYE_OPEN "  Display", 16, 12, &lv_font_montserrat_20, kColorText);
  makeLabel(displayCard, "Brightness", 16, 53, &lv_font_montserrat_14, kColorMuted);
  displayBrightnessValue = makeLabel(displayCard, "", 303, 49, &lv_font_montserrat_16, kColorText);
  lv_label_set_text_fmt(displayBrightnessValue, "%u%%", displayBrightness);
  slider(displayCard, 117, 59, 165, displayBrightness, [](lv_event_t* event) {
    displayBrightness = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    lv_label_set_text_fmt(displayBrightnessValue, "%u%%", displayBrightness); applyDisplayBrightness();
  });
  makeLabel(displayCard, "Software dimming / backlight off in sleep", 16, 99, &lv_font_montserrat_14, kColorMuted);
  auto* sleepCard = makeCard(displaySettingsOverlay, 408, 55, 376, 130);
  makeLabel(sleepCard, LV_SYMBOL_POWER "  Sleep", 16, 12, &lv_font_montserrat_20, kColorText);
  makeLabel(sleepCard, "Screen off after", 16, 60, &lv_font_montserrat_14, kColorMuted);
  sleepTimeoutDropdown = dropdown(sleepCard, kSleepTimeoutOptions, 166, 47, 192,
      sleepTimeoutChoiceIndex(sleepTimeoutMinutes), sleepTimeoutChanged);
  makeLabel(sleepCard, "Touch to wake. Moon button sleeps now.", 16, 99, &lv_font_montserrat_14, kColorMuted);
  auto* nightCard = makeCard(displaySettingsOverlay, 16, 195, 768, 180);
  makeLabel(nightCard, LV_SYMBOL_BELL "  Overnight", 16, 12, &lv_font_montserrat_20, kColorText);
  auto* enabled = lv_switch_create(nightCard); lv_obj_set_pos(enabled, 692, 12);
  if (overnightEnabled) lv_obj_add_state(enabled, LV_STATE_CHECKED);
  lv_obj_add_event_cb(enabled, [](lv_event_t* event) {
    overnightEnabled = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_STATE_CHECKED);
    saveDisplaySettings(); applyDisplayBrightness();
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  String times;
  for (unsigned minute = 0; minute < 1440; minute += 30) {
    char text[8]; snprintf(text, sizeof(text), "%02u:%02u", minute / 60, minute % 60);
    if (minute) times += "\n"; times += text;
  }
  makeLabel(nightCard, "Screen off at", 16, 66, &lv_font_montserrat_16, kColorMuted);
  dropdown(nightCard, times.c_str(), 150, 54, 140, overnightOff / 30, [](lv_event_t* event) {
    overnightOff = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event))) * 30;
    saveDisplaySettings();
  });
  makeLabel(nightCard, "back on at", 326, 66, &lv_font_montserrat_16, kColorMuted);
  dropdown(nightCard, times.c_str(), 448, 54, 140, overnightOn / 30, [](lv_event_t* event) {
    overnightOn = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event))) * 30;
    saveDisplaySettings();
  });
  makeLabel(nightCard, "Touch wakes at", 16, 118, &lv_font_montserrat_16, kColorMuted);
  overnightBrightnessValue = makeLabel(nightCard, "", 366, 118, &lv_font_montserrat_16, kColorText);
  lv_label_set_text_fmt(overnightBrightnessValue, "%u%%", overnightBrightness);
  slider(nightCard, 177, 129, 160, overnightBrightness, [](lv_event_t* event) {
    overnightBrightness = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    lv_label_set_text_fmt(overnightBrightnessValue, "%u%%", overnightBrightness); applyDisplayBrightness();
  });
  makeLabel(nightCard, "for", 448, 118, &lv_font_montserrat_16, kColorMuted);
  dropdown(nightCard, "15 s\n30 s\n1 min\n2 min\n5 min", 498, 107, 144, overnightWakeChoice, [](lv_event_t* event) {
    overnightWakeChoice = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    saveDisplaySettings();
  });
  makeLabel(nightCard, "After last touch. Equal off/on times disable the overnight period.", 16, 156, &lv_font_montserrat_14, kColorMuted);
  auto* clockCard = makeCard(displaySettingsOverlay, 16, 385, 768, 185);
  clockStatusLabel = makeLabel(clockCard, "Clock not set", 16, 12, &lv_font_montserrat_20, kColorText);
  dropdown(clockCard, TouchClock::kZones, 508, 8, 238, TouchClock::zone(), [](lv_event_t* event) {
    TouchClock::setZone(lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event))));
  });
  makeLabel(clockCard, "Phone remote syncs date/time for daylight saving. Manual time uses no date.", 16, 61, &lv_font_montserrat_14, kColorMuted);
  makeLabel(clockCard, "Or set time", 16, 109, &lv_font_montserrat_16, kColorMuted);
  String hours, minutes;
  for (unsigned i = 0; i < 60; ++i) {
    char text[4]; snprintf(text, sizeof(text), "%02u", i);
    if (i) minutes += "\n"; minutes += text;
    if (i < 24) { if (i) hours += "\n"; hours += text; }
  }
  clockHourDropdown = dropdown(clockCard, hours.c_str(), 150, 97, 92, 12, nullptr);
  clockMinuteDropdown = dropdown(clockCard, minutes.c_str(), 253, 97, 92, 0, nullptr);
  auto* setTime = lv_button_create(clockCard); lv_obj_set_pos(setTime, 360, 97); lv_obj_set_size(setTime, 115, 42);
  auto* caption = lv_label_create(setTime); lv_label_set_text(caption, "Set time"); lv_obj_center(caption);
  lv_obj_add_event_cb(setTime, [](lv_event_t*) {
    TouchClock::setTime(lv_dropdown_get_selected(clockHourDropdown), lv_dropdown_get_selected(clockMinuteDropdown));
  }, LV_EVENT_CLICKED, nullptr);
  makeLabel(clockCard, "After restart, sync or set time again. Overnight waits until the clock is set.", 16, 154, &lv_font_montserrat_14, kColorMuted);

}

void createLabelConfigurationOverlays() {
  labelConfigOverlay = createPageOverlay("Device assignments");
  lv_obj_add_flag(labelConfigOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(labelConfigOverlay, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(labelConfigOverlay, LV_SCROLLBAR_MODE_ACTIVE);
  makeLabel(labelConfigOverlay,
            "Assign the name and icon shown for each generic PCB channel",
            18, 50, &lv_font_montserrat_14, kColorMuted);
  for (uint8_t index = 0; index < kDeviceLabelCount; ++index) {
    const int column = index % 2;
    const int row = index / 2;
    lv_obj_t* card = makeCard(labelConfigOverlay,
                              16 + column * 392, 77 + row * 78,
                              376, 68);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* iconCircle = makeIconCircle(
        card, 10, 10, 48, kDeviceIcons[deviceLabels[index].icon].symbol,
        &iconFont28, kColorCyan);
    labelConfigIconLabels[index] = lv_obj_get_child(iconCircle, 0);
    makeLabel(card, deviceLabels[index].channelName, 70, 9,
              &lv_font_montserrat_12, kColorMuted);
    labelConfigValueLabels[index] =
        makeLabel(card, deviceLabels[index].value, 70, 30,
                  &lv_font_montserrat_18, kColorText);
    lv_obj_set_width(labelConfigValueLabels[index], 265);
    makeLabel(card, LV_SYMBOL_RIGHT, 344, 24,
              &lv_font_montserrat_16, kColorMuted);
    lv_obj_add_event_cb(card, openDeviceLabelEditor, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
  }
  // Extend the scrollable content beyond the final card so the fifth row can
  // be positioned fully above the persistent bottom navigation bar.
  lv_obj_t* bottomClearance = lv_obj_create(labelConfigOverlay);
  lv_obj_remove_style_all(bottomClearance);
  lv_obj_set_pos(bottomClearance, 0, 469);
  lv_obj_set_size(bottomClearance, 1, 28);
  lv_obj_remove_flag(bottomClearance, LV_OBJ_FLAG_CLICKABLE);

  labelEditorOverlay = createPageOverlay("Edit device");
  labelEditorTitle = makeLabel(labelEditorOverlay, "Name", 22, 56,
                               &lv_font_montserrat_16, kColorMuted);
  labelEditorTextArea = lv_textarea_create(labelEditorOverlay);
  lv_obj_set_pos(labelEditorTextArea, 20, 82);
  lv_obj_set_size(labelEditorTextArea, 220, 54);
  lv_textarea_set_one_line(labelEditorTextArea, true);
  lv_textarea_set_max_length(labelEditorTextArea, kDeviceLabelLength - 1);
  lv_obj_set_style_bg_color(labelEditorTextArea,
                            lv_color_hex(kColorSurface), 0);
  lv_obj_set_style_border_color(labelEditorTextArea,
                                lv_color_hex(kColorBorder), 0);
  lv_obj_set_style_text_color(labelEditorTextArea,
                              lv_color_hex(kColorText), 0);
  lv_obj_set_style_text_font(labelEditorTextArea,
                             &lv_font_montserrat_18, 0);

  makeLabel(labelEditorOverlay, "Icon & colour", 254, 56,
            &lv_font_montserrat_14, kColorMuted);
  lv_obj_t* iconButton = lv_button_create(labelEditorOverlay);
  lv_obj_set_pos(iconButton, 254, 82);
  lv_obj_set_size(iconButton, 154, 54);
  lv_obj_set_style_radius(iconButton, 16, 0);
  lv_obj_set_style_bg_color(iconButton, lv_color_hex(kColorSurface), 0);
  lv_obj_set_style_border_width(iconButton, 1, 0);
  lv_obj_set_style_border_color(iconButton, lv_color_hex(kColorBorder), 0);
  lv_obj_set_style_shadow_width(iconButton, 0, 0);
  lv_obj_add_event_cb(iconButton, openIconPicker, LV_EVENT_CLICKED, nullptr);
  labelEditorIconLabel = lv_label_create(iconButton);
  setDeviceIcon(labelEditorIconLabel, kIconLight);
  lv_obj_set_style_text_color(labelEditorIconLabel,
                              lv_color_hex(kColorCyan), 0);
  lv_obj_center(labelEditorIconLabel);

  labelEditorOrderLabel = makeLabel(
      labelEditorOverlay, "Order", 430, 56,
      &lv_font_montserrat_14, kColorMuted);
  labelEditorOrderDropdown = lv_dropdown_create(labelEditorOverlay);
  lv_obj_set_pos(labelEditorOrderDropdown, 428, 82);
  lv_obj_set_size(labelEditorOrderDropdown, 182, 54);
  lv_dropdown_set_options(labelEditorOrderDropdown,
                          "Hidden\n1\n2\n3\n4\n5\n6\n7\n8");

  lv_obj_t* saveButton = lv_button_create(labelEditorOverlay);
  lv_obj_set_pos(saveButton, 628, 82);
  lv_obj_set_size(saveButton, 150, 54);
  lv_obj_set_style_radius(saveButton, 16, 0);
  lv_obj_set_style_bg_color(saveButton, lv_color_hex(kColorCyan), 0);
  lv_obj_set_style_shadow_width(saveButton, 0, 0);
  lv_obj_add_event_cb(saveButton, saveDeviceLabel, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* saveLabel = lv_label_create(saveButton);
  registerCheckButton(saveButton, saveLabel);
  lv_obj_set_style_text_font(saveLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(saveLabel, lv_color_hex(kColorControlText), 0);
  lv_obj_center(saveLabel);

  labelEditorKeyboard = lv_keyboard_create(labelEditorOverlay);
  // lv_keyboard defaults to bottom alignment. Reset that alignment explicitly;
  // otherwise set_pos() is interpreted as an offset from the bottom and most
  // of the keyboard extends outside this 416 px overlay and gets clipped.
  lv_obj_set_size(labelEditorKeyboard, 800, 258);
  lv_obj_align(labelEditorKeyboard, LV_ALIGN_TOP_LEFT, 0, 142);
  lv_keyboard_set_textarea(labelEditorKeyboard, labelEditorTextArea);
  lv_obj_add_event_cb(labelEditorKeyboard, saveDeviceLabel,
                      LV_EVENT_READY, nullptr);
  lv_obj_add_flag(labelEditorOverlay, LV_OBJ_FLAG_HIDDEN);

  iconPickerOverlay = createPageOverlay("Icon & colour");
  iconPickerPreview = makeLabel(iconPickerOverlay, "", 78, 140, &iconFont28, kColorCyan);
  lv_obj_set_style_transform_scale_x(iconPickerPreview, 512, 0);
  lv_obj_set_style_transform_scale_y(iconPickerPreview, 512, 0);
  lv_obj_t* confirm = lv_button_create(iconPickerOverlay);
  lv_obj_set_pos(confirm, 710, 8); lv_obj_set_size(confirm, 68, 44);
  lv_obj_set_style_bg_color(confirm, lv_color_hex(kColorCyan), 0);
  lv_obj_t* confirmLabel = lv_label_create(confirm);
  registerCheckButton(confirm, confirmLabel);
  lv_obj_set_style_text_font(confirmLabel, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(confirmLabel, lv_color_hex(kColorControlText), 0);
  lv_obj_center(confirmLabel);
  lv_obj_add_event_cb(confirm, saveIconSelection, LV_EVENT_CLICKED, nullptr);
  for (uint8_t i = 0; i < 12; ++i) {
    const uint8_t choice = i + 9;
    lv_obj_t* swatch = lv_button_create(iconPickerOverlay);
    iconColourSwatches[choice] = swatch;
    lv_obj_set_pos(swatch, 198 + (i % 6) * 72, 118 + (i / 6) * 62);
    // Compensate for the panel's non-square pixels to show physical circles.
    lv_obj_set_size(swatch, 44, 47);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(kIconColours[choice]), 0);
    lv_obj_set_style_border_width(swatch, 0, 0);
    lv_obj_set_style_outline_color(swatch, lv_color_hex(kIconColours[choice]), 0);
    lv_obj_set_style_outline_pad(swatch, 4, 0);
    lv_obj_set_style_shadow_width(swatch, 0, 0);
    lv_obj_add_event_cb(swatch, deviceIconColourChanged, LV_EVENT_CLICKED,
        reinterpret_cast<void*>(static_cast<uintptr_t>(choice)));
  }
  lv_obj_t* divider = lv_obj_create(iconPickerOverlay);
  lv_obj_remove_style_all(divider); lv_obj_set_pos(divider, 18, 242); lv_obj_set_size(divider, 764, 1);
  lv_obj_set_style_bg_color(divider, lv_color_hex(kColorBorder), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  for (uint8_t i = 0; i < kDeviceIconCount; ++i) {
    auto* button = lv_button_create(iconPickerOverlay);
    iconPickerButtons[i] = button;
    lv_obj_set_pos(button, 86 + (i % 6) * 108, 256 + (i / 6) * 70);
    lv_obj_set_size(button, 88, 58);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorControlCard), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    iconPickerSymbols[i] = lv_label_create(button);
    setDeviceIcon(iconPickerSymbols[i], i); lv_obj_center(iconPickerSymbols[i]);
    lv_obj_add_event_cb(button, selectDeviceIcon, LV_EVENT_CLICKED,
        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }
  lv_obj_add_flag(iconPickerOverlay, LV_OBJ_FLAG_HIDDEN);
}

void createSystemInfoOverlay() {
  systemInfoOverlay = createPageOverlay("System information");
  lv_obj_t* card = makeCard(systemInfoOverlay, 16, 65, 768, 315);
  lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  addSystemInfoRow(card, 0, "Touchscreen firmware",
                   AppConfig::kFirmwareVersion, true);
  systemRearFirmwareLabel =
      addSystemInfoRow(card, 50, "Controller firmware", "--", true);
  char protocol[24]{};
  snprintf(protocol, sizeof(protocol), "BlueSquid v%u",
           BlueSquidControl::kProtocolVersion);
  addSystemInfoRow(card, 100, "Control protocol", protocol, true);
  systemConnectionLabel =
      addSystemInfoRow(card, 200, "Controller connection", "Offline", true);
  systemUptimeLabel =
      addSystemInfoRow(card, 250, "Controller uptime", "--", false);

}

void syncButton(lv_obj_t* button, lv_obj_t* stateLabel, bool enabled,
                uint32_t accent) {
  if (button == allLightsButton) accent = deviceIconColour(kLabelAllRgbwLights, accent);
  for (uint8_t i = 0; i < 4; ++i)
    if (button == favoriteButtons[i] || button == controlButtons[i])
      accent = deviceIconColour(kLabelAccessory1 + i, accent);

  if (enabled) lv_obj_add_state(button, LV_STATE_CHECKED);
  else lv_obj_remove_state(button, LV_STATE_CHECKED);
  lv_obj_t* iconCircle = lv_obj_get_child(button, 0);
  lv_obj_t* iconLabel = lv_obj_get_child(iconCircle, 0);
  lv_obj_t* titleLabel = lv_obj_get_child(button, 1);
  lv_obj_set_style_bg_color(iconCircle,
      lv_color_hex(enabled ? accent : kColorIconCircle), 0);
  lv_obj_set_style_text_color(iconLabel,
      lv_color_hex(enabled ? kColorControlText : accent), 0);
  lv_obj_set_style_text_color(titleLabel,
      lv_color_hex(enabled ? kColorControlText : kColorText), 0);
  lv_label_set_text(stateLabel, enabled ? "On" : "Off");
  lv_obj_set_style_text_color(stateLabel,
      lv_color_hex(enabled ? kColorControlMuted : kColorMuted), 0);
}

void setActionAvailable(lv_obj_t* button, bool available) {
  if (button == nullptr) return;
  if (available)
    lv_obj_remove_state(button, LV_STATE_DISABLED);
  else
    lv_obj_add_state(button, LV_STATE_DISABLED);
  lv_obj_set_style_opa(button, available ? LV_OPA_COVER : LV_OPA_40, 0);
}

void requestFanSpeed(uint8_t speed) {
  const auto& remote = transportClient.status();
  if (!transportClient.connected() || !(remote.fanFlags & 2)) return;
  // FA75 has ten speeds; zero is Off.
  speed = speed ? min(100, max(10, ((speed + 5) / 10) * 10)) : 0;
  if (transportClient.send(BlueSquidControl::Command::SetFan, 0, speed)) {
    if (speed) lastFanSpeed = speed;
    fanRequested = speed; fanCommandMs = millis(); fanCommandWaiting = true;
    lv_label_set_text(fanStateLabel, "Applying...");
  } else lv_label_set_text(fanStateLabel, "Command not sent");
}
void fanToggled(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  requestFanSpeed(lv_obj_has_state(fanButton, LV_STATE_CHECKED) ? lastFanSpeed : 0);
}
void fanSpeedChanged(lv_event_t* event) {
  const uint8_t speed = lv_slider_get_value(fanSlider) * 10;
  lv_label_set_text_fmt(fanSpeedLabel, "%u%%", speed);
  if (lv_event_get_code(event) == LV_EVENT_RELEASED) requestFanSpeed(speed);
}
void fanDirectionChanged(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  const bool intake = lv_obj_has_state(fanReverseSwitch, LV_STATE_CHECKED);
  if (transportClient.send(BlueSquidControl::Command::SetFanReverse, 0, intake ? 1 : 0)) {
    fanDirectionWaiting = true; fanRequestedIntake = intake; fanDirectionMs = millis();
  } else lv_label_set_text(fanStateLabel, "Command not sent");
}
void createFanCard(lv_obj_t* parent) {
  fanButton = lv_button_create(parent);
  lv_obj_set_pos(fanButton, 16, 278); lv_obj_set_size(fanButton, 768, 122);
  lv_obj_add_flag(fanButton, LV_OBJ_FLAG_CHECKABLE); styleControlCard(fanButton);
  lv_obj_set_style_pad_all(fanButton, 0, 0);
  makeIconCircle(fanButton, 14, 15, 46, BLUESQUID_SYMBOL_FAN, &iconFont28, kColorLightbulb);
  makeLabel(fanButton, "Vent fan", 72, 17, &lv_font_montserrat_16, kColorText);
  fanStateLabel = makeLabel(fanButton, "RV-C disabled", 72, 43, &lv_font_montserrat_14, kColorMuted);
  fanSpeedLabel = makeLabel(fanButton, "0%", 684, 23, &lv_font_montserrat_18, kColorText);
  makeLabel(fanButton, "Reverse air", 388, 15, &lv_font_montserrat_14, kColorText);
  fanDirectionLabel = makeLabel(fanButton, "Exhaust", 388, 40, &lv_font_montserrat_14, kColorMuted);
  fanReverseSwitch = lv_switch_create(fanButton);
  lv_obj_set_pos(fanReverseSwitch, 515, 22); lv_obj_set_size(fanReverseSwitch, 52, 28);
  lv_obj_set_style_bg_color(fanReverseSwitch, lv_color_hex(kColorLightbulb), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_remove_flag(fanReverseSwitch, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(fanReverseSwitch, fanDirectionChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  fanSlider = lv_slider_create(fanButton);
  lv_obj_set_pos(fanSlider, 24, 94); lv_obj_set_size(fanSlider, 720, 12);
  lv_slider_set_range(fanSlider, 0, 10);
  lv_obj_set_style_bg_color(fanSlider, lv_color_hex(kColorLightbulb), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(fanSlider, lv_color_hex(kColorLightbulb), LV_PART_KNOB);
  lv_obj_remove_flag(fanSlider, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(fanButton, fanToggled, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(fanSlider, fanSpeedChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(fanSlider, fanSpeedChanged, LV_EVENT_RELEASED, nullptr);
}
void refreshFanCard(bool online) {
  const auto& s = transportClient.status();
  const bool available = online && (s.fanFlags & 2);
  if (s.fanPreset) lastFanSpeed = min(100, max(10, ((s.fanPreset + 5) / 10) * 10));
  if (fanCommandWaiting && (!available || millis() - fanCommandMs > 7500 ||
      (s.fan == fanRequested && !(s.fanFlags & 8)))) fanCommandWaiting = false;
  const bool directionAvailable = available && (s.fanFlags & 32);
  if (fanDirectionWaiting && (!directionAvailable || millis() - fanDirectionMs > 7500 ||
      (!(s.fanFlags & 8) && bool(s.fanFlags & 16) == fanRequestedIntake))) fanDirectionWaiting = false;
  const bool intake = fanDirectionWaiting ? fanRequestedIntake : bool(s.fanFlags & 16);
  if (intake) lv_obj_add_state(fanReverseSwitch, LV_STATE_CHECKED);
  else lv_obj_remove_state(fanReverseSwitch, LV_STATE_CHECKED);
  setActionAvailable(fanReverseSwitch, directionAvailable);
  lv_label_set_text(fanDirectionLabel, !directionAvailable ? "Unavailable" : intake ? "Intake" : "Exhaust");
  const bool waiting = fanCommandWaiting || fanDirectionWaiting || (s.fanFlags & 8);
  const uint8_t level = fanCommandWaiting ? fanRequested : s.fan;
  syncButton(fanButton, fanStateLabel, available && (fanCommandWaiting ? level != 0 : s.fanFlags & 4), kColorLightbulb);
  const char* text = !online ? "Controller offline" : !(s.fanFlags & 1) ? "RV-C disabled" :
      s.fanError == 1 ? "CAN connection fault" : s.fanError == 2 ? "CAN address conflict" :
      !available ? "Waiting for fan" : waiting ? "Applying..." : s.fanError == 3 ? "Command not confirmed" : "";
  lv_label_set_text(fanStateLabel, text);
  setActionAvailable(fanButton, available); setActionAvailable(fanSlider, available);
  if (!lv_obj_has_state(fanSlider, LV_STATE_PRESSED)) {
    lv_slider_set_value(fanSlider, (level + 5) / 10, LV_ANIM_OFF);
    lv_label_set_text_fmt(fanSpeedLabel, "%u%%", level);
  }
  if (rvcSaveButton) {
    setActionAvailable(rvcSaveButton, online && rvcConfigLoaded && !rvcSaving);
    if (rvcSaving && millis() - rvcSaveMs > 15000) {
      rvcSaving = false; rvcConfigLoaded = false;
      lv_label_set_text(rvcStatusLabel, "Reopen RV-C settings to verify the saved configuration.");
    }
  }
}

void buildUi() {
  styleScreen();
  tabFont = lv_font_montserrat_14;
  tabIconFont = lv_font_montserrat_20;
  tabIconFont.fallback = &bluesquid_font_lightbulb_14;
  metricIconFont24 = lv_font_montserrat_24;
  metricIconFont24.fallback = &bluesquid_font_sun_24;
  iconFont28 = lv_font_montserrat_28;
  iconFont28.fallback = &bluesquid_font_lightbulb_28;
  climateFont16 = lv_font_montserrat_16;
  climateFont16.fallback = &bluesquid_font_climate_24;
  climateFont16.line_height = 24;
  climateFont16.base_line = 3;
  lv_obj_t* tabs = lv_tabview_create(lv_screen_active());
  lv_tabview_set_tab_bar_position(tabs, LV_DIR_BOTTOM);
  lv_tabview_set_tab_bar_size(tabs, 64);
  tabView = tabs;
  lv_obj_set_size(tabs, 800, 480);
  lv_obj_set_style_bg_color(tabs, lv_color_hex(kColorBackground), 0);
  lv_obj_set_style_border_width(tabs, 0, 0);

  // Slider drags are horizontal, just like the tab view's swipe gesture. Keep
  // page changes on the navigation bar so a lighting adjustment cannot
  // accidentally switch tabs.
  lv_obj_remove_flag(lv_tabview_get_content(tabs), LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* tabButtons = lv_tabview_get_tab_bar(tabs);
  lv_obj_set_style_bg_color(tabButtons, lv_color_hex(kColorSurface), 0);
  lv_obj_set_style_bg_opa(tabButtons, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tabButtons, 1, 0);
  lv_obj_set_style_border_side(tabButtons, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(tabButtons, lv_color_hex(kColorBorder), 0);

  lv_obj_t* home = lv_tabview_add_tab(tabs, "Home");
  lv_obj_t* lights = lv_tabview_add_tab(tabs, "Light");
  lv_obj_t* controls = lv_tabview_add_tab(tabs, "Control");
  lv_obj_t* power = lv_tabview_add_tab(tabs, "Power");
  lv_obj_t* menu = lv_tabview_add_tab(tabs, "Menu");
  styleTabButtons(tabs);
  syncTabButtonLabels(tabs);
  lv_obj_t* pages[] = {home, lights, controls, power, menu};
  for (lv_obj_t* page : pages) stylePage(page);

  addHeader(home, "BlueSquid", 0);
  makeLabel(home, "Quick access", 18, 55, &lv_font_montserrat_16, kColorText);
  allLightsButton = addToggle(
      home, kDeviceIcons[deviceLabels[kLabelAllRgbwLights].icon].symbol,
      deviceLabels[kLabelAllRgbwLights].value,
      16, 225, 142, 158, BlueSquidControl::Command::SetAllLights,
      kColorLightbulb, &allLightsStateLabel, &allLightsTitleLabel,
      &allLightsIconLabel);
  homeInverterButton = addToggle(
      home, LV_SYMBOL_POWER, "Inverter", 168, 225, 142, 158,
      BlueSquidControl::Command::SetInverter, kColorGreen,
      &homeInverterStateLabel);
  favoriteButtons[0] = addToggle(
      home, kDeviceIcons[deviceLabels[kLabelAccessory1].icon].symbol,
      deviceLabels[kLabelAccessory1].value, 168, 225, 142, 158,
      BlueSquidControl::Command::SetUsb, kColorCyan, &favoriteStateLabels[0],
      &favoriteTitleLabels[0], &favoriteIconLabels[0]);
  favoriteButtons[1] = addToggle(
      home, kDeviceIcons[deviceLabels[kLabelAccessory2].icon].symbol,
      deviceLabels[kLabelAccessory2].value, 320, 225, 142, 158,
      BlueSquidControl::Command::SetPump, kColorCyan, &favoriteStateLabels[1],
      &favoriteTitleLabels[1], &favoriteIconLabels[1]);
  favoriteButtons[2] = addToggle(
      home, kDeviceIcons[deviceLabels[kLabelAccessory3].icon].symbol,
      deviceLabels[kLabelAccessory3].value, 472, 225, 142, 158,
      BlueSquidControl::Command::SetAccessory3, kColorAmber, &favoriteStateLabels[2],
      &favoriteTitleLabels[2], &favoriteIconLabels[2]);
  favoriteButtons[3] = addToggle(
      home, kDeviceIcons[deviceLabels[kLabelAccessory4].icon].symbol,
      deviceLabels[kLabelAccessory4].value, 624, 225, 160, 158,
      BlueSquidControl::Command::SetAccessory4, kColorAmber, &favoriteStateLabels[3],
      &favoriteTitleLabels[3], &favoriteIconLabels[3]);

  addHeader(power, "Power", 3);
  makeLabel(power, "Dashboard", 18, 55, &lv_font_montserrat_16, kColorText);
  addHomePowerSummary(power);
  batteryLabel = addDetailCard(power, 16, 196, 376, 187,
                               LV_SYMBOL_BATTERY_3,
                               "Battery details", kColorGreen);
  powerLabel = nullptr;
  remainingLabel = addDetailCard(power, 408, 196, 376, 187, LV_SYMBOL_DRIVE,
                                 "Remaining", kColorAmber);

  addHeader(lights, "Light", 1);

  addLightZone(lights, deviceLabels[kLabelRgbwLight1].value, 28, 0);
  addLightZone(lights, deviceLabels[kLabelRgbwLight2].value, 412, 1);
  addLightZone(lights, deviceLabels[kLabelRgbwLight3].value, 28, 2);
  addLightZone(lights, deviceLabels[kLabelRgbwLight4].value, 412, 3);

  addHeader(controls, "Control", 2);
  createFanCard(controls);
  inverterButton = addToggle(
      controls, LV_SYMBOL_POWER, "Inverter", 16, 61, 374, 76,
      BlueSquidControl::Command::SetInverter, kColorGreen, &inverterStateLabel);
  chargerButton = addToggle(
      controls, LV_SYMBOL_CHARGE, "Shore charger", 400, 61, 384, 76,
      BlueSquidControl::Command::SetCharger, kColorCyan, &chargerStateLabel);
  controlButtons[0] = addToggle(
      controls, kDeviceIcons[deviceLabels[kLabelAccessory1].icon].symbol,
      deviceLabels[kLabelAccessory1].value, 16, 147, 180, 200,
      BlueSquidControl::Command::SetUsb, kColorCyan, &controlStateLabels[0],
      &controlTitleLabels[0], &controlIconLabels[0]);
  controlButtons[1] = addToggle(
      controls, kDeviceIcons[deviceLabels[kLabelAccessory2].icon].symbol,
      deviceLabels[kLabelAccessory2].value, 206, 147, 180, 200,
      BlueSquidControl::Command::SetPump, kColorCyan, &controlStateLabels[1],
      &controlTitleLabels[1], &controlIconLabels[1]);
  controlButtons[2] = addToggle(
      controls, kDeviceIcons[deviceLabels[kLabelAccessory3].icon].symbol,
      deviceLabels[kLabelAccessory3].value, 396, 147, 180, 200,
      BlueSquidControl::Command::SetAccessory3, kColorAmber, &controlStateLabels[2],
      &controlTitleLabels[2], &controlIconLabels[2]);
  controlButtons[3] = addToggle(
      controls, kDeviceIcons[deviceLabels[kLabelAccessory4].icon].symbol,
      deviceLabels[kLabelAccessory4].value, 586, 147, 198, 200,
      BlueSquidControl::Command::SetAccessory4, kColorAmber, &controlStateLabels[3],
      &controlTitleLabels[3], &controlIconLabels[3]);

  applyDeviceDisplayLayout();

  addHeader(menu, "Menu", 4);
  lv_obj_t* menuPanel = makeCard(menu, 16, 61, 768, 343);
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(menuPanel, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(menuPanel, LV_SCROLLBAR_MODE_ON);
  lv_obj_set_style_width(menuPanel, 5, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(menuPanel, lv_color_hex(kColorMuted),
                            LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(menuPanel, LV_OPA_60, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(menuPanel, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(menuPanel, 12, 0);
  addMenuRow(menuPanel, 0, LV_SYMBOL_REFRESH, "History", "Energy history and trends", kColorCyan, true);
  addMenuRow(menuPanel, 67, LV_SYMBOL_BELL, "Notifications", "Warnings and system events", kColorAmber, true);
  addMenuRow(menuPanel, 134, LV_SYMBOL_SETTINGS, "System Configuration", "Device assignments, backup and restore", kColorGreen, true);
  addMenuRow(menuPanel, 201, LV_SYMBOL_REFRESH, "Camper Position", "Pitch, roll and level calibration", kColorCyan, true);
  addMenuRow(menuPanel, 268, LV_SYMBOL_EYE_OPEN, "Display", "Brightness, sleep and overnight schedule", kColorAmber, true);
  addMenuRow(menuPanel, 335, LV_SYMBOL_FILE, "System information", "Firmware and link diagnostics", kColorMuted, false);
  addMenuHitTarget(menuPanel, 134, openSettings);
  addMenuHitTarget(menuPanel, 201, openCamperPosition);
  addMenuHitTarget(menuPanel, 268, openDisplaySettings);
  addMenuHitTarget(menuPanel, 335, openSystemInfo);

  createColorDialog();
  createSettingsOverlay();
  createBluetoothControllersOverlay();
  createCerboWifiOverlay();
  createHotspotOverlay();
  createRvcFanOverlay();
  createLabelConfigurationOverlays();
  createSystemInfoOverlay();
  matchKeyboardCheckButtons();
  lv_obj_add_event_cb(tabs, tabNavigationChanged,
                      LV_EVENT_VALUE_CHANGED, nullptr);
}

void refreshUi() {
  if (millis() - lastUiUpdateMs < 250) return;
  lastUiUpdateMs = millis();
  const auto& status = transportClient.status();
  if (!lvgl_port_lock(50)) return;
  bool keyboardVisible=false;
  syncKeyboardNavigation(lv_screen_active(),keyboardVisible);
  lv_obj_t* bar=lv_tabview_get_tab_bar(tabView);
  if (keyboardVisible) lv_obj_add_flag(bar,LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(bar,LV_OBJ_FLAG_HIDDEN);
  processSp630ePayload();
  const bool online = transportClient.connected();
  const bool cerboConnected = online && status.energyValid;
  for (lv_obj_t* icon : victronConnectionIcons)
    setVictronConnectionIcon(icon, online, cerboConnected);
  if (connectionStatusOverlay != nullptr &&
      !lv_obj_has_flag(connectionStatusOverlay, LV_OBJ_FLAG_HIDDEN))
    refreshConnectionStatusCard();
  const bool inverterControlsAvailable =
      cerboConnected && status.inverterValid;
  setActionAvailable(homeInverterButton, inverterControlsAvailable);
  setActionAvailable(inverterButton, inverterControlsAvailable);
  setActionAvailable(chargerButton, inverterControlsAvailable);
  if (inverterButton != nullptr) {
    if (!inverterControlsAvailable) pendingInverter = pendingCharger = {};
    const bool inverterOn = displayState(pendingInverter, status.inverterValid &&
                            (status.inverterMode == 2 ||
                             status.inverterMode == 3));
    const bool chargerOn = displayState(pendingCharger, status.inverterValid &&
                           (status.inverterMode == 1 ||
                            status.inverterMode == 3));
    syncButton(inverterButton, inverterStateLabel, inverterOn, kColorGreen);
    syncButton(homeInverterButton, homeInverterStateLabel, inverterOn,
               kColorGreen);
    syncButton(chargerButton, chargerStateLabel, chargerOn, kColorCyan);
    if (!inverterControlsAvailable) {
      lv_label_set_text(inverterStateLabel, "Off Line");
      lv_label_set_text(homeInverterStateLabel, "Off Line");
      lv_label_set_text(chargerStateLabel, "Off Line");
    }
  }
  lv_label_set_text_fmt(homeBatteryLabel, "%.0f%%\n%.2f V",
                        status.soc, status.voltage);
  lv_label_set_text_fmt(homeSolarLabel, "%u W\n%s",
                        status.solarPower, chargerState(status.solarState));
  lv_label_set_text_fmt(homeDcDcLabel, "%u W\n%s",
                        status.dcDcPower, chargerState(status.dcDcState));
  if (online && status.energyValid && status.shoreValid)
    lv_label_set_text_fmt(shorePowerLabel, "%u W\n%s", status.shorePower,
                          chargerState(status.shoreState));
  else
    lv_label_set_text(shorePowerLabel, "0 W\n--");
  lv_label_set_text_fmt(homeLoadLabel, "%u W\n%+d W net",
                        status.loadPower, status.batteryPower);
  const auto clock = TouchClock::read();
  if (clockStatusLabel) lv_label_set_text_fmt(clockStatusLabel, clock.valid ? "Local time %s" : "Clock not set (%s)", clock.text);
  for (lv_obj_t* label : headerClimateLabels) {
    lv_label_set_text_fmt(label,
                          "P %.1f°  R %.1f°  |  "
                          BLUESQUID_SYMBOL_THERMOMETER " %.1f°C  |  "
                          BLUESQUID_SYMBOL_HUMIDITY " %.0f%%  |  %s",
                          status.pitchDegrees, status.rollDegrees,
                          status.cabinTemperatureC, status.humidity, clock.text);
  }
  lv_label_set_text_fmt(settingsPitchLabel, "%.1f°", status.pitchDegrees);
  lv_label_set_text_fmt(settingsRollLabel, "%.1f°", status.rollDegrees);
  lv_label_set_text(systemConnectionLabel, online ? "Connected" : "Offline");
  lv_obj_set_style_text_color(systemConnectionLabel,
      lv_color_hex(online ? kColorGreen : kColorMuted), 0);
  if (status.systemInfoValid) {
    lv_label_set_text_fmt(systemRearFirmwareLabel, "%u.%u.%u",
                          status.rearFirmwareMajor,
                          status.rearFirmwareMinor,
                          status.rearFirmwarePatch);
  } else {
    lv_label_set_text(systemRearFirmwareLabel, "Not reported");
  }
  const uint32_t uptimeDays = status.rearUptimeSeconds / 86400UL;
  const uint32_t uptimeHours = (status.rearUptimeSeconds / 3600UL) % 24UL;
  const uint32_t uptimeMinutes = (status.rearUptimeSeconds / 60UL) % 60UL;
  lv_label_set_text_fmt(systemUptimeLabel, "%lud %02luh %02lum",
                        static_cast<unsigned long>(uptimeDays),
                        static_cast<unsigned long>(uptimeHours),
                        static_cast<unsigned long>(uptimeMinutes));
  if (calibrationRequestedMs != 0) {
    const uint32_t elapsed = millis() - calibrationRequestedMs;
    const bool levelIsZero = status.pitchDegrees > -0.2F &&
        status.pitchDegrees < 0.2F && status.rollDegrees > -0.2F &&
        status.rollDegrees < 0.2F;
    if (elapsed >= 500 && levelIsZero) {
      lv_label_set_text(calibrationStatusLabel,
                        "Calibration saved on the controller.");
      lv_obj_set_style_text_color(calibrationStatusLabel,
                                  lv_color_hex(kColorGreen), 0);
      calibrationRequestedMs = 0;
    } else if (elapsed >= 3500) {
      lv_label_set_text(calibrationStatusLabel,
                        "Calibration was not confirmed. Check the "
                        "controller and level sensor.");
      lv_obj_set_style_text_color(calibrationStatusLabel,
                                  lv_color_hex(kColorRed), 0);
      calibrationRequestedMs = 0;
    }
  }
  if (batteryLabel != nullptr)
    lv_label_set_text_fmt(batteryLabel, "%+.1f A\n%+d W battery",
                          status.current, status.batteryPower);
  if (remainingLabel != nullptr)
    lv_label_set_text_fmt(remainingLabel, "%.1f Ah\n%uh %02um",
                          status.remainingAh, status.timeToGoMinutes / 60,
                          status.timeToGoMinutes % 60);
  for (uint8_t zone = 0; zone < 4; ++zone) {
    const uint8_t* rgbw = status.rgbwChannels(zone);
    const uint8_t colorLevel =
        max(rgbw[0], max(rgbw[1], rgbw[2]));
    const bool colorOn = colorLevel != 0;
    const bool whiteOn = rgbw[3] != 0;
    const bool outputOn = colorOn || whiteOn;
    const uint8_t remoteBrightness = max(colorLevel, rgbw[3]);
    const bool colorWasPending = pendingColorEnabled[zone].waiting;
    const bool whiteWasPending = pendingWhiteEnabled[zone].waiting;
    const bool brightnessWasPending = pendingBrightness[zone].waiting;
    const bool gesturePending = lastColourGestureMs[zone] != 0 &&
        millis() - lastColourGestureMs[zone] < 750;
    const bool toneWasPending = pendingWhiteTone[zone].waiting;
    displayLevel(pendingWhiteTone[zone], status.rgbwOptions[zone] & 6);
    const bool presetCommandPending = colorWasPending || whiteWasPending || toneWasPending ||
                                      brightnessWasPending || gesturePending;
    if (status.rgbwPresetValid[zone] && !presetCommandPending) {
      // An off RGB output can report 0,0,0. That is output state, not a
      // replacement for the colour to restore when RGB is enabled again.
      if (status.rgb[zone][0] || status.rgb[zone][1] || status.rgb[zone][2])
        memcpy(selectedRgb[zone], status.rgb[zone], sizeof(selectedRgb[zone]));
      desiredBrightness[zone] = status.rgbwBrightness[zone];
      if (desiredBrightness[zone]) lastZoneBrightness[zone] = desiredBrightness[zone];
      // Coexistence capability is not proof that a channel is on.
      desiredColorEnabled[zone] = colorOn;
      desiredWhiteEnabled[zone] = whiteOn;
      if (status.rgbwOptions[zone] & 6) desiredWhiteTone[zone] = status.rgbwOptions[zone] & 6;
      if (outputOn) rememberedZoneOptions[zone] = (colorOn ? 1 : 0) | (whiteOn ? desiredWhiteTone[zone] : 0);
      lv_obj_set_style_bg_color(zoneColorCenters[zone], zoneColor(zone), 0);
    }
    const bool displayedColorOn =
        displayState(pendingColorEnabled[zone], colorOn);
    const bool displayedWhiteOn =
        displayState(pendingWhiteEnabled[zone], whiteOn);
    const uint8_t displayedBrightness =
        displayLevel(pendingBrightness[zone], remoteBrightness);
    if (!presetCommandPending && (outputOn ||
        (colorWasPending && desiredBrightness[zone] != 0))) {
      desiredColorEnabled[zone] = displayedColorOn;
    }
    if (!presetCommandPending && (outputOn ||
        (whiteWasPending && desiredBrightness[zone] != 0))) {
      desiredWhiteEnabled[zone] = displayedWhiteOn;
    }
    if (!presetCommandPending && (outputOn || brightnessWasPending)) {
      desiredBrightness[zone] = displayedBrightness;
    }
    if (colorOn && !presetCommandPending) {
      const uint8_t normalized[3] = {
          static_cast<uint8_t>((rgbw[0] * 100 + colorLevel / 2) /
                               colorLevel),
          static_cast<uint8_t>((rgbw[1] * 100 + colorLevel / 2) /
                               colorLevel),
          static_cast<uint8_t>((rgbw[2] * 100 + colorLevel / 2) /
                               colorLevel),
      };
      const bool changed = selectedRgb[zone][0] != normalized[0] ||
          selectedRgb[zone][1] != normalized[1] ||
          selectedRgb[zone][2] != normalized[2];
      selectedRgb[zone][0] = normalized[0];
      selectedRgb[zone][1] = normalized[1];
      selectedRgb[zone][2] = normalized[2];
      if (changed) {
        lv_obj_set_style_bg_color(zoneColorCenters[zone], zoneColor(zone), 0);
        if (!lv_obj_has_flag(colorDialogOverlay, LV_OBJ_FLAG_HIDDEN) &&
            activeColorZone == zone) {
          positionHueMarkerForZone(colorDialogMarker, colorDialogWheel,
                                   zone, 24);
        }
      }
    }
    syncZoneButton(zone, displayState(pendingZones[zone], outputOn));
    if (!lv_obj_has_state(zoneBrightnessControls[zone], LV_STATE_PRESSED))
      setZoneBrightnessDisplay(zone, zoneOutputEnabled[zone] ? desiredBrightness[zone] : 0);

    if (!lv_obj_has_flag(colorDialogOverlay, LV_OBJ_FLAG_HIDDEN) &&
        activeColorZone == zone) {
      if (desiredColorEnabled[zone]) {
        lv_obj_add_state(colorDialogEnableSwitch, LV_STATE_CHECKED);
      } else {
        lv_obj_remove_state(colorDialogEnableSwitch, LV_STATE_CHECKED);
      }
      syncWhiteSelections(zone);
    }
  }
  refreshFanCard(online);
  const bool outputStates[] = {status.usb, status.pump, status.accessory3,
                               status.accessory4};
  const uint32_t accents[] = {kColorCyan, kColorCyan, kColorAmber, kColorAmber};
  for (int i = 0; i < 4; ++i) {
    const bool displayed = displayState(pendingOutputs[i], outputStates[i]);
    syncButton(favoriteButtons[i], favoriteStateLabels[i], displayed, accents[i]);
    syncButton(controlButtons[i], controlStateLabels[i], displayed, accents[i]);
  }
  syncMainLightButtons(
      displayState(pendingAllLights,
                   status.anyLightsEnabled(savedLightGroup)));

  // Every local output depends on the controller. Apply availability
  // after normal state synchronization so stale snapshots cannot leave an
  // enabled-looking control or an "On" label behind when BLE is offline.
  // Group commands also reach assigned lights when another assignment is
  // unavailable; the Controller connection is the only group prerequisite.
  setActionAvailable(allLightsButton, online && savedLightGroup != 0);
  if (!savedLightGroup) lv_label_set_text(allLightsStateLabel, "No lights selected");
  for (uint8_t zone = 0; zone < 4; ++zone) {
    const bool available = online && status.outputAvailable(zone);
    setActionAvailable(zoneButtons[zone], available);
    setActionAvailable(zoneBrightnessControls[zone], available);
    if (!available) {
      syncZoneButton(zone, false);
      lv_label_set_text(zoneStateLabels[zone], online ? "Unavailable" : "Off Line");
    }
  }
  for (uint8_t output = 0; output < 4; ++output) {
    const bool available = online && status.outputAvailable(output + 4);
    setActionAvailable(favoriteButtons[output], available);
    setActionAvailable(controlButtons[output], available);
    if (!available) {
      syncButton(favoriteButtons[output], favoriteStateLabels[output], false, accents[output]);
      syncButton(controlButtons[output], controlStateLabels[output], false, accents[output]);
      lv_label_set_text(favoriteStateLabels[output], "Unavailable");
      lv_label_set_text(controlStateLabels[output], "Unavailable");
    }
  }
  const bool colorAvailable = online && fullLightType(activeColorZone) &&
                              status.outputAvailable(activeColorZone);
  setActionAvailable(colorDialogWheel, colorAvailable);
  setActionAvailable(colorDialogEnableSwitch, colorAvailable);
  setActionAvailable(colorDialogWhiteEnableSwitch, colorAvailable);
  setActionAvailable(colorDialogCoolWhiteSwitch, colorAvailable);
  lv_label_set_text(colorDialogAvailabilityLabel,
                   colorAvailable ? "" : online ? "Unavailable" : "Off Line");

  if (!online) {
    lv_label_set_text(allLightsStateLabel, "Off Line");
    for (uint8_t zone = 0; zone < 4; ++zone) {
      lv_label_set_text(zoneStateLabels[zone], "Off Line");
    }
    for (uint8_t output = 0; output < 4; ++output) {
      lv_label_set_text(favoriteStateLabels[output], "Off Line");
      lv_label_set_text(controlStateLabels[output], "Off Line");
    }
  }
  lvgl_port_unlock();
}
}  // namespace

String remoteStatus() {
  JsonDocument doc;
  const auto status=transportClient.status();
  doc["online"]=transportClient.connected();
  doc["energyValid"]=status.energyValid;
  doc["soc"]=status.soc; doc["voltage"]=status.voltage;
  doc["solar"]=status.solarPower; doc["dcdc"]=status.dcDcPower;
  doc["ac"]=status.shorePower;
  const auto add=[&](JsonObject item,const char* label,bool on,bool available,uint8_t level) {
    item["label"]=label; item["on"]=on; item["available"]=available; item["level"]=level;
  };
  if (!lvgl_port_lock(50)) return "{}";
  for(uint8_t i=0;i<4;++i) {
    const uint8_t* channels=status.rgbwChannels(i);
    const bool colour=channels[0]||channels[1]||channels[2];
    JsonObject item=doc["rgb"][i].to<JsonObject>();
    add(item,deviceLabels[kLabelRgbwLight1+i].value,displayState(pendingZones[i], colour||channels[3]),status.outputAvailable(i),displayLevel(pendingBrightness[i], status.rgbwBrightness[i]));
    item["full"]=fullLightType(i);
    item["rgbOnly"]=rgbOnlyLightType(i);
    item["colour"]=displayState(pendingColorEnabled[i], colour);
    const bool white = displayState(pendingWhiteEnabled[i], channels[3] > 0);
    const uint8_t tone = displayLevel(pendingWhiteTone[i], status.rgbwOptions[i] & 6);
    item["white"]=white && (tone & 2);
    item["coolWhite"]=white && (tone & 4);
  }
  const bool states[]={status.usb,status.pump,status.accessory3,status.accessory4};
  for(uint8_t i=0;i<4;++i)
    add(doc["outputs"][i].to<JsonObject>(),deviceLabels[kLabelAccessory1+i].value,displayState(pendingOutputs[i], states[i]),status.outputAvailable(i+4),0);
  add(doc["inverter"].to<JsonObject>(),"Inverter",displayState(pendingInverter, status.inverterMode==2||status.inverterMode==3),status.inverterValid,0);
  add(doc["charger"].to<JsonObject>(),"Shore charger",displayState(pendingCharger, status.inverterMode==1||status.inverterMode==3),status.inverterValid,0);
  lvgl_port_unlock();
  String json;serializeJson(doc,json);return json;
}

bool remoteCommand(const String& kind,uint8_t target,uint32_t value) {
  if (!transportClient.connected() || !lvgl_port_lock(50)) return false;
  const auto perform=[&]() -> bool {
    using Command=BlueSquidControl::Command;
    const auto& status=transportClient.status();
    if (kind.startsWith("rgb") && target<4 && status.outputAvailable(target)) {
      if (kind=="rgb" && value<=1) {
        const uint8_t options=value?rememberedZoneOptions[target]:0;
        desiredColorEnabled[target]=options&1;desiredWhiteEnabled[target]=options&6;
        desiredWhiteTone[target]=options&6;
      } else if(kind=="rgbColour" && value<=1) desiredColorEnabled[target]=value;
      else if((kind=="rgbWhite" || kind=="rgbCoolWhite") && value<=1) {
        if (rgbOnlyLightType(target)) return false;
        const uint8_t bit=kind=="rgbWhite" ? 2 : 4;
        desiredWhiteTone[target]=value ? desiredWhiteTone[target]|bit : desiredWhiteTone[target]&~bit;
        desiredWhiteEnabled[target]=desiredWhiteTone[target]!=0;
      }
      else if(kind=="rgbLevel" && value<=100) {
        desiredBrightness[target]=value;
        if(!value)desiredColorEnabled[target]=desiredWhiteEnabled[target]=false;
        else if(!desiredColorEnabled[target]&&!desiredWhiteEnabled[target]) {
          desiredColorEnabled[target]=rememberedZoneOptions[target]&1;
          desiredWhiteEnabled[target]=rememberedZoneOptions[target]&6;
          desiredWhiteTone[target]=rememberedZoneOptions[target]&6;
        }
      } else if(kind=="rgbHex" && value<=0xffffff) {
        selectedRgb[target][0]=((value>>16)*100+127)/255;
        selectedRgb[target][1]=(((value>>8)&255)*100+127)/255;
        selectedRgb[target][2]=((value&255)*100+127)/255;
        desiredColorEnabled[target]=true;
      } else return false;
      return applyZoneSelection(target);
    }
    if(kind=="output" && target<4 && value<=1 && status.outputAvailable(target+4)) {
      const Command commands[]={Command::SetUsb,Command::SetPump,Command::SetAccessory3,Command::SetAccessory4};
      const bool sent = transportClient.send(commands[target],0,value);
      if (sent) beginPending(pendingOutputs[target], value != 0);
      return sent;
    }
    if((kind=="inverter"||kind=="charger") && target==0 && value<=1 && status.inverterValid) {
      const bool sent = transportClient.send(kind=="inverter"?Command::SetInverter:Command::SetCharger,0,value);
      if (sent) beginPending(kind=="inverter" ? pendingInverter : pendingCharger, value != 0);
      return sent;
    }
    return false;
  };
  const bool sent=perform();
  lvgl_port_unlock();
  return sent;
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.printf("BlueSquid touchscreen %s starting; reset reason=%d\n",
                AppConfig::kFirmwareVersion, static_cast<int>(esp_reset_reason()));
  Serial.println("Display target: Waveshare ESP32-S3-Touch-LCD-7");

  uiPreferencesReady = uiPreferences.begin("bluesquid-ui", false);
  if (uiPreferencesReady) {
    sleepTimeoutMinutes = uiPreferences.getUShort(
        "sleep_min", kDefaultSleepTimeoutMinutes);
    sleepTimeoutMinutes =
        kSleepTimeoutChoices[sleepTimeoutChoiceIndex(sleepTimeoutMinutes)];
  } else {
    Serial.println("Warning: touchscreen preferences unavailable");
  }
  if (uiPreferencesReady) {
    displayBrightness = constrain(uiPreferences.getUChar("disp_level", 100), 5, 100);
    overnightBrightness = constrain(uiPreferences.getUChar("night_level", 20), 5, 100);
    overnightEnabled = uiPreferences.getBool("night_enabled", false);
    overnightOff = uiPreferences.getUShort("night_off", 1320) % 1440 / 30 * 30;
    overnightOn = uiPreferences.getUShort("night_on", 420) % 1440 / 30 * 30;
    overnightWakeChoice = min(unsigned(uiPreferences.getUChar("night_wake", 1)), 4U);
  }
  TouchClock::begin();
  loadDeviceLabels();


  panel = new Board();
  if (!panel->init()) {
    Serial.println("Display configuration initialization failed");
    return;
  }
  if (!panel->begin()) {
    Serial.println("Display hardware initialization failed");
    return;
  }
  Serial.println("Display hardware initialized");

  auto* backlight = panel->getBacklight();
  if (backlight == nullptr) {
    Serial.println("Warning: display backlight unavailable");
  } else if (!backlight->on()) {
    Serial.println("Warning: display backlight could not be enabled");
  } else {
    Serial.println("Display backlight enabled");
  }

  if (!lvgl_port_init(panel->getLCD(), panel->getTouch())) {
    Serial.println("LVGL 9.5 initialization failed");
    return;
  }
  Serial.println("LVGL initialized");
  TouchHotspot::setRemoteHandlers(remoteStatus,remoteCommand);
  if (!TouchHotspot::begin()) Serial.println("System hotspot initialization failed");
  FirmwareUpdate::setControllerRelay(
      [](bool start) { return transportClient.requestControllerUpdate(start); },
      [] { return transportClient.controllerUpdateStatus(); });
  const auto hotspotLogin = TouchHotspot::credentials();
  if (!FirmwareUpdate::begin(&hotspotLogin)) Serial.println("Firmware update service unavailable");
  lvgl_port_lock(-1);
  buildUi();
  displayDimmer = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(displayDimmer);
  lv_obj_set_size(displayDimmer, 800, 480);
  lv_obj_remove_flag(displayDimmer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(displayDimmer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(displayDimmer, lv_color_black(), 0);
  applyDisplayBrightness();
  lvgl_port_unlock();
  Serial.println("Touchscreen UI ready");
  touchscreenReady = true;
  lastUserActivityMs = millis();
  if (!transportClient.begin()) Serial.println("Transport initialization failed");
  else transportClient.send(BlueSquidControl::Command::RequestStatus, 0, 0);
}

void loop() {
  if (!touchscreenReady) {
    delay(1000);
    return;
  }
  if (lvgl_port_lock(20)) {
    const bool activity = lvgl_port_take_touch_activity();
    const bool wake = lvgl_port_take_wake_request();
    const uint32_t now = millis();
    if (activity || wake) lastUserActivityMs = now;
    const auto action = displaySchedule.update(overnightNow(), displaySleeping,
        activity || wake, now, lastUserActivityMs, uint32_t(sleepTimeoutMinutes) * 60000,
        uint32_t(kNightWakeSeconds[overnightWakeChoice]) * 1000);
    applyDisplayBrightness();
    if (action == DisplaySchedule::State::Wake) wakeDisplay();
    else if (action == DisplaySchedule::State::Sleep) sleepDisplay();
    lvgl_port_unlock();
  }
  transportClient.update();
  TouchHotspot::update();
  refreshUi();
  delay(5);
}
