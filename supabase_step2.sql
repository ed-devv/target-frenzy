-- TARGET FRENZY - Step 2: extra gameplay data for the Intelligent System features
-- Run this ONCE in Supabase: SQL Editor -> New query -> paste -> Run
-- (Safe to run again: "if not exists" skips columns that are already there.)

alter table public.scores
  add column if not exists shots_r1        integer not null default 0,  -- shots fired in round 1
  add column if not exists shots_r2        integer not null default 0,
  add column if not exists shots_r3        integer not null default 0,
  add column if not exists hits_r1         integer not null default 0,  -- targets hit in round 1
  add column if not exists hits_r2         integer not null default 0,
  add column if not exists hits_r3         integer not null default 0,
  add column if not exists avg_reaction_ms integer not null default 0,  -- avg ms from "target up" to "hit"
  add column if not exists targets_missed  integer not null default 0;  -- targets that went down unhit

-- Older games (before this update) keep 0 in the new columns.
