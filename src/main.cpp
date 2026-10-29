#include <Arduino.h>

#include <oled-utils.h>
#include <PicoGFX_SSD1322.h>
#include <Adafruit_GFX.h>

#include "hardware/flash.h"
#include "hardware/sync.h"

#include <RDA5807.h>
#include <LTC2944_BMS.h>
#include <EasyButton.h>

// --- PRZYCISKI ----

const int UP = 14;
const int DOWN = 13;
const int BAND = 12;
const int ST1 = 6;
const int ST2 = 7;
const int ST3 = 8;
const int ST4 = 9;
const int ST5 = 10;
const int ST6 = 11;

const int PS_PIN = 23;

uint32_t debounce = 40;
const int duration = 1000;

bool pullup = true;
bool invert = true;

EasyButton upBtn(UP, debounce, pullup, invert);
EasyButton dwnBtn(DOWN, debounce, pullup, invert);
EasyButton bandBtn(BAND, debounce, pullup, invert);
EasyButton st1Btn(ST1, debounce, pullup, invert);
EasyButton st2Btn(ST2, debounce, pullup, invert);
EasyButton st3Btn(ST3, debounce, pullup, invert);
EasyButton st4Btn(ST4, debounce, pullup, invert);
EasyButton st5Btn(ST5, debounce, pullup, invert);
EasyButton st6Btn(ST6, debounce, pullup, invert);

EasyButton *buttons[] = {
    &upBtn, &dwnBtn, &bandBtn,
    &st1Btn, &st2Btn, &st3Btn, &st4Btn, &st5Btn, &st6Btn};

void seek_interrupt();

void station_button(int id, bool long_press);
void seek_button(bool up_press, bool long_press);
void band_button(bool long_press);

// --- ----

// ---- ZAPIS DANYCH ----

#define FLASH_TARGET_OFFSET (512 * 1024)

struct RadioData
{
    int frequencies[18] = {8770, 10330, 9890, 10010, 9760, 10710, 9440, 9760, 10210, 9560, 10010, 9330, 9480, 10270, 10210, 10690, 9870, 10480};
};

RadioData radioData;
void saveMyData();
void readBackMyData();

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

void setup()
{
    Serial.begin(115200);

    readBackMyData();
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

    // ---- INICJALIZACJA BMS ----

    // bms.setShuntResistor(0.05);
    // bms.setProfile(CHEM_LIION, 1);
    // bms.begin();

    // ---- ----

    // ---- INICJALIZACJA RDA5807M ----

    // readBackMyData();
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

    for (auto btn : buttons)
    {
        btn->begin();
    }

    upBtn.onPressed([]()
                    { seek_button(true, false); });
    upBtn.onPressedFor(duration, []()
                       { seek_button(true, true); });

    dwnBtn.onPressed([]()
                     { seek_button(false, false); });
    dwnBtn.onPressedFor(duration, []()
                        { seek_button(false, true); });

    bandBtn.onPressed([]()
                      { band_button(false); });
    bandBtn.onPressedFor(duration, []()
                         { band_button(true); });

    // ================= STACJE =========================

    void (*stationShortFuncs[6])() = {
        []()
        { station_button(1, false); },
        []()
        { station_button(2, false); },
        []()
        { station_button(3, false); },
        []()
        { station_button(4, false); },
        []()
        { station_button(5, false); },
        []()
        { station_button(6, false); }};

    void (*stationLongFuncs[6])() = {
        []()
        { station_button(1, true); },
        []()
        { station_button(2, true); },
        []()
        { station_button(3, true); },
        []()
        { station_button(4, true); },
        []()
        { station_button(5, true); },
        []()
        { station_button(6, true); }};

    EasyButton *stationBtns[] = {&st1Btn, &st2Btn, &st3Btn, &st4Btn, &st5Btn, &st6Btn};

    for (int i = 0; i < 6; i++)
    {
        stationBtns[i]->onPressed(stationShortFuncs[i]);
        stationBtns[i]->onPressedFor(duration, stationLongFuncs[i]);
    }

    // ---- ----

    show_frequency(rx.getFrequency());
    show_band(band_n, station_n);

    attachInterrupt(digitalPinToInterrupt(UP), seek_interrupt, FALLING);
    attachInterrupt(digitalPinToInterrupt(DOWN), seek_interrupt, FALLING);
}

void loop()
{
    // ---- NORMALNY EKRAN ----
    if (!power_screen)
    {
        // ----  UPDATE RDSa ----
        if ((millis() - rds_timestamp) > DELAY_RDS)
        {
            if (rx.getRdsReady())
            {
                refresh_RDS();
            }

            rds_timestamp = millis();
        }

        // ---- ----

        // ---- ZMIEŃ JASNOŚĆ EKRANU ----

        if ((millis() - interaction_timestamp) > DELAY_INTERACTION)
        {
            display.setContrast(0);
        }
        else
        {
            display.setContrast(255);
        }

        // ---- ----

        // ---- UPDATE STATYSTYK ----

        if ((millis() - stats_timestamp) > DELAY_STATS)
        {
            bms.update();
            show_signal(rx.getRssi()); // update siły sygnału
            // Serial.println(digitalRead(13));
            stats_timestamp = millis();
        }
    }
    else
    {
        if ((millis() - stats_timestamp) > DELAY_STATS)
        {
            bms.update();
            show_power_screen(bms.getVoltage(), bms.getCurrent(), bms.getSoc());
            stats_timestamp = millis();
        }

        if ((millis() - power_screen_timestamp) > POWER_SCREEN_DURATION)
        {
            power_screen = false;

            draw_ui();
            show_frequency(rx.getFrequency());
        }
    }

    // ---- ----

    // ---- ODCZYT PRZYCISKÓW ----

    upBtn.read();
    dwnBtn.read();

    st1Btn.read();
    st2Btn.read();
    st3Btn.read();
    st4Btn.read();
    st5Btn.read();
    st6Btn.read();
    bandBtn.read();

    // ---- ----
}

void saveMyData()
{
    uint8_t *radioDataAsBytes = (uint8_t *)&radioData;
    int radioDataSize = sizeof(radioData);

    int writeSize = (radioDataSize / FLASH_PAGE_SIZE) + 1;
    int sectorCount = ((writeSize * FLASH_PAGE_SIZE) / FLASH_SECTOR_SIZE) + 1;

    uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE * sectorCount);
    flash_range_program(FLASH_TARGET_OFFSET, radioDataAsBytes, FLASH_PAGE_SIZE * writeSize);
    restore_interrupts(interrupts);
}

void readBackMyData()
{
    const uint8_t *flash_target_contents = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    memcpy(&radioData, flash_target_contents, sizeof(radioData));
}

// ---- OBSŁUGA PRZYCISKÓW ----

void seek_interrupt()
{
    rx.stopSeek();
    // rx.getBand();
}

void station_button(int id, bool long_press)
{
    station_n = id;

    if (!long_press)
    {
        rx.clearRdsBuffer();
        rx.setFrequency(radioData.frequencies[(band_n * 6) + (station_n - 1)]);
    }
    else
    {
        radioData.frequencies[(band_n * 6) + (station_n - 1)] = rx.getFrequency();
        saveMyData();
    }

    show_frequency(rx.getFrequency());

    show_band(band_n, station_n);
    interaction_timestamp = millis();
}

// -------------------------------------------------

void seek_button(bool up_press, bool long_press)
{
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

void band_button(bool long_press)
{
    //   if (!long_press)
    //   {
    //     station_n = -1;

    //     (band_n < 2) ? band_n++ : band_n = 0; // 0, 1, 2, 0, 1, 2 ... 0, 1, 2
    //     show_frequency(rx.getFrequency());
    //     show_band(band_n, station_n);
    //   }
    //   else
    //   {
    //     digitalWrite(PS_PIN, !digitalRead(PS_PIN));
    //   }

    //   interaction_timestamp = millis();
}

// ---- ----

// ---- FUNKCJE POMOCNICZE ----

void refresh_frequency()
{
    show_frequency(rx.getFrequency());
}

void refresh_RDS()
{
    if (rx.getRdsReady())
    {
        Serial.println(rx.getRdsSync());
        // Serial.println(rx.getRdsText2A());

        if ((station_name = rx.getRdsText0A()) != NULL && proper_rds(station_name) && rx.getErrorBlockA() == 0)
        {
            show_rds(station_name); // RDS Blok A - Nazwa stacji
        }
    }
}

boolean proper_rds(char *str)
{
    uint8_t length = strlen(str);

    if (length > 8)
    {
        return false;
    }

    for (int i = 0; i < length; i++)
    {
        if (!isAlphaNumeric(str[i]) && !isAscii(str[i]))
        {
            return false;
        }
    }

    return true;
}

// ---- ----