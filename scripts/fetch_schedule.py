#!/usr/bin/env python3
"""Laedt die echten Abfuhrtermine fuer Arnsdorf (Kreis Bautzen) von
app.abfallplus.de - demselben Backend, das die offizielle "Abfall LK BZ"-App
nutzt - und schreibt sie nach data/schedule.json.

Wird von .github/workflows/update-schedule.yml periodisch ausgefuehrt, damit
der ESP32-Muellkalender (src/waste_schedule.cpp) die Termine per HTTPS-Fetch
aktuell haelt, ohne dass jaehrlich der PDF-Kalender manuell abgetippt und neu
geflasht werden muss.

Die API liefert immer nur Termine ab "heute" (siehe Kommentar bei DATE_HORIZON
unten) - ein taeglicher Lauf haelt den Horizont daher ausreichend gefuellt.
"""
from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path

from app_abfallplus import AppAbfallplusDe

APP_ID = "de.k4systems.abfalllkbz"
APP_DISPLAY_NAME = "Abfall LK BZ"
KOMMUNE = "Arnsdorf"

OUTPUT_PATH = Path(__file__).resolve().parent.parent / "data" / "schedule.json"

# Reihenfolge/Namen muessen zu BinType in src/waste_schedule.h passen.
# Zuordnung per Teilstring (case-insensitive), da sich die exakte
# API-Schreibweise ("Restabfalltonne" vs. "Restmuelltonne" o.ae.) von Jahr zu
# Jahr unterscheiden kann - siehe auch ICON_MAP im Original-Projekt
# (mampfes/hacs_waste_collection_schedule), das denselben Ansatz nutzt.
CATEGORY_MAP = {
    "restmuell": ("rest",),
    "bioabfall": ("bio",),
    "gelbe_tonne": ("gelb",),
    "blaue_tonne": ("blau", "papier"),
}


def map_category(name: str) -> str | None:
    lower = name.lower()
    for bin_key, needles in CATEGORY_MAP.items():
        if any(needle in lower for needle in needles):
            return bin_key
    return None  # z.B. "Schadstoffe" (Giftmobil) - hat keine Kachel auf dem Display


def main() -> int:
    app = AppAbfallplusDe(app_id=APP_ID, kommune=KOMMUNE, app_display_name=APP_DISPLAY_NAME)
    collections = app.generate_calendar()

    bins: dict[str, set[str]] = {key: set() for key in CATEGORY_MAP}
    unmapped: set[str] = set()
    for entry in collections:
        bin_key = map_category(entry["category"])
        if bin_key is None:
            unmapped.add(entry["category"])
            continue
        bins[bin_key].add(entry["date"].isoformat())

    if unmapped:
        print(f"Hinweis: nicht zugeordnete Kategorien (ignoriert): {sorted(unmapped)}", file=sys.stderr)

    empty_bins = [key for key, dates in bins.items() if not dates]
    if empty_bins:
        print(f"FEHLER: keine Termine fuer: {empty_bins} - breche ab, alte schedule.json bleibt erhalten.", file=sys.stderr)
        return 1

    output = {
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "source": f"abfallplus.de (app_id={APP_ID}, Kommune={KOMMUNE})",
        "bins": {key: sorted(dates) for key, dates in bins.items()},
    }

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    total = sum(len(d) for d in bins.values())
    print(f"OK: {total} Termine nach {OUTPUT_PATH} geschrieben.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
