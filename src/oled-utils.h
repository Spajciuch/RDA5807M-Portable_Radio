#include <Adafruit_SH110X.h>
#include<RDA5807.h>

#ifndef DISPLAY_UTILS_H
#define DISPLAY_UTILS_H

void show_frequency(float frequency_khz);
void show_rds(const char * rds_text); 
void show_power_screen(float voltage, float current, int precentage);
void show_band(int band, int station);
void show_signal(int signal);

void draw_power_ui();
void draw_ui();

extern Adafruit_SH1106G display;
extern long band_n, station_n;
extern RDA5807 rx;

#endif