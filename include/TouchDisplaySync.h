#pragma once

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

// Touchscreen display settings (device names, icons, icon colours, and the
// visibility and order of devices) kept on the Controller so that every
// touchscreen shows the same set. Each message fits one BLE notification.
namespace TouchDisplaySync {

constexpr uint8_t kLabelCount = 9;
// Visibility and order cover every label except "All lights".
constexpr uint8_t kLayoutCount = kLabelCount - 1;
constexpr size_t kNameLength = 24;
constexpr size_t kMessageSize = 64;

// Discovery request: the Controller answers with every stored message, or
// kNone when no touchscreen has saved display settings yet.
constexpr char kRequest[] = "ui.config";
constexpr char kNone[] = "ui.none";
// <index>|<icon>|<colour>|<name>
constexpr char kLabelPrefix[] = "ui.label=";
// <visible>,<order> for each layout entry, separated by ';'
constexpr char kLayoutPrefix[] = "ui.layout=";

struct Label {
  uint8_t index = 0;
  uint8_t icon = 0;
  uint8_t colour = 0;
  char name[kNameLength + 1]{};
};

struct Layout {
  bool visible[kLayoutCount]{};
  uint8_t order[kLayoutCount]{};
};

inline bool readNumber(const char*& text, unsigned limit, char end, uint8_t& value) {
  char* stop = nullptr;
  const unsigned long parsed = strtoul(text, &stop, 10);
  if (stop == text || *stop != end || parsed > limit) return false;
  value = static_cast<uint8_t>(parsed);
  text = stop + 1;
  return true;
}

inline bool parseLabel(const char* text, Label& label) {
  if (strncmp(text, kLabelPrefix, sizeof(kLabelPrefix) - 1) != 0) return false;
  text += sizeof(kLabelPrefix) - 1;
  // Icon and colour indexes are checked against each screen's own tables.
  if (!readNumber(text, kLabelCount - 1, '|', label.index) ||
      !readNumber(text, 255, '|', label.icon) ||
      !readNumber(text, 255, '|', label.colour))
    return false;
  const size_t length = strlen(text);
  if (length == 0 || length > kNameLength) return false;
  for (size_t index = 0; index < length; ++index)
    if (static_cast<uint8_t>(text[index]) < 0x20) return false;
  memcpy(label.name, text, length + 1);
  return true;
}

inline String encodeLabel(const Label& label) {
  char message[kMessageSize];
  snprintf(message, sizeof(message), "%s%u|%u|%u|%s", kLabelPrefix, label.index,
           label.icon, label.colour, label.name);
  return String(message);
}

inline bool parseLayout(const char* text, Layout& layout) {
  if (strncmp(text, kLayoutPrefix, sizeof(kLayoutPrefix) - 1) != 0) return false;
  text += sizeof(kLayoutPrefix) - 1;
  for (uint8_t index = 0; index < kLayoutCount; ++index) {
    uint8_t visible = 0;
    const char end = index + 1 < kLayoutCount ? ';' : '\0';
    if (!readNumber(text, 1, ',', visible) ||
        !readNumber(text, kLayoutCount - 1, end, layout.order[index]))
      return false;
    layout.visible[index] = visible != 0;
  }
  return true;
}

inline String encodeLayout(const Layout& layout) {
  String message = kLayoutPrefix;
  for (uint8_t index = 0; index < kLayoutCount; ++index) {
    if (index != 0) message += ';';
    message += layout.visible[index] ? '1' : '0';
    message += ',';
    message += layout.order[index];
  }
  return message;
}

}  // namespace TouchDisplaySync
