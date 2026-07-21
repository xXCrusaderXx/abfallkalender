// Datenmodell fuer die Abfuhrtermine.
//
// Die Termine kommen NICHT mehr aus fest im Code hinterlegten Listen, sondern
// aus data/schedule.json in diesem Repo (per HTTPS-GET), das wiederum von
// einem taeglichen GitHub-Actions-Workflow (.github/workflows/update-schedule.yml)
// automatisch aus derselben Datenquelle wie die offizielle "Abfall LK BZ"-App
// (app.abfallplus.de) aktualisiert wird - siehe scripts/fetch_schedule.py.
// Ein erfolgreicher Abruf wird als Rohtext in LittleFS gecacht, damit das
// Geraet auch ohne WLAN sofort die zuletzt bekannten Termine anzeigen kann.
#pragma once
#include <Arduino.h>
#include <time.h>

// Die vier Tonnenarten, die auf dem Display angezeigt werden.
enum class BinType : uint8_t {
    Restmuell = 0,
    Bioabfall,
    GelbeTonne,
    BlaueTonne,
    COUNT
};

// Ein einzelnes Kalenderdatum (lokale Zeit Europe/Berlin), ohne Uhrzeit.
struct PickupDate {
    uint16_t year;
    uint8_t month; // 1-12
    uint8_t day;   // 1-31
};

// Menschlicher Anzeigename der Tonnenart, fuer Kachel-Titel.
const char *binTypeName(BinType type);

// Ob fuer diese Tonnenart aktuell Termindaten geladen sind (aus Cache oder
// Netzwerk). false heisst: weder Cache noch Netzwerk-Fetch waren bisher
// erfolgreich - die UI zeigt dann einen Platzhalter statt eines Datums an.
bool binTypeHasData(BinType type);

// Liefert den naechsten Abholtermin (heute oder spaeter) fuer die angegebene
// Tonnenart. Rueckgabe false, wenn kein zukuenftiger Termin ermittelt werden
// konnte (Datenbestand erschoepft oder Termine fuer diese Tonnenart offen).
bool getNextPickup(BinType type, time_t now, PickupDate &outDate);

// Anzahl ganzer Kalendertage zwischen "now" und dem Abholtermin.
// 0 = heute, 1 = morgen usw. Beide Zeitpunkte werden vor der Differenzbildung
// auf Mitternacht normalisiert.
int daysUntil(time_t now, const PickupDate &date);

// Wandelt ein PickupDate in Mitternacht (00:00 Uhr lokaler Zeit) als time_t um.
time_t pickupDateToMidnight(const PickupDate &date);

// Einmalig in setup() aufrufen: mountet LittleFS, laedt eine evtl. vorhandene
// gecachte schedule.json und stoesst danach einen ersten Netzwerk-Refresh an
// (best effort - schlaegt der Fetch fehl, bleiben die gecachten/leeren Daten
// bestehen). Blockiert kurzzeitig waehrend des HTTPS-Requests.
void scheduleBegin();

// Regelmaessig aus der Arduino loop() aufrufen. Stoesst alle
// SCHEDULE_FETCH_INTERVAL_MS einen erneuten HTTPS-Fetch an (siehe config.h).
// Schlaegt ein Fetch fehl (kein WLAN, Server nicht erreichbar, ungueltiges
// JSON), bleiben die zuletzt bekannten Termine unveraendert erhalten.
void scheduleLoop();
