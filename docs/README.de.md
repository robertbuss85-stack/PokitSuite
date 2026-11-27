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

**Strom im Oszilloskop:** Unter **VERTICAL → Kanal einstellen** den gewünschten Kanal und **Messgröße → Strom** wählen. Die Skala wechselt auf A/div, mA/div oder µA/div; Messlinien und Kennwerte verwenden Ampere. Der Pokit-Schalter muss zur Strommessung passen (kleiner Strom/Multimodus oder hoher Strom). Bei falscher oder unbekannter Stellung wartet die Erfassung. Strom in Reihe messen und vor dem Umbau den Stromkreis abschalten; Herstellergrenzen beachten. Strom und Spannung lassen sich auf zwei Kanälen kombinieren und gemeinsam exportieren. [Details und geprüfte Funktionen](SCOPE_CURRENT.md).

Beim Wechsel zwischen Multimeter und Oszilloskop wird der bisherige Modus gestoppt und der neue automatisch gestartet. Im Oszilloskop verändert das Mausrad über dem Diagramm die horizontale Zeitbasis. `Run/Stop` schaltet die fortlaufende Erfassung, `Single` löst eine Einzelaufnahme aus.

Die **CH1–CH4-Tasten** im Oszilloskop schalten die jeweilige Kurve ein oder aus. Verfügbare Kanäle sind bedienbar; CH3/CH4 bleiben bis zur Geräteintegration deaktiviert. Unter **VERTICAL → Kanal einstellen** den Kanal wählen und **Volt/Div** sowie **Position** getrennt einstellen. Jeder Kanal behält seine Einstellungen beim Ausblenden. Die Spannungslinien U1/U2 und ΔU beziehen sich auf den dort ausgewählten, an der Linie genannten Kanal. Die Zeitbasis und die Kopplung bleiben gemeinsam; beim Ausblenden läuft die Erfassung einschließlich Funktrigger weiter. Eine geladene Vergleichskurve nutzt weiterhin die CH1-Skalierung. Der Aufnahmeexport enthält alle Kanäle mit vollständigen Messdaten, auch ausgeblendete: eine gemeinsame Übersicht und eine Detailseite je Kanal mit eigener Skalierung, Vertikalposition und Kennwerten. JSON speichert die Wellenformen je Kanal; CSV enthält die Spalte `channel` und sämtliche zugehörigen Samples. Bei Funkaufnahmen sind die CSV-Zeiten relativ zum Trigger. Bei unabhängigen Aufnahmen wird keine Synchronität behauptet.

Im Multimeter bietet jeder Kanal **HOLD**, **REL**, **Reset**, **Min/Max/Mittelwert** und **Grenzen**. HOLD hält nur die Anzeige fest; REL zieht einen gespeicherten Referenzwert ab. Statistik, Grenzwertprüfung und CSV verwenden weiterhin absolute Messwerte. Reset setzt Statistik und HOLD/REL zurück. Beim Wechsel der Messgröße werden zusätzlich Verlauf und Grenzen zurückgesetzt.

Mit **AUFZEICHNEN** beginnt eine neue CSV-Datei unter `Dokumente/PokitSuite/Multimeter`; dieselbe Schaltfläche beendet sie. Die Datei enthält beide Kanäle, Zeitstempel, Einheiten und Kommentare. **PDF-Bericht** speichert die aktuelle Statistik mit den angezeigten Verläufen und letzten Ereignissen. **Ordner / CSV** öffnet den Speicherort. Kommentare mit **Markieren** hinzufügen; **Ereignisse** zeigt die letzten 100 Einträge. Grenzwertverletzungen werden rot angezeigt und bei Zustandswechseln protokolliert. Der Alarm ist optisch.

Zeitfenster, **+/−**, Mausrad und Pfeile steuern Zoom und Rückblick; **LIVE** folgt wieder den neuen Werten. Beide Kanäle haben unabhängige Spannung-/Werteskalen. Im Arbeitsspeicher bleiben die letzten 100.000 Werte je Kanal; die laufende CSV-Aufzeichnung enthält sämtliche seit ihrem Start aufgezeichneten Werte. Ältere CSV-Dateien lassen sich noch nicht in die App zurückladen. Grenzen werden in Basiseinheiten eingegeben: Strom in A, Kapazität in F. [Ausführliche Dokumentation auf Englisch](MULTIMETER.md).

Für den Funktrigger unter **TRIGGER → Quelle** entweder **CH1 · Funk** oder **CH2 · Funk** wählen. **Sofort** startet ohne Flankenbedingung. **RUN** erfasst fortlaufende Kurvenpaare, **STOP** hält an und **SINGLE** nimmt ein Paar auf. Bei 10, 20 und 50 µs/div werden 100, 200 beziehungsweise 500 Samples pro Kanal mit 1 MS/s erfasst, ab 100 µs/div jeweils 1.000 Samples. Änderungen von Volt/div und Zeit/div werden im laufenden Betrieb zwischen den Aufnahmen übernommen. Beide Kurven werden anhand ihrer geräteseitigen Triggerposition und Abtastrate gemeinsam angezeigt. Diese Einstellungswechsel wurden live geprüft. Fehlende Aufnahmen beim Flankentrigger werden weiter untersucht. Die Messung von Versatz und Jitter mit einem gemeinsamen Referenzsignal sowie die Prüfung schneller A/B-Drehgebersignale stehen noch aus.

Mit **PDF + Werte speichern** wird die aktuelle vollständige Aufnahme unter `Dokumente/PokitSuite/Captures` abgelegt: als PDF-Messbericht mit Oszilloskopbild und Kennwerttabelle, als CSV-Wertetabelle mit Zeit- und Spannungswerten sowie als JSON-Aufnahme für PokitSuite. Gespeicherte Aufnahmen können in der Capture-Liste ausgewählt und mit **EINBLENDEN** als violette Vergleichskurve über die Live-Kurven gelegt werden.

**Zeitlinien** und **Spannungslinien** oberhalb des Diagramms schalten jeweils zwei Messlinien ein. Die blauen Linien T1/T2 mit der Maus horizontal, die orangefarbenen Linien U1/U2 vertikal ziehen. An jeder Linie steht der Achsenwert; unten im Diagramm stehen die vorzeichenbehafteten Differenzen T2−T1 und U2−U1 sowie 1/|Δt|. T=0 liegt an der Triggermarkierung. 1/|Δt| entspricht der Signalfrequenz, wenn die Zeitlinien genau eine Periode begrenzen. Die Werte folgen der aktuellen Zeitbasis, Spannungsskalierung und Vertikalposition. Die Schaltfläche **↺** setzt die Linien zurück. Die Linien sind manuelle Ablesehilfen und rasten nicht automatisch am Signal ein.

Die Frequenz- und Tastgradmessung setzt ein ausreichend sauberes, periodisches Signal voraus. Bei starkem Rauschen, Clipping oder weniger als einer vollständigen Periode können Werte fehlen oder ungenau sein.

## Sicherheit

Das MCP-Werkzeug kann Arduino-Quellcode verändern und Firmware übertragen. Änderungen werden gesichert, ersetzen aber keine Prüfung des Diffs und keine elektrische Schutzmaßnahme. Messbereiche, Massebezug und zulässige Eingangsspannungen des Pokit-Geräts sind einzuhalten.

