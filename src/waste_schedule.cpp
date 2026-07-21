#include "waste_schedule.h"

namespace {

// Alle Termine in diesem Abschnitt stammen aus dem offiziellen Abfallkalender
// 2026 des Landkreises Bautzen (Tabelle "Arnsdorf - Bautzen 3", Seite 10):
// https://www.landkreis-bautzen.de/download/Abfallamt/Abfallkalender_2026.pdf
// Enthaelt bereits alle feiertagsbedingten Verschiebungen.

// --- Restabfall (Donnerstag) -------------------------------------------------
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

// --- Bioabfall: Wintertermine (ausserhalb der Sommersaison) -----------------
// Lt. Kalender 14-taegig mit unregelmaessigen Abstaenden (Tourenplanung),
// daher als explizite Liste statt als Regel gefuehrt.
const PickupDate kBioWinter2026[] = {
    {2026, 1, 7},   {2026, 1, 21},  {2026, 2, 4},   {2026, 2, 18},
    {2026, 3, 4},   {2026, 3, 18},  {2026, 4, 1},   {2026, 4, 15},
    {2026, 4, 29},  {2026, 11, 11}, {2026, 11, 25}, {2026, 12, 9},
    {2026, 12, 23},
};
const size_t kBioWinterCount = sizeof(kBioWinter2026) / sizeof(kBioWinter2026[0]);

// --- Bioabfall: Sommersaison, woechentlich mittwochs ------------------------
// HINWEIS: Die vom Landkreis genannten Eckdaten 04.05.2026 und 30.10.2026
// sind selbst KEIN Mittwoch (04.05. ist ein Montag, 30.10. ein Freitag).
// Sie werden hier daher als Saison-*Zeitraum* interpretiert: der erste
// Abholtermin ist der erste Mittwoch ab Saisonstart (06.05.2026), der letzte
// der letzte Mittwoch vor/an Saisonende (28.10.2026) - passend zur Kalender-
// Angabe "vom 04.05. bis 30.10.2026 woechentliche Bioentsorgung: Mittwoch".
const PickupDate kBioSaisonStart = {2026, 5, 4};
const PickupDate kBioSaisonEnde = {2026, 10, 30};
const int kBioWochentag = 3; // struct tm::tm_wday: 0=Sonntag ... 3=Mittwoch

// --- Gelbe Tonne (Verpackungen) ----------------------------------------------
const PickupDate kGelbeTonne2026[] = {
    {2026, 1, 5},   {2026, 1, 19},  {2026, 2, 2},   {2026, 2, 16},
    {2026, 3, 2},   {2026, 3, 16},  {2026, 3, 30},  {2026, 4, 15},
    {2026, 4, 29},  {2026, 5, 15},  {2026, 6, 1},   {2026, 6, 15},
    {2026, 6, 29},  {2026, 7, 13},  {2026, 7, 27},  {2026, 8, 10},
    {2026, 8, 24},  {2026, 9, 7},   {2026, 9, 21},  {2026, 10, 5},
    {2026, 10, 19}, {2026, 11, 2},  {2026, 11, 16}, {2026, 12, 1},
    {2026, 12, 15}, {2026, 12, 30},
};
const size_t kGelbeTonneCount = sizeof(kGelbeTonne2026) / sizeof(kGelbeTonne2026[0]);

// --- Blaue Tonne (Papier) -----------------------------------------------------
// Fast monatlich; 21.11. ist im Original fett gedruckt = feiertagsbedingt
// verschoben (siehe Legende im Kalender-PDF).
const PickupDate kBlaueTonne2026[] = {
    {2026, 1, 16},  {2026, 2, 13},  {2026, 3, 13},  {2026, 4, 11},
    {2026, 5, 8},   {2026, 6, 5},   {2026, 7, 3},   {2026, 7, 31},
    {2026, 8, 28},  {2026, 9, 25},  {2026, 10, 23}, {2026, 11, 21},
    {2026, 12, 18},
};
const size_t kBlaueTonneCount = sizeof(kBlaueTonne2026) / sizeof(kBlaueTonne2026[0]);

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

// Liefert den ersten Termin >= nowMidnight aus einer aufsteigend sortierten
// Terminliste. Gemeinsame Suchlogik fuer Restmuell/Gelbe Tonne/Blaue Tonne.
bool nextFromList(const PickupDate *list, size_t count, time_t nowMidnight, PickupDate &out) {
    for (size_t i = 0; i < count; ++i) {
        time_t candidate = pickupDateToMidnight(list[i]);
        if (candidate >= nowMidnight) {
            out = list[i];
            return true;
        }
    }
    return false; // Terminliste 2026 erschoepft - fuer Folgejahr nachpflegen
}

bool nextBioSommer(time_t nowMidnight, PickupDate &out) {
    time_t saisonStart = pickupDateToMidnight(kBioSaisonStart);
    time_t saisonEnde = pickupDateToMidnight(kBioSaisonEnde);

    time_t searchFrom = nowMidnight > saisonStart ? nowMidnight : saisonStart;
    time_t candidate = nextWeekday(searchFrom, kBioWochentag);

    if (candidate < saisonStart) {
        candidate = nextWeekday(saisonStart, kBioWochentag);
    }
    if (candidate > saisonEnde) {
        return false; // ausserhalb der Sommersaison
    }
    out = fromTime(candidate);
    return true;
}

// Bioabfall kombiniert die feste Wintertermin-Liste mit der Sommersaison-Regel
// und liefert von beiden den zeitlich naeheren Termin.
bool nextBioabfall(time_t nowMidnight, PickupDate &out) {
    PickupDate winterCandidate;
    bool hasWinter = nextFromList(kBioWinter2026, kBioWinterCount, nowMidnight, winterCandidate);

    PickupDate sommerCandidate;
    bool hasSommer = nextBioSommer(nowMidnight, sommerCandidate);

    if (hasWinter && hasSommer) {
        time_t w = pickupDateToMidnight(winterCandidate);
        time_t s = pickupDateToMidnight(sommerCandidate);
        out = (w <= s) ? winterCandidate : sommerCandidate;
        return true;
    }
    if (hasWinter) {
        out = winterCandidate;
        return true;
    }
    if (hasSommer) {
        out = sommerCandidate;
        return true;
    }
    return false; // beide Terminlisten erschoepft - fuer Folgejahr nachpflegen
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
        case BinType::Restmuell:
        case BinType::Bioabfall:
        case BinType::GelbeTonne:
        case BinType::BlaueTonne:
            return true;
        default:
            return false;
    }
}

time_t pickupDateToMidnight(const PickupDate &date) {
    struct tm t = toTm(date);
    return mktime(&t);
}

bool getNextPickup(BinType type, time_t now, PickupDate &outDate) {
    time_t nowMidnight = toMidnight(now);
    switch (type) {
        case BinType::Restmuell:
            return nextFromList(kRestmuell2026, kRestmuellCount, nowMidnight, outDate);
        case BinType::Bioabfall:
            return nextBioabfall(nowMidnight, outDate);
        case BinType::GelbeTonne:
            return nextFromList(kGelbeTonne2026, kGelbeTonneCount, nowMidnight, outDate);
        case BinType::BlaueTonne:
            return nextFromList(kBlaueTonne2026, kBlaueTonneCount, nowMidnight, outDate);
        default:
            return false;
    }
}

int daysUntil(time_t now, const PickupDate &date) {
    time_t nowMidnight = toMidnight(now);
    time_t dateMidnight = pickupDateToMidnight(date);
    double diffSeconds = difftime(dateMidnight, nowMidnight);
    return (int)(diffSeconds / 86400.0 + 0.5);
}
