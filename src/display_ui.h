// LVGL-basierte Anzeige: vier Kacheln (Restabfall, Bioabfall, Gelbe Tonne,
// Blaue Tonne) mit Datum und "Tage bis dahin", Hervorhebung der naechsten
// Abfuhr sowie Abend-Reminder-Blinken.
#pragma once
#include <time.h>

// Initialisiert TFT, Touch-Controller und LVGL sowie die vier Kacheln.
// Einmalig in setup() aufrufen.
void displayInit();

// In jedem loop()-Durchlauf aufrufen: treibt lv_timer_handler() und das
// Blinken des Abend-Reminders an.
void displayLoop();

// Aktualisiert Datum/"Tage bis"/Hervorhebung/Reminder-Status der vier
// Kacheln. now = aktuelle lokale Zeit (Europe/Berlin); timeIsValid = ob die
// Zeit bereits per NTP synchronisiert wurde (siehe time_sync.h). Sollte
// periodisch aufgerufen werden (reicht z.B. einmal pro Minute).
void displayUpdate(time_t now, bool timeIsValid);
