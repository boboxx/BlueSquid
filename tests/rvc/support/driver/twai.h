#pragma once
#include <stdint.h>
#include <string.h>
#include <algorithm>
using std::min; using std::max;
using gpio_num_t = int;
constexpr int ESP_OK = 0, TWAI_MODE_NORMAL = 0;
enum {TWAI_STATE_RUNNING, TWAI_STATE_BUS_OFF, TWAI_STATE_RECOVERING, TWAI_STATE_STOPPED};
struct twai_message_t { uint32_t identifier=0; unsigned extd=0, rtr=0, ss=0; uint8_t data_length_code=0, data[8]{}; };
struct twai_general_config_t { int tx_queue_len=5, rx_queue_len=5; };
struct twai_timing_config_t {};
struct twai_filter_config_t {};
struct twai_status_info_t { int state=TWAI_STATE_RUNNING; };
#define TWAI_GENERAL_CONFIG_DEFAULT(a,b,c) {}
#define TWAI_TIMING_CONFIG_250KBITS() {}
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() {}
int twai_driver_install(const twai_general_config_t*, const twai_timing_config_t*, const twai_filter_config_t*);
int twai_driver_uninstall(); int twai_start(); int twai_stop(); int twai_initiate_recovery();
int twai_transmit(const twai_message_t*, int); int twai_receive(twai_message_t*, int);
int twai_get_status_info(twai_status_info_t*);
uint32_t millis();
struct FakeEsp { uint64_t getEfuseMac() { return 0x12345678; } };
extern FakeEsp ESP;
