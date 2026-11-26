# PokitSuite – Deutsche Dokumentation

PokitSuite ist eine native Windows-Anwendung für Pokit-Pro-Messgeräte. Sie bietet ein automatisch startendes Digitalmultimeter, ein an klassische Tischoszilloskope angelehntes DSO, berechnete Signalwerte und eine MCP-Schnittstelle für KI-gestützte Mess- und Arduino-Abläufe.

PokitSuite wurde mit wesentlicher Unterstützung von **OpenAI Codex** entwickelt und am realen Pokit-Pro-Gerät getestet.

## Funktionsstand

- **Implementiert:** automatische Suche und unabhängiger Wiederaufbau für zwei Pokit Pro; zweikanaliges Multimeter mit 0,5/1 s Intervall und zwei Verlaufskurven; zweikanaliges Oszilloskop mit CH1/CH2, Trigger, Kopplung, V/div, Zeit/div, Einzelaufnahme und Signalmetriken. Live-Spannungsmessung und DSO-Aufnahme wurden mit beiden Geräten getestet.
- **Nativer Funktrigger, live getestet:** CH1 oder CH2 als Triggerquelle, bestätigtes Scharfstellen des Empfängers und gemeinsame Anzeige vollständiger Aufnahmepaare mit übereinstimmender Aufnahmenummer. Der tatsächliche Kanalversatz ist noch zu vermessen.
- **Implementiert und protokollgetestet:** MCP-Server mit 17 Werkzeugen. Für Livewerte müssen PokitSuite und das Gerät laufen.
- **Experimentell:** automatische Arduino-Skizzenbearbeitung, Kompilierung, Upload und erneute Messung; alte Weboberfläche.
- **Geplant:** vier echte Messkanäle, native Langzeit-Logger-Oberfläche, signierter Installer, breitere Hardwaretests.

## Installation

1. Das ZIP-Archiv der [aktuellen Version](https://github.com/robertbuss85-stack/PokitSuite/releases/latest) herunterladen und vollständig entpacken.
2. Bluetooth einschalten und das Pokit Pro aufwecken.
3. `PokitSuiteNative.exe` starten. Nach der Verbindung startet der aktuell sichtbare Messmodus selbstständig.

Vorausgesetzt werden Windows 10/11 x64 und Bluetooth Low Energy. Ausführliche Hinweise stehen in [INSTALL.md](INSTALL.md); die MCP-Konfiguration in [MCP.md](MCP.md).

## Bedienung

Beim Wechsel zwischen Multimeter und Oszilloskop wird der bisherige Modus gestoppt und der neue automatisch gestartet. Im Oszilloskop verändert das Mausrad über dem Diagramm die horizontale Zeitbasis. `Run/Stop` schaltet die fortlaufende Erfassung, `Single` löst eine Einzelaufnahme aus.

Für den Funktrigger unter **TRIGGER → Quelle** entweder **CH1 · Funk** oder **CH2 · Funk** wählen. Danach **Sofort**, **Steigend** oder **Fallend** und den gewünschten Pegel einstellen und **SINGLE** drücken. Die Funkaufnahme liefert 1.000 Samples pro Kanal ab **100 µs/div**. Beide Kurven werden anhand ihrer geräteseitigen Triggerposition und Abtastrate gemeinsam angezeigt. Wiederholte Aufnahmen mit beiden Triggerquellen, Flankentrigger und STOP mit erneutem Scharfstellen wurden live geprüft. Die Messung von Versatz und Jitter mit einem gemeinsamen Referenzsignal sowie die Prüfung schneller A/B-Drehgebersignale stehen noch aus.

Mit **PDF + Werte speichern** wird die aktuelle vollständige Aufnahme unter `Dokumente/PokitSuite/Captures` abgelegt: als PDF-Messbericht mit Oszilloskopbild und Kennwerttabelle, als CSV-Wertetabelle mit Zeit- und Spannungswerten sowie als JSON-Aufnahme für PokitSuite. Gespeicherte Aufnahmen können in der Capture-Liste ausgewählt und als cyanfarbene Vergleichskurve über die gelbe Live-Kurve gelegt werden.

Die Frequenz- und Tastgradmessung setzt ein ausreichend sauberes, periodisches Signal voraus. Bei starkem Rauschen, Clipping oder weniger als einer vollständigen Periode können Werte fehlen oder ungenau sein.

## Sicherheit

Das MCP-Werkzeug kann Arduino-Quellcode verändern und Firmware übertragen. Änderungen werden gesichert, ersetzen aber keine Prüfung des Diffs und keine elektrische Schutzmaßnahme. Messbereiche, Massebezug und zulässige Eingangsspannungen des Pokit-Geräts sind einzuhalten.

