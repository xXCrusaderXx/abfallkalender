// Datenmodell fuer die Abfuhrtermine.
//
// Bewusst getrennt von der UI: neue/fehlende Terminlisten (Gelbe Tonne, Blaue
// Tonne, Bioabfall-Winterturnus) koennen hier nachgetragen werden, ohne dass
// display_ui.cpp angefasst werden muss. Spaeter (V2) kann getNextPickup()
// intern auf einen HTTP/JSON-Abruf umgestellt werden, ohne dass sich die
// Schnittstelle nach aussen aendert.
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

// Ob fuer diese Tonnenart aktuell vollstaendige Termindaten hinterlegt sind.
// false bedeutet: Termine sind noch offen (siehe TODOs in waste_schedule.cpp) -
// die UI zeigt dann einen Platzhalter statt eines Datums an.
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
