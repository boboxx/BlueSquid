#pragma once
#include <cmath>
#include <stdint.h>
namespace ColourWheel {
struct Rgb { uint8_t r, g, b; };
inline Rgb colour(float x, float y) {
  float saturation = std::fmin(1.0f, std::sqrt(x*x + y*y));
  if (saturation < 0.04f) saturation = 0; // Easy-to-select pure white centre.
  float hue = std::atan2(-y, x) * 3.0f / 3.14159265358979323846f;
  if (hue < 0) hue += 6;
  const int sector = static_cast<int>(hue) % 6;
  const float f = hue - std::floor(hue);
  const float p = 1-saturation, q = 1-saturation*f, t = 1-saturation*(1-f);
  float r=1,g=t,b=p;
  switch (sector) {
    case 1:r=q;g=1;b=p;break; case 2:r=p;g=1;b=t;break;
    case 3:r=p;g=q;b=1;break; case 4:r=t;g=p;b=1;break;
    case 5:r=1;g=p;b=q;break;
  }
  return {static_cast<uint8_t>(std::lround(r*255)),
          static_cast<uint8_t>(std::lround(g*255)),
          static_cast<uint8_t>(std::lround(b*255))};
}
inline void position(float r, float g, float b, float& x, float& y) {
  const float hi=std::fmax(r,std::fmax(g,b)), lo=std::fmin(r,std::fmin(g,b));
  const float delta=hi-lo;
  if (hi<=0 || delta<=0) { x=y=0; return; }
  float h=hi==r ? (g-b)/delta : hi==g ? 2+(b-r)/delta : 4+(r-g)/delta;
  const float angle=h*3.14159265358979323846f/3;
  x=std::cos(angle)*delta/hi; y=-std::sin(angle)*delta/hi;
}
// Smooth visual gradient; touch selection below retains the larger discrete
// targets. Preserve the same white centre and broad vivid outer band.
inline Rgb gradientColour(float x, float y) {
  const float radius = std::sqrt(x*x+y*y);
  if (radius <= 0.24f) return {255,255,255};
  const float saturation = std::fmin(1.0f, (radius-0.24f)/(0.65f-0.24f));
  return colour(x*saturation/radius, y*saturation/radius);
}
// Large touch targets: white centre, twelve pastel sectors, twelve vivid
// sectors, overlaid on the smooth visual gradient.
inline Rgb reducedColour(float x, float y) {
  const float radius = std::sqrt(x*x+y*y);
  if (radius <= 0.24f) return {255,255,255};
  constexpr float step = 3.14159265358979323846f/6;
  const float angle = std::round(std::atan2(y,x)/step)*step;
  const float saturation = radius < 0.65f ? 0.5f : 1.0f;
  return colour(std::cos(angle)*saturation, std::sin(angle)*saturation);
}
inline void reducedPosition(float r, float g, float b, float& x, float& y) {
  position(r,g,b,x,y);
  const float saturation = std::sqrt(x*x+y*y);
  if (saturation < 0.01f) { x=y=0; return; }
  // Put the marker inside the band, never half outside the canvas edge.
  const float radius = saturation < 0.75f ? 0.445f : 0.825f;
  x *= radius/saturation;
  y *= radius/saturation;
}
}
