#include "waste_schedule.h"
#include "config.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <vector>

namespace {

const char *kCacheFile = "/schedule.json";

// Ein Eintrag pro Tonnenart, aufsteigend sortiert (garantiert durch
// scripts/fetch_schedule.py) - getNextPickup() verlaesst sich darauf.
std::vector<PickupDate> g_schedule[(size_t)BinType::COUNT];

unsigned long g_lastFetchAttemptMs = 0;
bool g_littleFsMounted = false;

struct BinKey {
    const char *jsonKey; // muss zu scripts/fetch_schedule.py (CATEGORY_MAP) passen
    BinType type;
};

const BinKey kBinKeys[] = {
    {"restmuell", BinType::Restmuell},
    {"bioabfall", BinType::Bioabfall},
    {"gelbe_tonne", BinType::GelbeTonne},
    {"blaue_tonne", BinType::BlaueTonne},
};

bool parsePickupDate(const char *iso, PickupDate &out) {
    if (!iso) return false;
    unsigned int year, month, day;
    if (sscanf(iso, "%4u-%2u-%2u", &year, &month, &day) != 3) return false;
    out.year = (uint16_t)year;
    out.month = (uint8_t)month;
    out.day = (uint8_t)day;
    return true;
}

// Parst schedule.json (Format siehe scripts/fetch_schedule.py) und ersetzt
// g_schedule nur bei vollstaendigem Erfolg - bei ungueltigem JSON oder
// fehlendem "bins"-Feld bleiben die zuletzt bekannten Termine unangetastet.
bool parseScheduleJson(const String &json) {
    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("schedule.json: JSON-Fehler: %s\n", err.c_str());
        return false;
    }
    JsonObject bins = doc["bins"];
    if (bins.isNull()) {
        Serial.println("schedule.json: Feld 'bins' fehlt.");
        return false;
    }

    std::vector<PickupDate> parsed[(size_t)BinType::COUNT];
    for (const BinKey &key : kBinKeys) {
        JsonArray dates = bins[key.jsonKey];
        if (dates.isNull()) continue; // Bin fehlt im JSON - bleibt leer, UI zeigt Platzhalter
        for (JsonVariant v : dates) {
            PickupDate date;
            if (parsePickupDate(v.as<const char *>(), date)) {
                parsed[(size_t)key.type].push_back(date);
            }
        }
    }

    for (size_t i = 0; i < (size_t)BinType::COUNT; ++i) {
        g_schedule[i] = std::move(parsed[i]);
    }
    return true;
}

void ensureLittleFs() {
    if (g_littleFsMounted) return;
    // true = bei korruptem Dateisystem automatisch formatieren. Unkritisch,
    // da hier nur ein Cache liegt - ein Netzwerk-Refresh heilt das wieder.
    g_littleFsMounted = LittleFS.begin(true);
    if (!g_littleFsMounted) {
        Serial.println("LittleFS konnte nicht gemountet werden - Termin-Cache deaktiviert.");
    }
}

void loadCache() {
    ensureLittleFs();
    if (!g_littleFsMounted) return;
    File f = LittleFS.open(kCacheFile, "r");
    if (!f) return; // noch kein Cache vorhanden (erster Boot)
    String content = f.readString();
    f.close();
    if (parseScheduleJson(content)) {
        Serial.println("Abfuhrtermine aus Cache geladen.");
    }
}

void saveCache(const String &json) {
    ensureLittleFs();
    if (!g_littleFsMounted) return;
    File f = LittleFS.open(kCacheFile, "w");
    if (!f) {
        Serial.println("Termin-Cache konnte nicht geschrieben werden.");
        return;
    }
    f.print(json);
    f.close();
}

bool fetchFromNetwork() {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure client;
    // Kein Zertifikats-Pinning: schedule.json ist eine oeffentliche, nicht
    // sicherheitsrelevante Datei (nur Abfuhrtermine) - der Wartungsaufwand
    // fuer Pinning/Root-CA-Rotation steht in keinem Verhaeltnis zum Risiko.
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, SCHEDULE_JSON_URL)) {
        Serial.println("Schedule-Fetch: HTTPClient::begin() fehlgeschlagen.");
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("Schedule-Fetch fehlgeschlagen, HTTP %d\n", code);
        http.end();
        return false;
    }
    String payload = http.getString();
    http.end();

    if (!parseScheduleJson(payload)) {
        return false;
    }
    saveCache(payload);
    Serial.println("Abfuhrtermine per HTTPS aktualisiert.");
    return true;
}

time_t toMidnight(time_t t) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    tmv.tm_isdst = -1;
    return mktime(&tmv);
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
    if (type >= BinType::COUNT) return false;
    return !g_schedule[(size_t)type].empty();
}

time_t pickupDateToMidnight(const PickupDate &date) {
    struct tm t = {};
    t.tm_year = date.year - 1900;
    t.tm_mon = date.month - 1;
    t.tm_mday = date.day;
    t.tm_isdst = -1; // DST von der C-Library ermitteln lassen
    return mktime(&t);
}

bool getNextPickup(BinType type, time_t now, PickupDate &outDate) {
    if (type >= BinType::COUNT) return false;
    time_t nowMidnight = toMidnight(now);
    for (const PickupDate &candidate : g_schedule[(size_t)type]) {
        if (pickupDateToMidnight(candidate) >= nowMidnight) {
            outDate = candidate;
            return true;
        }
    }
    return false; // Terminliste erschoepft oder (noch) keine Daten geladen
}

int daysUntil(time_t now, const PickupDate &date) {
    time_t nowMidnight = toMidnight(now);
    time_t dateMidnight = pickupDateToMidnight(date);
    double diffSeconds = difftime(dateMidnight, nowMidnight);
    return (int)(diffSeconds / 86400.0 + 0.5);
}

void scheduleBegin() {
    loadCache();
    fetchFromNetwork(); // best effort - Cache/leere Daten bleiben bei Fehlschlag erhalten
    g_lastFetchAttemptMs = millis();
}

void scheduleLoop() {
    unsigned long nowMs = millis();
    if (nowMs - g_lastFetchAttemptMs < SCHEDULE_FETCH_INTERVAL_MS) return;
    g_lastFetchAttemptMs = nowMs;
    fetchFromNetwork();
}
