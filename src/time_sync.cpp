#include "time_sync.h"
#include <WiFi.h>
#include "config.h"
#include "secrets.h"

namespace {

bool syncValid = false;
unsigned long lastAttemptMs = 0;
const unsigned long kWifiConnectTimeoutMs = 15000;
const unsigned long kNtpWaitTimeoutMs = 10000;

// Verbindet (falls noetig) mit dem WLAN und versucht einen NTP-Sync.
// Blockierend, aber zeitlich begrenzt - siehe Kommentare in time_sync.h.
void attemptSync() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs) {
            delay(100);
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        return; // Kein WLAN erreichbar - naechster Versuch folgt spaeter
    }

    configTzTime(TIME_ZONE_POSIX, NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeInfo;
    if (getLocalTime(&timeInfo, kNtpWaitTimeoutMs)) {
        syncValid = true;
    }
}

} // namespace

void timeSyncBegin() {
    attemptSync();
    lastAttemptMs = millis();
}

void timeSyncLoop() {
    if (syncValid) {
        return; // Interne RTC laeuft ausreichend genau weiter, siehe time_sync.h
    }
    if (millis() - lastAttemptMs < NTP_RETRY_INTERVAL_MS) {
        return;
    }
    lastAttemptMs = millis();
    attemptSync();
}

bool timeSyncIsValid() {
    return syncValid;
}

time_t timeSyncNow() {
    time_t now;
    time(&now);
    return now;
}
