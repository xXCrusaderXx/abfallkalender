// NTP-Zeitsynchronisation.
//
// Nach dem ersten erfolgreichen Sync laeuft die Zeit ueber die interne
// ESP32-RTC weiter, auch wenn das WLAN danach ausfaellt - fuer eine reine
// Anzeige ist der Drift des internen Timers ausreichend genau, ein staendiger
// Netzwerk-Zwang besteht also nicht.
#pragma once
#include <Arduino.h>
#include <time.h>

// WLAN verbinden und einmalig NTP-Sync versuchen. Blockiert kurzzeitig
// (max. ca. 25s), wird daher einmalig in setup() aufgerufen.
void timeSyncBegin();

// Regelmaessig aus der Arduino loop() aufrufen. Solange noch kein Sync
// gelungen ist, wird alle NTP_RETRY_INTERVAL_MS ein erneuter Versuch
// unternommen (blockiert dabei ebenfalls kurzzeitig). Nach erfolgreichem
// Sync tut diese Funktion nichts mehr.
void timeSyncLoop();

// true, sobald die Zeit mindestens einmal erfolgreich per NTP gesetzt wurde.
bool timeSyncIsValid();

// Aktuelle lokale Zeit (Europe/Berlin). Vor dem ersten erfolgreichen Sync
// liefert dies KEINE gueltige Kalenderzeit - vorher immer timeSyncIsValid()
// pruefen.
time_t timeSyncNow();
