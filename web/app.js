// Target Frenzy – online leaderboard (reads scores from Supabase)

// =====================================================================
//  SETTINGS YOU MUST CHANGE  (the same values you put in sketch.ino)
// =====================================================================
const SUPABASE_URL = "https://xgucrsnfjbnxbgdgnjvk.supabase.co";
const SUPABASE_KEY = "sb_publishable_APHNcWA-bZKF4jFbgBej4g_ZCgqEX5j";
const REFRESH_SECONDS = 5;   // how often the page checks for new scores

const $ = id => document.getElementById(id);
let knownIds = null;

function headers(extra) {
  const h = { apikey: SUPABASE_KEY };
  if (!SUPABASE_KEY.startsWith("sb_")) h.Authorization = "Bearer " + SUPABASE_KEY;  // old "eyJ..." anon keys
  return Object.assign(h, extra || {});
}

async function query(path, extraHeaders) {
  const r = await fetch(SUPABASE_URL + "/rest/v1/" + path, { headers: headers(extraHeaders), cache: "no-store" });
  if (!r.ok) throw new Error("Supabase answered " + r.status);
  return r;
}

function item(r, opts) {
  const li = document.createElement("li");
  if (opts.first) li.classList.add("first");
  if (opts.isNew) li.classList.add("new");

  if (opts.rank) {
    const rk = document.createElement("span");
    rk.className = "rk";
    rk.textContent = opts.rank;
    li.appendChild(rk);
  }

  const nm = document.createElement("span");
  nm.className = "nm";
  nm.textContent = r.player_name;
  const sub = document.createElement("span");
  sub.className = "sub";
  sub.textContent = opts.sub;
  nm.appendChild(sub);
  li.appendChild(nm);

  const sc = document.createElement("span");
  sc.className = "sc";
  sc.textContent = r.score;
  li.appendChild(sc);
  return li;
}

function timeAgo(iso) {
  const s = Math.max(0, (Date.now() - new Date(iso).getTime()) / 1000);
  if (s < 60) return "just now";
  if (s < 3600) return Math.floor(s / 60) + " min ago";
  if (s < 86400) return Math.floor(s / 3600) + " h ago";
  return new Date(iso).toLocaleDateString();
}

function accuracy(r) {
  return r.shots ? Math.round((100 * r.hits) / r.shots) + "% accuracy" : "no shots";
}

async function refresh() {
  try {
    const cols = "id,player_name,score,shots,hits,created_at";
    const [topRes, recentRes, countRes] = await Promise.all([
      query("scores?select=" + cols + "&order=score.desc,created_at.asc&limit=10"),
      query("scores?select=" + cols + "&order=created_at.desc&limit=8"),
      query("scores?select=id&limit=1", { Prefer: "count=exact" })
    ]);
    const top = await topRes.json();
    const recent = await recentRes.json();
    const range = countRes.headers.get("content-range") || "";
    const total = range.includes("/") ? Number(range.split("/")[1]) : top.length;

    // one-line summary
    const summary = $("summary");
    if (!top.length) {
      summary.textContent = "No games yet. The first score sets the record.";
    } else {
      summary.innerHTML = "";
      summary.append(
        document.createElement("b"), " ",
        total === 1 ? "game played. Record " : "games played. Record ",
        document.createElement("b"), " by ",
        document.createElement("b"), "."
      );
      const bs = summary.querySelectorAll("b");
      bs[0].textContent = total;
      bs[1].textContent = top[0].score;
      bs[2].textContent = top[0].player_name;
    }

    const isNew = id => knownIds && !knownIds.has(id);

    // all-time top 10
    const t = $("top");
    t.innerHTML = "";
    if (!top.length) t.innerHTML = '<li class="empty">No scores yet.</li>';
    top.forEach((r, i) => {
      t.appendChild(item(r, {
        rank: String(i + 1),
        first: i === 0,
        isNew: isNew(r.id),
        sub: accuracy(r) + " · " + timeAgo(r.created_at)
      }));
    });

    // latest games
    const rt = $("recent");
    rt.innerHTML = "";
    if (!recent.length) rt.innerHTML = '<li class="empty">Waiting for the first game.</li>';
    recent.forEach(r => {
      rt.appendChild(item(r, { isNew: isNew(r.id), sub: timeAgo(r.created_at) }));
    });

    knownIds = new Set([...recent.map(r => r.id), ...top.map(r => r.id)]);
    $("status").className = "";
    $("status").textContent = "Live · updated " + new Date().toLocaleTimeString();
  } catch (e) {
    $("status").className = "err";
    $("status").textContent = SUPABASE_URL.includes("YOUR-PROJECT")
      ? "Add your Supabase URL and key at the top of app.js"
      : "Can't reach the scores right now (" + e.message + "). Retrying…";
  }
}

refresh();
setInterval(refresh, REFRESH_SECONDS * 1000);