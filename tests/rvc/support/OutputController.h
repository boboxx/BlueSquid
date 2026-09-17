#pragma once
#include <stdint.h>
#include <functional>
class OutputController {
 public:
  std::function<bool(uint8_t)> handler;
  std::function<bool(bool)> directionHandler;
  uint8_t direction=3;
  void setFanDirectionHandler(std::function<bool(bool)> fn) { directionHandler=fn; }
  bool enabled=false, online=false, on=false, pending=false;
  uint8_t speed=0, source=0, instance=0, error=0;
  void setFanCommandHandler(std::function<bool(uint8_t)> fn) { handler=fn; }
  void setRvcFanStatus(bool e,bool o,bool power,uint8_t s,bool p,uint8_t a,uint8_t i,uint8_t err,uint8_t dir) {
    direction=dir;
    enabled=e;online=o;on=power;speed=s;pending=p;source=a;instance=i;error=err;
  }
};
