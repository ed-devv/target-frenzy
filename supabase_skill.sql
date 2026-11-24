-- TARGET FRENZY - IS Feature 3: store each player's K-Means skill group
-- Run ONCE in Supabase: SQL Editor -> New query -> paste -> Run

alter table public.scores add column if not exists skill text;   -- 'Beginner', 'Sharpshooter' or 'Pro'

-- Label the games that were already uploaded (same K-Means centers as the ESP32)
update public.scores set skill = 'Pro' where game_id = 'a36f5024' and skill is null;
update public.scores set skill = 'Beginner' where game_id = '39e1a2bf' and skill is null;
update public.scores set skill = 'Pro' where game_id = '8f4e09f1' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = 'f6401e64' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'd796334b' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = 'c955c4b9' and skill is null;
update public.scores set skill = 'Pro' where game_id = '5ab2251e' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '744fc065' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '61399f42' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'fdcb4e96' and skill is null;
update public.scores set skill = 'Beginner' where game_id = '91a051fc' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'a899604a' and skill is null;
update public.scores set skill = 'Pro' where game_id = '554f8cae' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'ca4ff899' and skill is null;
update public.scores set skill = 'Beginner' where game_id = 'a84367b5' and skill is null;
update public.scores set skill = 'Beginner' where game_id = 'd48e7ea2' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '64249bfd' and skill is null;
update public.scores set skill = 'Pro' where game_id = '5a73396b' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '7b4a8742' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '9dde3072' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '03d4dd01' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '8fe5297e' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'eb99efe0' and skill is null;
update public.scores set skill = 'Pro' where game_id = '49da0c8c' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '5ea4ec9d' and skill is null;
update public.scores set skill = 'Beginner' where game_id = 'b1f3e132' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '15128b5d' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = 'c4b2a7c6' and skill is null;
update public.scores set skill = 'Beginner' where game_id = '898dd681' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = 'eb5ae4b5' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'b704915e' and skill is null;
update public.scores set skill = 'Pro' where game_id = '709d3ecd' and skill is null;
update public.scores set skill = 'Beginner' where game_id = '563048db' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = 'a37b1755' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '22a37db3' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'bf0bf49b' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'a7ff67e9' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = 'cbf6a1c7' and skill is null;
update public.scores set skill = 'Sharpshooter' where game_id = '77535bf3' and skill is null;
update public.scores set skill = 'Pro' where game_id = 'ce9964b2' and skill is null;

-- Older games (before 19 Nov) have no reaction-time data, so they stay without a label.
