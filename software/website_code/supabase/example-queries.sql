-- Example: create a site and core entities
insert into public.mine_sites (name, code, region, depth_m)
values ('Kusmunda', 'KSM-01', 'South-East', 312)
returning *;

-- Example: create mine zones
insert into public.zones (site_id, name, zone_type, x, y)
values
  ((select id from public.mine_sites where code = 'KSM-01'), 'Panel 4', 'panel', 26, 27),
  ((select id from public.mine_sites where code = 'KSM-01'), 'Panel 5', 'panel', 52, 44),
  ((select id from public.mine_sites where code = 'KSM-01'), 'Junction 2', 'junction', 72, 30),
  ((select id from public.mine_sites where code = 'KSM-01'), 'Ramp 1', 'ramp', 82, 67)
returning *;

-- Example: add worker profiles (no health fields)
insert into public.workers (site_id, employee_id, name, role, status, assigned_zone_id)
values
  ((select id from public.mine_sites where code = 'KSM-01'), 'MIN-2041', 'Ramesh Kumar', 'Cutting Crew', 'online', (select id from public.zones where name = 'Panel 4')),
  ((select id from public.mine_sites where code = 'KSM-01'), 'MIN-2088', 'Suresh Pradhan', 'Roof Bolting', 'alert', (select id from public.zones where name = 'Panel 5'))
returning *;

-- Example: add devices
insert into public.devices (site_id, zone_id, worker_id, kind, name, serial_number, status, battery_pct, last_seen_at, position_index, routing_mode)
values
  ((select id from public.mine_sites where code = 'KSM-01'), (select id from public.zones where name = 'Panel 5'), (select id from public.workers where employee_id = 'MIN-2088'), 'rath', 'RATH-07', 'RATH-07-001', 'online', 89, now(), null, null),
  ((select id from public.mine_sites where code = 'KSM-01'), (select id from public.zones where name = 'Panel 5'), (select id from public.workers where employee_id = 'MIN-2088'), 'mukut', 'MUKUT-2088', 'MUKUT-2088-001', 'online', 64, now(), null, null),
  ((select id from public.mine_sites where code = 'KSM-01'), (select id from public.zones where name = 'Panel 5'), null, 'sarthi', 'S-07', 'S-07-001', 'online', 92, now(), 7, 'fallback')
returning *;

-- Example: location fix from MUKUT + BLE relative to Sarthi node
insert into public.worker_locations (worker_id, site_id, zone_id, device_id, source, x_rel, y_rel, z_rel, signal_quality, accuracy_m, measured_at)
values (
  (select id from public.workers where employee_id = 'MIN-2088'),
  (select id from public.mine_sites where code = 'KSM-01'),
  (select id from public.zones where name = 'Panel 5'),
  (select id from public.devices where serial_number = 'MUKUT-2088-001'),
  'mukut',
  52,
  44,
  2.4,
  82,
  1.2,
  now()
);

-- Example: gas reading from Rath or wearable
insert into public.gas_readings (site_id, zone_id, device_id, worker_id, sensor_kind, reading_value, unit, is_reliable, threshold_alert, baseline_value, deviation_pct, severity, measured_at)
values (
  (select id from public.mine_sites where code = 'KSM-01'),
  (select id from public.zones where name = 'Panel 5'),
  (select id from public.devices where serial_number = 'RATH-07-001'),
  (select id from public.workers where employee_id = 'MIN-2088'),
  'ch4',
  0.8,
  '%',
  true,
  false,
  0.5,
  60,
  null,
  now() - interval '4 minutes'
);

-- Example: basic threshold gas reading from Sarthi
insert into public.gas_readings (site_id, zone_id, device_id, sensor_kind, reading_value, unit, is_reliable, threshold_alert, severity, measured_at)
values (
  (select id from public.mine_sites where code = 'KSM-01'),
  (select id from public.zones where name = 'Panel 5'),
  (select id from public.devices where serial_number = 'S-07-001'),
  'co',
  18,
  'ppm',
  false,
  true,
  'warning',
  now()
);

-- Example: seismic event from Sarthi
insert into public.vibration_events (site_id, zone_id, device_id, event_type, peak_g, rms_mm_s, severity, is_active, measured_at)
values (
  (select id from public.mine_sites where code = 'KSM-01'),
  (select id from public.zones where name = 'Panel 5'),
  (select id from public.devices where serial_number = 'S-07-001'),
  'seismic',
  0.17,
  3.4,
  'warning',
  true,
  now()
);

-- Example: camera streams when Rath is live
insert into public.camera_streams (site_id, device_id, stream_type, source_url, is_live, started_at)
values
  ((select id from public.mine_sites where code = 'KSM-01'), (select id from public.devices where serial_number = 'RATH-07-001'), 'rgb', 'https://example.com/live/rgb', true, now()),
  ((select id from public.mine_sites where code = 'KSM-01'), (select id from public.devices where serial_number = 'RATH-07-001'), 'thermal', 'https://example.com/live/thermal', true, now());

-- Example: alert surfaced to command center
insert into public.alerts (site_id, zone_id, worker_id, device_id, source, alert_type, severity, title, message, metadata)
values (
  (select id from public.mine_sites where code = 'KSM-01'),
  (select id from public.zones where name = 'Panel 5'),
  (select id from public.workers where employee_id = 'MIN-2088'),
  (select id from public.devices where serial_number = 'S-07-001'),
  'sarthi',
  'gas',
  'warning',
  'Gas threshold reached',
  'Sarthi node S-07 detected elevated CO activity near the active zone.',
  '{"threshold_ppm": 18, "sensor_kind": "co"}'::jsonb
);

-- Example: real-time query for current live gas and location data
select w.name,
       wl.measured_at,
       wl.x_rel,
       wl.y_rel,
       gr.sensor_kind,
       gr.reading_value,
       gr.severity
from public.workers w
left join public.worker_locations wl on wl.worker_id = w.id
left join public.gas_readings gr on gr.worker_id = w.id
where w.site_id = (select id from public.mine_sites where code = 'KSM-01')
order by wl.measured_at desc, gr.measured_at desc;
