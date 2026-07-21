// Zentrale, nicht-geheime Konfiguration des Muellkalenders.
// WLAN-Zugangsdaten liegen bewusst NICHT hier, sondern in secrets.h (gitignored).
#pragma once

// --- Zeitzone & NTP ---
// POSIX-TZ-String fuer Europe/Berlin inkl. Sommerzeit-Regel
#define TIME_ZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "de.pool.ntp.org"
#define NTP_SERVER_2 "pool.ntp.org"

// Wie oft nach einer fehlgeschlagenen NTP-Sync erneut versucht wird (ms)
#define NTP_RETRY_INTERVAL_MS (5UL * 60UL * 1000UL) // 5 Minuten

// --- Abfuhrtermine (schedule.json) ---
// Wird von .github/workflows/update-schedule.yml taeglich aus den echten
// Daten der "Abfall LK BZ"-App (app.abfallplus.de) generiert und hier per
// HTTPS abgerufen, siehe src/waste_schedule.cpp.
#define SCHEDULE_JSON_URL "https://raw.githubusercontent.com/xXCrusaderXx/abfallkalender/master/data/schedule.json"

// Wie oft (ms) versucht wird, schedule.json neu zu laden. Die Quelldatei
// aendert sich hoechstens taeglich (siehe Workflow), haeufigeres Abfragen
// braeuchte es nicht.
#define SCHEDULE_FETCH_INTERVAL_MS (12UL * 60UL * 60UL * 1000UL) // 12 Stunden

// --- Abend-Reminder ---
// Ab dieser Uhrzeit (lokale Zeit) am Vorabend einer Abfuhr beginnt das Blinken.
#define REMINDER_START_HOUR 18
#define REMINDER_START_MINUTE 0

// Blink-Intervall der hervorgehobenen Kachel im Reminder-Modus (ms)
#define REMINDER_BLINK_INTERVAL_MS 700

// --- Display ---
// Rotation gemaess LVGL/TFT_eSPI-Konvention (0-3). 1 = Querformat, USB-Anschluss links.
#define DISPLAY_ROTATION 1

// Helligkeit der Hintergrundbeleuchtung, 0-255 (PWM ueber TFT_BL-Pin)
#define BACKLIGHT_DEFAULT_DUTY 255
