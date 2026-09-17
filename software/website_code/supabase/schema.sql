create extension if not exists "pgcrypto";

create type public.device_kind as enum ('rath', 'mukut', 'sarthi', 'camera');
create type public.sensor_kind as enum ('co', 'co2', 'ch4', 'h2s', 'o2', 'so2', 'voc', 'other');
create type public.alert_severity as enum ('info', 'warning', 'critical');
create type public.stream_kind as enum ('rgb', 'thermal');

create table public.mine_sites (
  id uuid primary key default gen_random_uuid(),
  name text not null,
  code text not null unique,
  region text,
  depth_m integer,
  status text not null default 'active' check (status in ('active', 'maintenance', 'offline')),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table public.zones (
  id uuid primary key default gen_random_uuid(),
  site_id uuid not null references public.mine_sites (id) on delete cascade,
  name text not null,
  zone_type text not null check (zone_type in ('panel', 'junction', 'ramp', 'roadway', 'stope', 'other')),
  x numeric default 0,
  y numeric default 0,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (site_id, name)
);

create table public.workers (
  id uuid primary key default gen_random_uuid(),
  site_id uuid not null references public.mine_sites (id) on delete cascade,
  assigned_zone_id uuid references public.zones (id),
  employee_id text not null unique,
  name text not null,
  role text not null,
  status text not null default 'online' check (status in ('online', 'offline', 'alert', 'evacuated', 'maintenance')),
  is_active boolean not null default true,
  last_seen_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table public.devices (
  id uuid primary key default gen_random_uuid(),
  site_id uuid not null references public.mine_sites (id) on delete cascade,
  zone_id uuid references public.zones (id),
  worker_id uuid references public.workers (id) on delete set null,
  kind public.device_kind not null,
  name text not null,
  serial_number text not null unique,
  status text not null default 'online' check (status in ('online', 'offline', 'degraded', 'alert', 'maintenance')),
  battery_pct numeric(5,2) default 100,
  last_seen_at timestamptz,
  position_index integer,
  routing_mode text check (routing_mode in ('primary', 'fallback')),
  fw_version text,
  metadata jsonb not null default '{}'::jsonb,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table public.worker_locations (
  id bigserial primary key,
  worker_id uuid not null references public.workers (id) on delete cascade,
  site_id uuid not null references public.mine_sites (id) on delete cascade,
  zone_id uuid references public.zones (id),
  device_id uuid references public.devices (id) on delete set null,
  source text not null check (source in ('mukut', 'sarthi', 'manual')),
  x_rel double precision,
  y_rel double precision,
  z_rel double precision,
  signal_quality integer check (signal_quality between 0 and 100),
  accuracy_m double precision,
  measured_at timestamptz not null default now(),
  created_at timestamptz not null default now()
);

create table public.gas_readings (
  id bigserial primary key,
  site_id uuid not null references public.mine_sites (id) on delete cascade,
  zone_id uuid references public.zones (id),
  device_id uuid not null references public.devices (id) on delete cascade,
  worker_id uuid references public.workers (id) on delete set null,
  sensor_kind public.sensor_kind not null,
  reading_value double precision,
  unit text not null default 'ppm',
  is_reliable boolean not null default false,
  threshold_alert boolean not null default false,
  baseline_value double precision,
  deviation_pct double precision,
  severity public.alert_severity,
  measured_at timestamptz not null default now(),
  created_at timestamptz not null default now()
);

create table public.vibration_events (
  id bigserial primary key,
  site_id uuid not null references public.mine_sites (id) on delete cascade,
  zone_id uuid references public.zones (id),
  device_id uuid not null references public.devices (id) on delete cascade,
  event_type text not null check (event_type in ('seismic', 'vibration', 'shock')),
  peak_g double precision,
  rms_mm_s double precision,
  severity public.alert_severity,
  is_active boolean not null default true,
  measured_at timestamptz not null default now(),
  created_at timestamptz not null default now()
);

create table public.camera_streams (
  id uuid primary key default gen_random_uuid(),
  site_id uuid not null references public.mine_sites (id) on delete cascade,
  device_id uuid references public.devices (id) on delete set null,
  stream_type public.stream_kind not null,
  source_url text not null,
  is_live boolean not null default true,
  started_at timestamptz,
  ended_at timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now()
);

create table public.alerts (
  id uuid primary key default gen_random_uuid(),
  site_id uuid not null references public.mine_sites (id) on delete cascade,
  zone_id uuid references public.zones (id),
  worker_id uuid references public.workers (id) on delete set null,
  device_id uuid references public.devices (id) on delete set null,
  source text not null check (source in ('rath', 'mukut', 'sarthi', 'camera', 'system')),
  alert_type text not null check (alert_type in ('gas', 'vibration', 'location', 'camera', 'system')),
  severity public.alert_severity not null,
  title text not null,
  message text not null,
  metadata jsonb not null default '{}'::jsonb,
  status text not null default 'open' check (status in ('open', 'acknowledged', 'resolved')),
  created_at timestamptz not null default now(),
  acknowledged_at timestamptz,
  resolved_at timestamptz
);

create or replace function public.set_updated_at()
returns trigger as $$
begin
  new.updated_at = now();
  return new;
end;
$$ language plpgsql;

create trigger trg_set_updated_at_mine_sites
before update on public.mine_sites
for each row execute function public.set_updated_at();

create trigger trg_set_updated_at_zones
before update on public.zones
for each row execute function public.set_updated_at();

create trigger trg_set_updated_at_workers
before update on public.workers
for each row execute function public.set_updated_at();

create trigger trg_set_updated_at_devices
before update on public.devices
for each row execute function public.set_updated_at();

create trigger trg_set_updated_at_camera_streams
before update on public.camera_streams
for each row execute function public.set_updated_at();

create index idx_workers_site_active on public.workers (site_id, is_active, last_seen_at desc);
create index idx_devices_site_kind on public.devices (site_id, kind, status);
create index idx_devices_position on public.devices (site_id, position_index);
create index idx_worker_locations_measured_at on public.worker_locations (worker_id, measured_at desc);
create index idx_gas_readings_site_time on public.gas_readings (site_id, measured_at desc);
create index idx_gas_readings_device_time on public.gas_readings (device_id, measured_at desc);
create index idx_vibration_site_time on public.vibration_events (site_id, measured_at desc);
create index idx_camera_streams_site_live on public.camera_streams (site_id, is_live, stream_type);
create index idx_alerts_site_status on public.alerts (site_id, status, severity, created_at desc);

alter table public.mine_sites enable row level security;
alter table public.zones enable row level security;
alter table public.workers enable row level security;
alter table public.devices enable row level security;
alter table public.worker_locations enable row level security;
alter table public.gas_readings enable row level security;
alter table public.vibration_events enable row level security;
alter table public.camera_streams enable row level security;
alter table public.alerts enable row level security;

create policy "service role can manage all mine data"
on public.mine_sites for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "service role can manage all zone data"
on public.zones for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "service role can manage all worker data"
on public.workers for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "service role can manage all device data"
on public.devices for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "service role can manage all location data"
on public.worker_locations for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "service role can manage all gas data"
on public.gas_readings for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "service role can manage all vibration data"
on public.vibration_events for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "service role can manage all camera streams"
on public.camera_streams for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "service role can manage all alerts"
on public.alerts for all
using (auth.role() = 'service_role')
with check (auth.role() = 'service_role');

create policy "authenticated users can read mine data"
on public.mine_sites for select
using (auth.role() = 'authenticated');

create policy "authenticated users can read zone data"
on public.zones for select
using (auth.role() = 'authenticated');

create policy "authenticated users can read worker data"
on public.workers for select
using (auth.role() = 'authenticated');

create policy "authenticated users can read device data"
on public.devices for select
using (auth.role() = 'authenticated');

create policy "authenticated users can read location data"
on public.worker_locations for select
using (auth.role() = 'authenticated');

create policy "authenticated users can read gas data"
on public.gas_readings for select
using (auth.role() = 'authenticated');

create policy "authenticated users can read vibration data"
on public.vibration_events for select
using (auth.role() = 'authenticated');

create policy "authenticated users can read camera streams"
on public.camera_streams for select
using (auth.role() = 'authenticated');

create policy "authenticated users can read alerts"
on public.alerts for select
using (auth.role() = 'authenticated');
