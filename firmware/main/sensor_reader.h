#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  char device_id[32];
  uint32_t sequence_number;
  char measured_at[32];
  float pm1;
  float pm25;
  float pm10;
  float co;
  float no2;
  float o3;
  float so2;
  float temperature_c;
  float humidity_percent;
  float battery_voltage;
  float solar_voltage;
  int signal_strength;
  char firmware_version[32];
  uint32_t alarm_flags;
} sensor_reading_t;

bool sensor_reader_collect(sensor_reading_t *reading);
int sensor_reader_get_signal_strength(void);
float sensor_reader_get_battery_voltage(void);
float sensor_reader_get_solar_voltage(void);
uint32_t sensor_reader_compute_alarm_flags(const sensor_reading_t *reading);
