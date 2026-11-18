-- TARGET FRENZY - run this once in Supabase: SQL Editor -> New query -> paste -> Run

create table if not exists public.scores (
  id          bigint generated always as identity primary key,
  created_at  timestamptz not null default now(),
  game_id     text not null unique,          -- stops the same game being saved twice
  player_name text not null check (char_length(player_name) between 1 and 12),
  score       integer not null check (score >= 0),
  round1      integer not null default 0,
  round2      integer not null default 0,
  round3      integer not null default 0,
  shots       integer not null default 0,
  hits        integer not null default 0
);

-- Row Level Security: the ESP32 may ADD scores and anyone may READ them,
-- but nobody can edit or delete them with the public key.
alter table public.scores enable row level security;

drop policy if exists "esp32 can insert scores" on public.scores;
create policy "esp32 can insert scores" on public.scores
  for insert to anon with check (true);

drop policy if exists "anyone can read scores" on public.scores;
create policy "anyone can read scores" on public.scores
  for select to anon using (true);

-- Handy view: best 10 scores of all time
create or replace view public.leaderboard
with (security_invoker = true) as
  select player_name, score, created_at
  from public.scores
  order by score desc, created_at asc
  limit 10;
