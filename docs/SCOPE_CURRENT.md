# Oscilloscope current measurement

In **VERTICAL**, select the channel, set **Messgröße → Strom**, then select DC or AC coupling and the required current/division scale. CH1 and CH2 can independently measure voltage or current. Coupling and the timebase remain shared. The amplitude cursors become I1/I2 with ΔI, and channel metrics use A, mA or µA. Trigger levels use the trigger source's unit.

The physical Pokit switch must be in **low current / multi-mode** (up to 300 mA) or **high current**. The application reads the switch in scope mode and waits rather than issuing a current acquisition while voltage mode is selected or the switch is unknown. Low-current scales are restricted to that path's capacity. Explicit device ranges are selected; the DSO service does not use auto-range. Changing the measurement quantity clears that channel's old waveform and metrics and resets scale, offset and the relevant trigger level. Firmware metadata must match the requested acquisition mode.

The status service subscribes to switch notifications when the device supports them. Explicit confirmation reads are queued between complete acquisitions, every 30 seconds with an acknowledged subscription or every 5 seconds without one. Reads have a 4.5-second deadline and bounded retry backoff. Current acquisition waits for an initial confirmed switch position. With an acknowledged subscription and a known position, two transient read failures retain that position with a warning; the third failure pauses acquisition and reconnects the affected device. Without notifications, a failed read pauses current acquisition until confirmation returns. Newer switch notifications take precedence over a delayed read reply. Channel numbers follow the current device attachment order; identify the Pokit by its saved color and reported switch position after reconnecting.

Current is measured **in series**, not across a voltage source. Turn off circuit power before changing the connection. Respect the manufacturer's limits, including the 10 A maximum and the limit above 6 A of 30 seconds per 15 minutes. The app displays a reminder; it does not replace the instrument's protections or enforce a thermal duty-cycle timer. See the [Pokit Pro manual, sections 5, 6 and 8](https://help.pokitmeter.com/hc/en-us/article_attachments/4407576614041).

## Files and AI tools

Mixed voltage/current PDF reports show each channel's unit. CSV columns are `sample_index,time_s,value,unit,channel`. JSON and live telemetry include `unit`, `quantity`, generic `waveform` and generic amplitude metrics. Current channels expose `waveform_a`, `vpp_a`, etc., and `amps_per_div`; voltage aliases such as `waveform_v` remain available for voltage data only. Generic settings include `units_per_div`, `vertical_position` and `trigger_level`. This prevents amperes from being labelled as volts. Radio CSV times remain relative to the shared trigger.

Reference curves are drawn against the first channel with a matching unit. A current reference is not drawn using a voltage scale. The existing reference selector loads the first stored channel, as before.

MCP scope readers retain the current unit and can return current samples. A voltage-amplitude reference check is rejected for a current channel. The legacy `configure_scope.volts_per_div` argument uses the active channel's base unit (V or A); select the measurement quantity in the native UI first.

Use `get_scope_frame(channel=2)` or `get_scope_metrics(channel=2)` when the current-measuring device is CH2. Existing calls still default to CH1.

## Validation

Native tests verify explicit current range selection, low/high path boundaries, µA scales, unit resets, independent channel state and current JSON/CSV/PDF units. Mixed-unit PDF pages were rendered and inspected. The MCP test covers current-frame decimation and rejection of voltage-reference checks on current data.

On 2026-11-27, one physical Pokit Pro in the high-current switch position returned complete 200-sample current frames in DC and AC modes using the 10 A and 3 A firmware ranges. A second Pokit simultaneously returned voltage frames. A mixed current/voltage radio pair with CH2 as master and a 400-row mixed-unit export also succeeded. Selecting current on the device still switched to voltage correctly produced a waiting notice and no current samples. Current accuracy against a known reference, physical low-current-path validation and high-current thermal testing remain unvalidated.

After serializing status reads between acquisitions, a 30-second live current/voltage run completed without GATT service errors or watchdog re-synchronization messages. This is a short communication smoke test, not a calibrated accuracy or long-duration reliability test.
