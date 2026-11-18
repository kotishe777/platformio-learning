#pragma once

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_err.h"

class Encoder {
 public:
  Encoder(gpio_num_t pinA, gpio_num_t pinB) : pinA_(pinA), pinB_(pinB) {}

  void init() {
    pcnt_unit_config_t unitCfg{};
    unitCfg.low_limit  = -countLimit;
    unitCfg.high_limit = countLimit;
    ESP_ERROR_CHECK(pcnt_new_unit(&unitCfg, &unit_));

    pcnt_glitch_filter_config_t filter{};
    filter.max_glitch_ns = 1000;
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unit_, &filter));

    addChannel(pinA_, pinB_, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    addChannel(pinB_, pinA_, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);

    ESP_ERROR_CHECK(pcnt_unit_enable(unit_));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unit_));
    ESP_ERROR_CHECK(pcnt_unit_start(unit_));
  }

  int readClicks() {
    int count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(unit_, &count));
    const int clicks = count / countsPerClick;
    const int delta  = clicks - lastClicks_;
    lastClicks_      = clicks;
    if (count > resetThreshold || count < -resetThreshold) {
      ESP_ERROR_CHECK(pcnt_unit_clear_count(unit_));
      lastClicks_ = 0;
    }
    return delta;
  }

 private:
  static constexpr int countsPerClick = 4;
  static constexpr int countLimit     = 30000;
  static constexpr int resetThreshold = 20000;

  void addChannel(gpio_num_t edgePin, gpio_num_t levelPin,
                  pcnt_channel_edge_action_t onRise, pcnt_channel_edge_action_t onFall) {
    pcnt_chan_config_t cfg{};
    cfg.edge_gpio_num  = edgePin;
    cfg.level_gpio_num = levelPin;
    pcnt_channel_handle_t channel = nullptr;
    ESP_ERROR_CHECK(pcnt_new_channel(unit_, &cfg, &channel));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(channel, onRise, onFall));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
  }

  gpio_num_t          pinA_;
  gpio_num_t          pinB_;
  pcnt_unit_handle_t  unit_       = nullptr;
  int                 lastClicks_ = 0;
};
