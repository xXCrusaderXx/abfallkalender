#include "display_ui.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <TAMC_GT911.h>
#include "config.h"
#include "waste_schedule.h"

namespace {

// --- Bildschirm-Aufloesung nach Rotation -----------------------------------
// Panel ist nativ 240x320 (Hochformat). DISPLAY_ROTATION=1 dreht auf
// Querformat, LVGL rechnet daher mit vertauschten Massen.
const uint16_t kScreenWidth = 320;
const uint16_t kScreenHeight = 240;

// --- Touch (GT911, kapazitiv, I2C) -----------------------------------------
// Pinbelegung verifiziert ueber die Board-Definition
// rzeldent/platformio-espressif32-sunton/esp32-2432S032C.json
const uint8_t kTouchSda = 33;
const uint8_t kTouchScl = 32;
const uint8_t kTouchInt = 21;
const uint8_t kTouchRst = 25;
// Native (nicht rotierte) Aufloesung, wie sie der Touch-Controller meldet.
const uint16_t kTouchNativeWidth = 240;
const uint16_t kTouchNativeHeight = 320;

TFT_eSPI tft = TFT_eSPI();
TAMC_GT911 touch = TAMC_GT911(kTouchSda, kTouchScl, kTouchInt, kTouchRst,
                               kTouchNativeWidth, kTouchNativeHeight);

// Anzahl der Zeilen, die der LVGL-Renderpuffer gleichzeitig haelt. Kleiner
// gehalten als oft empfohlen, weil der ESP32 nur begrenztes internes DRAM hat
// und dieses UI mit wenigen, seltenen Redraws problemlos damit auskommt.
const uint16_t kDrawBufLines = 20;
lv_disp_draw_buf_t drawBuf;
lv_color_t lvBuffer[kScreenWidth * kDrawBufLines];

// --- Farben je Tonnenart -----------------------------------------------------
const lv_color_t kColorRestmuell = LV_COLOR_MAKE(0x80, 0x80, 0x80); // grau
const lv_color_t kColorBioabfall = LV_COLOR_MAKE(0x6B, 0x44, 0x23); // braun
const lv_color_t kColorGelbeTonne = LV_COLOR_MAKE(0xF2, 0xC2, 0x30); // gelb
const lv_color_t kColorBlaueTonne = LV_COLOR_MAKE(0x2C, 0x6F, 0xBB); // blau
const lv_color_t kColorWarnung = LV_COLOR_MAKE(0xE8, 0x4C, 0x3D);   // Reminder-Blinkfarbe
const lv_color_t kColorTextHell = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);
const lv_color_t kColorTextDunkel = LV_COLOR_MAKE(0x20, 0x20, 0x20);

struct BinTile {
    BinType type;
    lv_obj_t *container = nullptr;
    lv_obj_t *nameLabel = nullptr;
    lv_obj_t *dateLabel = nullptr;
    lv_obj_t *daysLabel = nullptr;
    lv_color_t baseColor{};
    bool reminderDue = false; // Vorabend-Reminder aktuell aktiv fuer diese Kachel
};

BinTile tiles[(size_t)BinType::COUNT];
lv_obj_t *syncStatusLabel = nullptr;

unsigned long lastBlinkToggleMs = 0;
bool blinkOn = false;

lv_color_t colorForType(BinType type) {
    switch (type) {
        case BinType::Restmuell: return kColorRestmuell;
        case BinType::Bioabfall: return kColorBioabfall;
        case BinType::GelbeTonne: return kColorGelbeTonne;
        case BinType::BlaueTonne: return kColorBlaueTonne;
        default: return kColorRestmuell;
    }
}

// Gelbe Kacheln brauchen dunklen statt hellen Text fuer ausreichend Kontrast.
lv_color_t textColorForType(BinType type) {
    return type == BinType::GelbeTonne ? kColorTextDunkel : kColorTextHell;
}

// --- LVGL <-> TFT_eSPI Anbindung --------------------------------------------

void dispFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colorP) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)colorP, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(drv);
}

void touchpadRead(lv_indev_drv_t *indevDrv, lv_indev_data_t *data) {
    touch.read();
    if (touch.isTouched) {
        // GT911 liefert Koordinaten im nativen Hochformat (240 x 320).
        // Unsere UI laeuft im Querformat (DISPLAY_ROTATION=1). Umrechnung
        // hier manuell statt ueber touch.setRotation(), damit die Zuordnung
        // an dieser einen Stelle nachvollziehbar bleibt.
        //
        // TODO(marko): Am realen Geraet verifizieren - falls Touch-Punkte
        // nicht zur angezeigten Kachel passen, hier X/Y tauschen bzw. die
        // Vorzeichen der Subtraktion anpassen.
        uint16_t rawX = touch.points[0].x;
        uint16_t rawY = touch.points[0].y;
        data->point.x = rawY;
        data->point.y = kTouchNativeWidth - 1 - rawX;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// --- Kachel-Aufbau -----------------------------------------------------------

void createTile(BinType type, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    BinTile &tile = tiles[(size_t)type];
    tile.type = type;
    tile.baseColor = colorForType(type);
    lv_color_t textColor = textColorForType(type);

    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_size(cont, w, h);
    lv_obj_set_style_bg_color(cont, tile.baseColor, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cont, 10, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 6, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    tile.container = cont;

    lv_obj_t *name = lv_label_create(cont);
    lv_label_set_text(name, binTypeName(type));
    lv_obj_set_style_text_color(name, textColor, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);
    tile.nameLabel = name;

    lv_obj_t *date = lv_label_create(cont);
    lv_label_set_text(date, "--.--.");
    lv_obj_set_style_text_color(date, textColor, 0);
    lv_obj_set_style_text_font(date, &lv_font_montserrat_20, 0);
    lv_obj_align(date, LV_ALIGN_CENTER, 0, -6);
    tile.dateLabel = date;

    lv_obj_t *days = lv_label_create(cont);
    lv_label_set_text(days, "");
    lv_obj_set_style_text_color(days, textColor, 0);
    lv_obj_set_style_text_font(days, &lv_font_montserrat_24, 0);
    lv_obj_align(days, LV_ALIGN_BOTTOM_MID, 0, 0);
    tile.daysLabel = days;
}

void buildTiles() {
    const uint16_t margin = 6;
    const uint16_t tileW = (kScreenWidth - 3 * margin) / 2;
    const uint16_t tileH = (kScreenHeight - 3 * margin) / 2;

    createTile(BinType::Restmuell, margin, margin, tileW, tileH);
    createTile(BinType::Bioabfall, 2 * margin + tileW, margin, tileW, tileH);
    createTile(BinType::GelbeTonne, margin, 2 * margin + tileH, tileW, tileH);
    createTile(BinType::BlaueTonne, 2 * margin + tileW, 2 * margin + tileH, tileW, tileH);

    // Status-Overlay fuer "Zeit noch nicht synchronisiert" - wird ueber den
    // Kacheln angezeigt und nach dem ersten NTP-Sync ausgeblendet.
    syncStatusLabel = lv_label_create(lv_scr_act());
    lv_label_set_text(syncStatusLabel, "Zeit wird synchronisiert...");
    lv_obj_set_style_text_color(syncStatusLabel, kColorTextHell, 0);
    lv_obj_set_style_bg_color(syncStatusLabel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(syncStatusLabel, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(syncStatusLabel, 4, 0);
    lv_obj_align(syncStatusLabel, LV_ALIGN_TOP_MID, 0, 0);
}

// Setzt die Kachel auf ihre normale (nicht blinkende) Optik zurueck.
void resetTileHighlight(BinTile &tile) {
    lv_obj_set_style_bg_color(tile.container, tile.baseColor, 0);
    lv_obj_set_style_border_width(tile.container, 0, 0);
}

// Hebt die Kachel der naechsten anstehenden Abfuhr per Rahmen hervor.
void applyNextPickupHighlight(BinTile &tile) {
    lv_obj_set_style_border_color(tile.container, kColorTextHell, 0);
    lv_obj_set_style_border_width(tile.container, 4, 0);
    lv_obj_set_style_border_opa(tile.container, LV_OPA_COVER, 0);
}

} // namespace

void displayInit() {
    tft.init();
    tft.setRotation(DISPLAY_ROTATION);

    // Hintergrundbeleuchtung per PWM (LEDC-API von Arduino-ESP32 Core 2.x).
    const int kBacklightLedcChannel = 0;
    ledcSetup(kBacklightLedcChannel, 5000, 8);
    ledcAttachPin(TFT_BL, kBacklightLedcChannel);
    ledcWrite(kBacklightLedcChannel, BACKLIGHT_DEFAULT_DUTY);

    lv_init();
    lv_disp_draw_buf_init(&drawBuf, lvBuffer, nullptr, kScreenWidth * kDrawBufLines);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = kScreenWidth;
    dispDrv.ver_res = kScreenHeight;
    dispDrv.flush_cb = dispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    touch.begin();

    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = touchpadRead;
    lv_indev_drv_register(&indevDrv);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    buildTiles();
}

void displayLoop() {
    lv_timer_handler();

    unsigned long nowMs = millis();
    if (nowMs - lastBlinkToggleMs >= REMINDER_BLINK_INTERVAL_MS) {
        lastBlinkToggleMs = nowMs;
        blinkOn = !blinkOn;
        for (BinTile &tile : tiles) {
            if (tile.reminderDue) {
                lv_obj_set_style_bg_color(tile.container, blinkOn ? kColorWarnung : tile.baseColor, 0);
            }
        }
    }
}

void displayUpdate(time_t now, bool timeIsValid) {
    if (syncStatusLabel != nullptr) {
        if (timeIsValid) {
            lv_obj_add_flag(syncStatusLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(syncStatusLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!timeIsValid) {
        return; // Ohne gueltige Zeit koennen Termine/Reminder nicht berechnet werden
    }

    struct tm nowTm;
    localtime_r(&now, &nowTm);
    int nowMinutesOfDay = nowTm.tm_hour * 60 + nowTm.tm_min;
    int reminderStartMinutes = REMINDER_START_HOUR * 60 + REMINDER_START_MINUTE;

    // Kachel mit dem kleinsten "Tage bis dahin"-Wert wird hervorgehoben.
    int bestDays = 999999;
    BinType bestType = BinType::COUNT;

    for (size_t i = 0; i < (size_t)BinType::COUNT; ++i) {
        BinTile &tile = tiles[i];
        PickupDate date;
        bool hasNext = getNextPickup(tile.type, now, date);

        if (!hasNext) {
            lv_label_set_text(tile.dateLabel, "Termine");
            lv_label_set_text(tile.daysLabel, "folgen");
            tile.reminderDue = false;
            resetTileHighlight(tile);
            continue;
        }

        int days = daysUntil(now, date);

        char dateBuf[8];
        snprintf(dateBuf, sizeof(dateBuf), "%02u.%02u.", date.day, date.month);
        lv_label_set_text(tile.dateLabel, dateBuf);

        if (days <= 0) {
            lv_label_set_text(tile.daysLabel, "Heute!");
        } else if (days == 1) {
            lv_label_set_text(tile.daysLabel, "Morgen");
        } else {
            char daysBuf[16];
            snprintf(daysBuf, sizeof(daysBuf), "in %d Tagen", days);
            lv_label_set_text(tile.daysLabel, daysBuf);
        }

        // Reminder: ab der konfigurierten Uhrzeit am Vorabend (days == 1) einer Abfuhr.
        tile.reminderDue = (days == 1) && (nowMinutesOfDay >= reminderStartMinutes);
        if (!tile.reminderDue) {
            resetTileHighlight(tile);
        }

        if (days < bestDays) {
            bestDays = days;
            bestType = tile.type;
        }
    }

    if (bestType != BinType::COUNT) {
        applyNextPickupHighlight(tiles[(size_t)bestType]);
    }
}
