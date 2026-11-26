# Multimeter statistics and recording

The native workspace supports two independent Pokit Pro channels. Each channel retains its selected measurement mode and uses the physical range-switch information already provided by the device.

## Display and statistics

- **HOLD** freezes the numeric display. Acquisition, statistics, alarms and recording continue.
- **REL** captures the current displayed absolute reading as a reference and shows subsequent readings minus that reference. If HOLD is active, REL captures the held reading. Disable HOLD to see changing relative values.
- **Reset** resets the channel statistics and clears HOLD/REL. It keeps the trend history and any running CSV recording.
- Minimum, maximum, arithmetic mean and sample count use valid absolute readings since the last reset or measurement-unit change. They are not calculated from the decimated plot.
- Changing measurement units clears the channel statistics, trend, HOLD, REL and limits. This prevents mixing incompatible quantities. Previously recorded CSV rows retain their original units.

These statistics describe the DMM readings delivered by the instrument; they do not capture transients between readings.

## Limits and events

Open **Grenzen** on a channel and enter lower and upper limits in the channel's base unit. Current limits are in amperes; capacitance limits are in farads even though the display shows microfarads. Both decimal commas and decimal points are accepted. The lower limit must be less than the upper limit.

Limits are checked against absolute live readings, including while HOLD or REL is enabled. The channel card turns red outside the range. Events are added on entry into an alarm state, transitions between high/low alarm, and recovery into the range. This is a visual alarm; it does not currently produce sound. Equality with a limit is inside the range. There is no configurable hysteresis yet.

The **Ereignisse** dialog lists the most recent 100 events and comments. Annotations are also marked in the trend. During recording, all events are appended to the CSV file.

## Timestamped trend

Both channel plots share a time window but have independent vertical scales and units. The window selector, plus/minus buttons and mouse wheel zoom the time axis. Arrow buttons browse historical data; **LIVE** resumes following new readings. Historical browsing freezes the time window while acquisition continues.

The in-memory trend keeps the last **100,000 readings per channel** (about 13.9 hours at a steady 0.5-second interval). A view exposes at most 1,200 plotted points per channel, preserving bucket minima and maxima. Statistics retain all valid samples since reset. Empty periods are left blank; gaps longer than five seconds are not joined by a line. The selector's 24-hour option does not extend the buffer's sample capacity.

## CSV recording and PDF reports

Click **AUFZEICHNEN** to create a new CSV file and **AUFZEICHNUNG STOPPEN** to close it. Files are stored in the user's `Documents/PokitSuite/Multimeter` directory. **Ordner / CSV** opens this folder. Each new recording gets a unique filename.

CSV is written and flushed as readings arrive, without retaining the whole recording in memory. It includes UTC timestamps, elapsed seconds, channel, absolute value, unit, row kind and event/comment text. Numeric values use a decimal point. Text fields are quoted, including embedded commas, quotes and newlines. Recording captures future readings from the time the button is pressed; it does not retroactively export the existing plot buffer. Switching to the oscilloscope pauses DMM readings; returning to the multimeter resumes them in the same open recording. Application exit closes the file; restarting the application starts a fresh session.

**PDF-Bericht** saves an A4 summary of the current statistics, the selected time window for both trends and recent events. It does not export every raw sample as a multi-page table. Use CSV for the complete recorded value table. The PDF uses absolute readings regardless of HOLD/REL.

Saved CSV session replay and multi-day soak validation remain planned. This implementation records DMM readings through the desktop application; it does not yet configure the Pokit's autonomous on-device logger.

## Verification

Offline native tests cover statistics, HOLD/REL independence, unit resets, alarm transitions, invalid limits, CSV quoting, PDF generation, historical-window stability and bounded snapshots after more than 100,000 readings. Live Windows checks used two Pokit Pro devices in DC voltage mode, including HOLD, a visual limit alarm, a two-channel CSV recording with a comment and PDF export. PDF pages were rendered and visually inspected. Long-duration recording across multiple days and additional physical measurement modes still need hardware validation.
