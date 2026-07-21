#include <Arduino.h>
#include "config.h"
#include "time_sync.h"
#include "display_ui.h"

// Wie oft die Kacheln neu berechnet werden (Termine/"Tage bis"/Reminder).
// Kalenderdaten aendern sich nur beim Ueberschreiten der Mitternachtsgrenze,
// der Reminder braucht aber minuetliche Genauigkeit fuer den Start um
// REMINDER_START_HOUR:REMINDER_START_MINUTE - 30s Intervall ist dafuer mehr
// als ausreichend und kostet praktisch nichts an Rechenzeit.
const unsigned long kScheduleUpdateIntervalMs = 30UL * 1000UL;

unsigned long lastScheduleUpdateMs = 0;

void setup() {
    Serial.begin(115200);

    displayInit();
    timeSyncBegin();

    // Sofortige erste Aktualisierung, damit die UI nicht bis zum ersten
    // Intervall-Tick leer bleibt.
    displayUpdate(timeSyncNow(), timeSyncIsValid());
    lastScheduleUpdateMs = millis();
}

void loop() {
    displayLoop();
    timeSyncLoop();

    unsigned long nowMs = millis();
    if (nowMs - lastScheduleUpdateMs >= kScheduleUpdateIntervalMs) {
        lastScheduleUpdateMs = nowMs;
        displayUpdate(timeSyncNow(), timeSyncIsValid());
    }
}
