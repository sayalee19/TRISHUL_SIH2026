# Mine telemetry data model

This project is designed around a mine safety stack where:

- Rath is the emergency-capable relay and camera platform.
- Mukut is mounted on each worker's helmet and reports location and gas.
- Sarthi nodes are fixed anchor modules in the mine and provide BLE-based relative positioning and threshold gas/vibration alerts.
- Live RGB and thermal feeds are only available when Rath is active.

## Core entities

1. `mine_sites`
   - one row per mine or section
2. `zones`
   - logical mine sections like panel, ramp, junction, roadway
3. `workers`
   - worker roster, no medical or health fields included
4. `devices`
   - all hardware devices: rath, mukut, sarthi, camera
5. `worker_locations`
   - periodic worker coordinates relative to Sarthi anchor nodes
6. `gas_readings`
   - gas sensor data from Rath/Mukut/Sarthi with reliable vs threshold-only states
7. `vibration_events`
   - seismic and vibration events from Sarthi nodes
8. `camera_streams`
   - RGB and thermal streams from Rath
9. `alerts`
   - system-level alert events used for command center notifications

## Important rules

- Do not store health metrics like pulse, SpO2, or temperature in the worker table.
- Treat `gas_readings` as two visual categories in the UI:
  - threshold-only chips from Mukut (helmet MQ) and Saarthi (fixed MQ): Normal/Alert, never shown as a calibrated number
  - reliable readings from Rath surveys only: these are the one place a real value is shown, with `baseline_value` / `deviation_pct` when available
- Saarthi nodes are ordered by `devices.position_index` (physical tunnel order). `routing_mode` is `primary` (N+1) or `fallback` (N+2).
- Track device ownership and zone context so the UI can show which worker is near which Sarthi module.
- Keep `camera_streams` separate from `gas_readings`, because the camera lane only operates when Rath is active.

## Recommended Supabase usage

- Use Postgres tables for durable storage.
- Use Supabase Realtime on `worker_locations`, `gas_readings`, `vibration_events`, `alerts`, and `camera_streams` for live command-center updates.
- Keep worker-facing cards focused on:
  - worker identity and zone
  - last location update
  - recent gas status
  - recent alert severity
  - camera availability when Rath is online

## Example data flow

- Mukut on helmet emits gas + location.
- Worker location is computed relative to nearby Sarthi BLE anchors.
- Sarthi emits threshold-only gas and vibration alerts.
- Rath emits reliable gas values and live camera streams.
- The UI combines these streams into a single safety view for the operator.
