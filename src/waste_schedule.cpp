#include "waste_schedule.h"

namespace {

// --- Restabfall: bestaetigte Termine 2026 -----------------------------------
// Quelle: Abfallkalender Landkreis Bautzen 2026 (offizielles PDF), Gemeinde
// Arnsdorf. Enthaelt bereits alle feiertagsbedingten Verschiebungen (z.B.
// 02.01. und 10.04. fallen auf einen Freitag statt Donnerstag).
const PickupDate kRestmuell2026[] = {
    {2026, 1, 2},   {2026, 1, 15},  {2026, 1, 29},  {2026, 2, 12},
    {2026, 2, 26},  {2026, 3, 12},  {2026, 3, 26},  {2026, 4, 10},
    {2026, 4, 23},  {2026, 5, 7},   {2026, 5, 21},  {2026, 6, 4},
    {2026, 6, 18},  {2026, 7, 2},   {2026, 7, 16},  {2026, 7, 30},
    {2026, 8, 13},  {2026, 8, 27},  {2026, 9, 10},  {2026, 9, 24},
    {2026, 10, 8},  {2026, 10, 22}, {2026, 11, 5},  {2026, 11, 19},
    {2026, 12, 3},  {2026, 12, 17},
};
const size_t kRestmuellCount = sizeof(kRestmuell2026) / sizeof(kRestmuell2026[0]);

// --- Bioabfall: Sommersaison, woechentlich mittwochs ------------------------
// TODO(marko): Winterturnus (vor dem Saisonstart bzw. nach dem Saisonende)
// noch nicht bestaetigt - vermutlich 14-taegig. Sobald bekannt, hier eine
// eigene Terminliste/Regel ergaenzen; getNextPickup() liefert fuer Anfragen
// ausserhalb der Saison aktuell bewusst "false" zurueck.
//
// HINWEIS: Die vom Landkreis genannten Eckdaten 04.05.2026 und 30.10.2026
// sind selbst KEIN Mittwoch (04.05. ist ein Montag, 30.10. ein Freitag).
// Sie werden hier daher als Saison-*Zeitraum* interpretiert: der erste
// Abholtermin ist der erste Mittwoch ab Saisonstart (06.05.2026), der letzte
// der letzte Mittwoch vor/an Saisonende (28.10.2026). Bitte gegenpruefen,
// sobald die ICS-Datei vom Landkreis-Tool vorliegt.
const PickupDate kBioSaisonStart = {2026, 5, 4};
const PickupDate kBioSaisonEnde = {2026, 10, 30};
const int kBioWochentag = 3; // struct tm::tm_wday: 0=Sonntag ... 3=Mittwoch

// --- Zeit-Hilfsfunktionen ----------------------------------------------------

struct tm toTm(const PickupDate &date) {
    struct tm t = {};
    t.tm_year = date.year - 1900;
    t.tm_mon = date.month - 1;
    t.tm_mday = date.day;
    t.tm_isdst = -1; // DST von der C-Library ermitteln lassen
    return t;
}

PickupDate fromTime(time_t t) {
    struct tm out;
    localtime_r(&t, &out);
    PickupDate d;
    d.year = out.tm_year + 1900;
    d.month = out.tm_mon + 1;
    d.day = out.tm_mday;
    return d;
}

// Rundet einen Zeitpunkt auf Mitternacht (lokale Zeit) desselben Tages ab.
time_t toMidnight(time_t t) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    tmv.tm_isdst = -1;
    return mktime(&tmv);
}

// Naechster Tag mit gegebenem Wochentag auf/nach "from" (inklusive "from").
time_t nextWeekday(time_t from, int targetWday) {
    struct tm t;
    localtime_r(&from, &t);
    int diff = (targetWday - t.tm_wday + 7) % 7;
    t.tm_mday += diff;
    t.tm_isdst = -1;
    return mktime(&t);
}

bool nextRestmuell(time_t nowMidnight, PickupDate &out) {
    for (size_t i = 0; i < kRestmuellCount; ++i) {
        time_t candidate = pickupDateToMidnight(kRestmuell2026[i]);
        if (candidate >= nowMidnight) {
            out = kRestmuell2026[i];
            return true;
        }
    }
    return false; // Terminliste 2026 erschoepft - fuer Folgejahr nachpflegen
}

bool nextBioabfall(time_t nowMidnight, PickupDate &out) {
    time_t saisonStart = pickupDateToMidnight(kBioSaisonStart);
    time_t saisonEnde = pickupDateToMidnight(kBioSaisonEnde);

    time_t searchFrom = nowMidnight > saisonStart ? nowMidnight : saisonStart;
    time_t candidate = nextWeekday(searchFrom, kBioWochentag);

    if (candidate < saisonStart) {
        candidate = nextWeekday(saisonStart, kBioWochentag);
    }
    if (candidate > saisonEnde) {
        return false; // ausserhalb der Saison - Winterturnus noch offen (TODO)
    }
    out = fromTime(candidate);
    return true;
}

} // namespace

const char *binTypeName(BinType type) {
    switch (type) {
        case BinType::Restmuell: return "Restabfall";
        case BinType::Bioabfall: return "Bioabfall";
        case BinType::GelbeTonne: return "Gelbe Tonne";
        case BinType::BlaueTonne: return "Blaue Tonne";
        default: return "?";
    }
}

bool binTypeHasData(BinType type) {
    switch (type) {
        case BinType::Restmuell: return true;
        case BinType::Bioabfall: return true; // nur Sommersaison, siehe getNextPickup()
        case BinType::GelbeTonne: return false;  // TODO: Termine noch offen
        case BinType::BlaueTonne: return false;  // TODO: Termine noch offen
        default: return false;
    }
}

time_t pickupDateToMidnight(const PickupDate &date) {
    struct tm t = toTm(date);
    return mktime(&t);
}

bool getNextPickup(BinType type, time_t now, PickupDate &outDate) {
    time_t nowMidnight = toMidnight(now);
    switch (type) {
        case BinType::Restmuell: return nextRestmuell(nowMidnight, outDate);
        case BinType::Bioabfall: return nextBioabfall(nowMidnight, outDate);
        case BinType::GelbeTonne:
        case BinType::BlaueTonne:
        default:
            return false; // TODO: Termine noch offen (siehe Kommentar oben)
    }
}

int daysUntil(time_t now, const PickupDate &date) {
    time_t nowMidnight = toMidnight(now);
    time_t dateMidnight = pickupDateToMidnight(date);
    double diffSeconds = difftime(dateMidnight, nowMidnight);
    return (int)(diffSeconds / 86400.0 + 0.5);
}
