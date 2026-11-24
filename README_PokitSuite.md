# PokitSuite

## Native Version (Standard)

`Start_PokitSuite.cmd` startet die native C++/Qt-Version. Sie bindet QtPokit direkt ein, hält eine BLE-Verbindung offen und benötigt weder Browser noch Dokit-Unterprozesse. Die frühere Browserfassung bleibt über `Start_PokitSuite_Web.cmd` verfügbar.

## Start

Doppelklick auf `Start_PokitSuite.cmd`. Für die native Version sind weder Python noch ein Browser erforderlich.

## Bedienung

1. Pokit einschalten und `Start_PokitSuite.cmd` ausführen. Die Gerätesuche und Verbindung starten automatisch.
2. Oben **Multimeter** oder **Oszilloskop** auswählen.
   Nach dem Verbinden startet der aktive Bereich automatisch. Beim Reiterwechsel wird die vorherige Messart beendet und die neue gestartet.
3. Im Multimeter stehen 0,5 s, 1 s und 2 s Abtastung zur Wahl. Der optionale t-y-Schreiber zeigt Zeitfenster von 30 Sekunden bis 1 Stunde.
4. Das Oszilloskop arbeitet fortlaufend mit 10 kS/s und 1.000 Samples pro Aufnahme. Triggerflanke und Triggerpegel sind wählbar; Pokit stellt einen Hardwarekanal (CH1) bereit.

## Hinweise

- Die Weboberfläche bindet ausschließlich an `127.0.0.1`; sie ist nicht im Netzwerk freigegeben.
- Für die Verbindung muss Windows-Bluetooth aktiv sein. Wenn der Pokit gerade von einer anderen App belegt ist, diese Verbindung zuerst trennen.
- Es kann nur eine native PokitSuite-Instanz laufen. So konkurrieren nicht mehrere Programme um dieselbe BLE-Verbindung.
- USB-C am Pokit dient nur zur Stromversorgung bzw. zum Laden. Die Datenverbindung bleibt Bluetooth Low Energy.
- Wenn das Gerät nicht gefunden wird, startet PokitSuite automatisch einen neuen Suchlauf. Nach einem Abbruch erfolgt nach drei Sekunden ein Neuaufbau.
- Die tatsächlich verfügbaren Messarten und minimalen Loggerintervalle hängen vom Pokit-Modell und dessen Firmware ab.
- Die BLE-Notifications werden pro Verbindung nur einmal eingerichtet. Beim Reiterwechsel wird nur der Messmodus gewechselt; verspätete Pakete der vorherigen Messart werden verworfen.

## Selbsttest

```powershell
.\native\build\PokitSuiteNative.exe --cycle-test
```

Der Zyklustest wechselt automatisch Multimeter → DSO → Multimeter → DSO.
