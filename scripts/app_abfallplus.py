# Schlanker Client fuer die Abfall+/k4systems-App-Plattform (app.abfallplus.de).
#
# Adaptiert (stark gekuerzt auf den hier benoetigten Ablauf: Kommune waehlen,
# Termine laden) aus dem Home-Assistant-Projekt
# https://github.com/mampfes/hacs_waste_collection_schedule
# (service/AppAbfallplusDe.py), MIT-Lizenz:
#
#   MIT License
#   Copyright (c) 2020 Steffen Zimmermann
#
# Das Original unterstuetzt >100 Abfall+-Apps generisch inkl. Bezirk/Strasse/
# Hausnummer-Aufloesung; hier bleibt nur das, was fuer den Ablauf
# app_id + Kommune -> Terminliste gebraucht wird.
from __future__ import annotations

import json
import re
import time
import uuid
from collections import OrderedDict
from datetime import date, datetime
from urllib.parse import unquote

import requests
from bs4 import BeautifulSoup, Tag

API_BASE = "https://app.abfallplus.de/{}"
API_ASSISTANT = API_BASE.format("assistent/{}")
USER_AGENT_ASSISTANT = (
    "Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/114.0.0.0 Safari/537.36 Abfallwecker"
)


def _user_agent(app_name: str) -> str:
    return f"Android / {app_name} 8.1.1 (1915081010) / DM=unknown;DT=vbox86p;SN=Google;SV=8.1.0 (27);MF=unknown"


def extract_onclicks(data: BeautifulSoup | str | requests.Response) -> list[list]:
    if isinstance(data, requests.Response):
        data = data.text
    if isinstance(data, str):
        data = BeautifulSoup(data, features="html.parser")

    to_return = []
    for a in data.find_all("a"):
        onclick: str = a.attrs["onclick"].replace("('#f_ueberspringen').val('0')", "")
        start = onclick.find("(") + 1
        end = onclick.find("})") + 1
        if end == 0:
            end = onclick.find(')"')
        string = (
            ("[" + onclick[start:end] + "]")
            .replace('"', '\\"')
            .replace("'", '"')
            .replace("\t", "")
            .replace("\r\n", "")
            .replace("\n", "")
        )
        try:
            to_return.append(json.loads(string))
        except json.decoder.JSONDecodeError:
            raise Exception(f"Failed to parse '{string}', onclick: '{onclick}'") from None
    return to_return


def compare(a: str, b: str) -> bool:
    return a.lower().strip() == b.lower().strip()


class AppAbfallplusDe:
    """Fuehrt den Kommune-Auswahl-Assistenten von app.abfallplus.de aus und
    laedt anschliessend die Abholtermine. Nur der Pfad "Kommune direkt
    eindeutig" wird unterstuetzt (kein Bezirk/Strasse/Hausnummer noetig) -
    das reicht fuer Arnsdorf/Kreis Bautzen. Falls eine andere Kommune
    zusaetzlich Bezirk/Strasse braucht, wirft select_kommune/get_collections
    eine aussagekraeftige Exception statt stillschweigend falsche Daten zu
    liefern.
    """

    def __init__(self, app_id: str, kommune: str, app_display_name: str):
        self._client = str(uuid.uuid4())
        self._app_id = app_id
        self._app_display_name = app_display_name
        self._session = requests.Session()
        self._kommune_search = kommune

        self._bundesland_id = None
        self._landkreis_id = None
        self._kommune_id = None
        self._f_id_strasse = None
        self._hnr = None

    def _request(self, url_ending, base=API_ASSISTANT, data=None, method="post"):
        headers = OrderedDict({})
        if base == API_ASSISTANT:
            headers["User-Agent"] = USER_AGENT_ASSISTANT
            headers["Accept"] = "*/*"
            headers["Origin"] = "https://app.abfallplus.de"
            headers["X-Requested-With"] = "XMLHttpRequest"
            headers["Content-Type"] = "application/x-www-form-urlencoded; charset=UTF-8"
            headers["Referer"] = "https://app.abfallplus.de/login/"
        else:
            headers["User-Agent"] = _user_agent(self._app_display_name)
            headers["x-abfallplus-client"] = self._client
            headers["x-abfallplus-appid"] = self._app_id

        if "config.xml" not in url_ending:
            time.sleep(1)  # wie im Original: die Assistent-API mag keine Bursts

        if method == "get":
            return self._session.get(base.format(url_ending), headers=headers)
        req = requests.Request(
            method="POST",
            url=base.format(url_ending),
            data=data,
            headers=headers,
            cookies=self._session.cookies,
        )
        return self._session.send(req.prepare())

    def init_connection(self) -> None:
        data = {"client": self._client, "app_id": self._app_id}
        self._request("config.xml", base=API_BASE, data=data).raise_for_status()
        r = self._request("login/", base=API_BASE, data=data)
        r.raise_for_status()
        soup = BeautifulSoup(r.text, features="html.parser")
        for input_tag in soup.find_all("input"):
            name = input_tag.attrs.get("name")
            if name == "f_id_bundesland":
                self._bundesland_id = input_tag.attrs["value"]
            elif name == "f_id_landkreis":
                self._landkreis_id = input_tag.attrs["value"]
            elif name == "f_id_kommune":
                self._kommune_id = input_tag.attrs["value"]

    def get_kommunen(self) -> list[dict]:
        data = {}
        if self._bundesland_id:
            data["id_bundesland"] = self._bundesland_id
        if self._landkreis_id:
            data["id_landkreis"] = self._landkreis_id
        r = self._request("kommune/", data=data)
        r.raise_for_status()
        kommunen = []
        for a in extract_onclicks(r):
            kommunen.append({"id": a[0], "name": a[1]})
        return kommunen

    def select_kommune(self) -> None:
        kommunen = self.get_kommunen()
        for kommune in kommunen:
            if compare(kommune["name"], self._kommune_search):
                self._kommune_id = kommune["id"]
                return
        raise Exception(
            f"Kommune '{self._kommune_search}' nicht gefunden. "
            f"Verfuegbar: {[k['name'] for k in kommunen]}"
        )

    def get_streets(self) -> list[dict]:
        data = {
            "id_landkreis": self._landkreis_id,
            "id_kommune": self._kommune_id,
            "id_kommune_qry": self._kommune_id,
        }
        r = self._request("strasse/", data=data)
        r.raise_for_status()
        streets = []
        for a in extract_onclicks(r):
            streets.append({"id": a[0], "name": a[1], "hnrs": a[3] != "fertig"})
        return streets

    def select_all_streets(self) -> None:
        """Waehlt "Alle Strassen" falls angeboten (wie in Arnsdorf ueblich),
        sonst bricht mit einer Liste der verfuegbaren Strassen ab - dann
        braucht dieses Script eine Bezirk/Strasse-Erweiterung (nicht
        implementiert, da fuer die aktuelle Adresse nicht noetig)."""
        streets = self.get_streets()
        for street in streets:
            if compare(street["name"], "Alle Strassen") or compare(street["name"], "Alle Straßen"):
                self._f_id_strasse = street["id"]
                return
        if len(streets) == 1:
            self._f_id_strasse = streets[0]["id"]
            return
        raise Exception(
            "Mehrere Strassen gefunden, aber kein 'Alle Strassen' verfuegbar - "
            f"Script muesste um Strassenauswahl erweitert werden: {[s['name'] for s in streets]}"
        )

    def select_all_waste_types(self) -> None:
        data = {
            "f_id_bundesland": self._bundesland_id,
            "f_id_landkreis": self._landkreis_id,
            "f_id_kommune": self._kommune_id,
            "f_id_bezirk": "",
            "f_id_strasse": self._f_id_strasse,
            "f_hnr": self._hnr,
            "f_kdnr": "",
        }
        r = self._request("abfallarten/", data=data)
        r.raise_for_status()
        soup = BeautifulSoup(r.text, features="html.parser")
        self._f_id_abfallart = []
        for input_tag in soup.find_all("input", {"name": "f_id_abfallart[]"}):
            if input_tag.attrs["value"] == "0":
                if "id" in input_tag.attrs:
                    self._f_id_abfallart.append(input_tag.attrs["id"].split("_")[-1])
                continue
            self._f_id_abfallart.append(input_tag.attrs["value"])
        self._f_id_abfallart = list(set(self._f_id_abfallart))

    def validate(self) -> None:
        data = {
            "f_id_bundesland": self._bundesland_id,
            "f_id_landkreis": self._landkreis_id,
            "f_id_kommune": self._kommune_id,
            "f_id_bezirk": "",
            "f_id_strasse": self._f_id_strasse,
            "f_hnr": self._hnr,
            "f_kdnr": "",
            "f_id_abfallart[]": self._f_id_abfallart,
            "f_uhrzeit_tag": "86400|0",
            "f_uhrzeit_stunden": 54000,
            "f_uhrzeit_minuten": 600,
            "f_anonym": 1,
            "f_ausgangspunkt": 1,
            "f_ueberspringen": 0,
        }
        r = self._request("ueberpruefen/", data=data)
        r.raise_for_status()
        data["f_datenschutz"] = datetime.now().strftime("%Y%m%d%H%M%S")
        r = self._request("finish/", data=data)
        r.raise_for_status()

    def get_collections(self) -> list[dict[str, "date | str"]]:
        self._request("version.xml", base=API_BASE, data={"client": self._client, "app_id": self._app_id})
        self._request(
            "version.xml",
            base=API_BASE,
            data={"client": self._client, "app_id": self._app_id, "renew": 1},
        )
        r = self._request(
            "struktur.xml.zip", base=API_BASE, data={"client": self._client, "app_id": self._app_id}
        )
        r.raise_for_status()

        soup = BeautifulSoup(r.text, "xml")
        soup_categories = soup.find("key", text="categories")
        if not soup_categories:
            raise Exception("Keine 'categories' in der Antwort gefunden.")
        soup_array = soup_categories.find_next_sibling("array")
        if not soup_array or not isinstance(soup_array, Tag):
            raise Exception("Kein Array unter 'categories' gefunden.")

        categories = {}
        for category in soup_array.find_all("dict"):
            cat_id = category.find("key", text="id").find_next_sibling("string").text
            name = (
                category.find("key", text="name")
                .find_next_sibling("string")
                .text.replace("![CDATA[", "")
                .replace("]]", "")
                .strip()
            )
            categories[cat_id] = name

        collections: list[dict] = []
        for collection in soup.find("key", text="dates").find_next_sibling("array").find_all("dict"):
            category_id = collection.find("key", text="category_id").find_next_sibling("string").text
            pickup_date_str = collection.find("key", text="pickup_date").find_next_sibling("string").text
            pickup_date = datetime.strptime(pickup_date_str, "%Y-%m-%dT%H:%M:%S%z").date()
            collections.append({"category": categories[category_id], "date": pickup_date})
        return collections

    def generate_calendar(self) -> list[dict[str, "date | str"]]:
        self.init_connection()
        self.select_kommune()
        self.select_all_streets()
        self.select_all_waste_types()
        self.validate()
        return self.get_collections()
