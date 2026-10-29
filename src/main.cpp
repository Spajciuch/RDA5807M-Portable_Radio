#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <PicoGFX_SSD1322.h>
#include <oled-utils.h>

#include "hardware/flash.h"
#include "hardware/sync.h"

#include <EasyButton.h>
#include <LTC2944_BMS.h>
#include <RDA5807.h>

#include <EEPROM.h>

// --- PRZYCISKI ----

struct ButtonBinding {
    EasyButton *btn;
    void (*onShort)();
    void (*onLong)();
};

void seek_interrupt();
void setup_buttons();
void station_button(int id, bool long_press);
void seek_button(bool up_press, bool long_press);
void band_button(bool long_press);

const uint8_t UP = 14;
const uint8_t DOWN = 13;
const uint8_t BAND = 12;

const uint8_t ST1 = 6;
const uint8_t ST2 = 7;
const uint8_t ST3 = 8;
const uint8_t ST4 = 9;
const uint8_t ST5 = 10;
const uint8_t ST6 = 11;

const uint8_t PS_PIN = 23;

const uint8_t DEBOUNCE_MS = 40;
const uint16_t LONG_PRESS_MS = 1000;

const bool PULLUP = true;
const bool INVERT = true;

EasyButton upBtn(UP, DEBOUNCE_MS, PULLUP, INVERT);
EasyButton dwnBtn(DOWN, DEBOUNCE_MS, PULLUP, INVERT);
EasyButton bandBtn(BAND, DEBOUNCE_MS, PULLUP, INVERT);

EasyButton st1Btn(ST1, DEBOUNCE_MS, PULLUP, INVERT);
EasyButton st2Btn(ST2, DEBOUNCE_MS, PULLUP, INVERT);
EasyButton st3Btn(ST3, DEBOUNCE_MS, PULLUP, INVERT);
EasyButton st4Btn(ST4, DEBOUNCE_MS, PULLUP, INVERT);
EasyButton st5Btn(ST5, DEBOUNCE_MS, PULLUP, INVERT);
EasyButton st6Btn(ST6, DEBOUNCE_MS, PULLUP, INVERT);

ButtonBinding bindings[] = { // lambda, bo easybutton oczekuje funkcji bez argumentów
    {&upBtn, []() { seek_button(true, false); }, []() { seek_button(true, true); }}, 
    {&dwnBtn, []() { seek_button(false, false); }, []() { seek_button(false, true); }},
    {&bandBtn, []() { band_button(false); }, []() { band_button(true); }},
    {&st1Btn, []() { station_button(1, false); }, []() { station_button(1, true); }},
    {&st2Btn, []() { station_button(2, false); }, []() { station_button(2, true); }},
    {&st3Btn, []() { station_button(3, false); }, []() { station_button(3, true); }},
    {&st4Btn, []() { station_button(4, false); }, []() { station_button(4, true); }},
    {&st5Btn, []() { station_button(5, false); }, []() { station_button(5, true); }},
    {&st6Btn, []() { station_button(6, false); }, []() { station_button(6, true); }},
};

// --- ----

// ---- ZAPIS DANYCH ----

void updateConfig();

struct RadioConfig {
    uint16_t magic = 0xA55A;
    uint16_t frequencies[18] = {8770, 10330, 9890, 10010, 9760, 10710, 9440, 9760, 10210, 9560, 10010, 9330, 9480, 10270, 10210, 10690, 9870, 10480}; // domyślne wartości
};

RadioConfig default_radioConfig;
RadioConfig radioConfig;

// --- ----

// ---- WYŚWIETLACZ ----

#define DC_PIN 8
#define RST_PIN 10
#define CS_PIN 9

Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire1, -1);

// ---- ----

// ---- LICZNIKI ----

long rds_timestamp = millis();
long stats_timestamp = millis();
long interaction_timestamp = millis();
long power_screen_timestamp = millis();

// ---- ----

// ---- STATUS ----

long band_n = 0;
long station_n = -1;
bool power_screen = false;

// ---- ----

// ---- ODVIORNIK RADIOWY ----

RDA5807 rx;

int CRYSTAL_DELAY = 2000;
#define VOLUME 9

// ---- ----

// ---- FUNKCJONALNOŚCI RADIA ----

#define DELAY_RDS 100
#define DELAY_STATS 500

#define DELAY_INTERACTION 5000
#define POWER_SCREEN_DURATION 7000

char *station_name;

void refresh_frequency();
void refresh_RDS();
boolean proper_rds(char *str);

// ---- ----

LTC2944_BMS bms;

void setup() {
    Serial.begin(115200);

    EEPROM.begin(1024);
    EEPROM.get(0, radioConfig);

    if (radioConfig.magic != default_radioConfig.magic) {
        EEPROM.put(0, default_radioConfig);
        EEPROM.commit();

        radioConfig = default_radioConfig;
    }

    station_n = -1;

    // ---- INICJALIZACJA OLED ----

    Wire1.setSDA(18);
    Wire1.setSCL(19);
    Wire1.begin();

    display.begin(0x3c, true);
    display.clearDisplay();
    display.setTextWrap(false);

    draw_ui();

    // ---- ----

    // ---- INICJALIZACJA RDA5807M ----
    rx.setDelayAfterCrystalOn(100);

    rx.setup();
    rx.setVolume(VOLUME);
    rx.setAFC(true);
    // rx.setLnaPortSel(2); // Trying improve sensitivity.
    rx.setRDS(true);
    rx.setRdsFifo(true);
    rx.setMono(true);
    delay(500);
    rx.setFrequency(10330);

    // ---- ----

    // ---- PRZYCISKI ----

    pinMode(UP, INPUT_PULLUP);
    pinMode(DOWN, INPUT_PULLUP);
    pinMode(ST1, INPUT_PULLUP);
    pinMode(ST2, INPUT_PULLUP);
    pinMode(ST3, INPUT_PULLUP);
    pinMode(ST4, INPUT_PULLUP);
    pinMode(ST5, INPUT_PULLUP);
    pinMode(ST6, INPUT_PULLUP);
    pinMode(BAND, INPUT_PULLUP);

    setup_buttons();

    // ---- ----

    show_frequency(rx.getFrequency());
    show_band(band_n, station_n);

    attachInterrupt(digitalPinToInterrupt(UP), seek_interrupt, FALLING); // PRZERWIJ SZUKANIE GDY WCIŚNIĘTO PRZYCISK STROJENIA
    attachInterrupt(digitalPinToInterrupt(DOWN), seek_interrupt, FALLING);
}

void loop() {
    // ---- NORMALNY EKRAN ----
    if (!power_screen) {
        // ----  UPDATE RDSa ----
        if ((millis() - rds_timestamp) > DELAY_RDS) {
            if (rx.getRdsReady()) {
                refresh_RDS();
            }

            rds_timestamp = millis();
        }

        // ---- ----

        // ---- ZMIEŃ JASNOŚĆ EKRANU ----

        if ((millis() - interaction_timestamp) > DELAY_INTERACTION) {
            display.setContrast(0);
        } else {
            display.setContrast(255);
        }

        // ---- ----

        // ---- UPDATE STATYSTYK ----

        if ((millis() - stats_timestamp) > DELAY_STATS) {
            bms.update();
            show_signal(rx.getRssi()); // update siły sygnału
            // Serial.println(digitalRead(13));
            stats_timestamp = millis();
        }
    } else {
        if ((millis() - stats_timestamp) > DELAY_STATS) {
            bms.update();
            show_power_screen(bms.getVoltage(), bms.getCurrent(), bms.getSoc());
            stats_timestamp = millis();
        }

        if ((millis() - power_screen_timestamp) > POWER_SCREEN_DURATION) {
            power_screen = false;

            draw_ui();
            show_frequency(rx.getFrequency());
        }
    }

    // ---- ----

    // ---- ODCZYT PRZYCISKÓW ----

    for (auto &b : bindings)
        b.btn->read();

    // ---- ----
}

void updateConfig() {
    EEPROM.put(0, radioConfig);
    EEPROM.commit();
}

// ---- OBSŁUGA PRZYCISKÓW ----

void setup_buttons() {
    for (auto &b : bindings) {
        b.btn->begin();
        b.btn->onPressed(b.onShort);
        b.btn->onPressedFor(LONG_PRESS_MS, b.onLong);
    }
}

void seek_interrupt() { rx.stopSeek(); }

void station_button(int id, bool long_press) {
    station_n = id;

    if (!long_press) {
        rx.clearRdsBuffer();
        rx.setFrequency(radioConfig.frequencies[(band_n * 6) + (station_n - 1)]);
    } else {
        radioConfig.frequencies[(band_n * 6) + (station_n - 1)] = rx.getFrequency();
        updateConfig();
    }

    show_frequency(rx.getFrequency());

    show_band(band_n, station_n);
    interaction_timestamp = millis();
}

// -------------------------------------------------

void seek_button(bool up_press, bool long_press) {
    station_n = -1;

    if (!long_press)
        up_press ? rx.setFrequencyUp() : rx.setFrequencyDown();
    else
        up_press ? rx.seek(RDA_SEEK_WRAP, RDA_SEEK_UP, refresh_frequency) : rx.seek(RDA_SEEK_WRAP, RDA_SEEK_DOWN, refresh_frequency);

    show_frequency(rx.getFrequency());
    rx.clearRdsBuffer();

    show_band(band_n, station_n);
    interaction_timestamp = millis();
}

// -------------------------------------------------

void band_button(bool long_press) {
    if (!long_press) {
        station_n = -1;

        (band_n < 2) ? band_n++ : band_n = 0; // 0, 1, 2, 0, 1, 2 ... 0, 1, 2
        show_frequency(rx.getFrequency());
        show_band(band_n, station_n);
    } else {
        digitalWrite(PS_PIN, !digitalRead(PS_PIN));
    }

    interaction_timestamp = millis();
}

// ---- ----

// ---- FUNKCJE POMOCNICZE ----

void refresh_frequency() { show_frequency(rx.getFrequency()); }

void refresh_RDS() {
    if (rx.getRdsReady()) {
        if ((station_name = rx.getRdsText0A()) != NULL && proper_rds(station_name) && rx.getErrorBlockA() == 0) {
            show_rds(station_name); // RDS Blok A - Nazwa stacji
        }
    }
}

boolean proper_rds(char *str) {
    uint8_t length = strlen(str);

    if (length > 8) {
        return false;
    }

    for (int i = 0; i < length; i++) {
        if (!isAlphaNumeric(str[i]) && !isAscii(str[i])) {
            return false;
        }
    }

    return true;
}

// ---- ----