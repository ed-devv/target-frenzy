/*
  =====================================================================
   TARGET FRENZY  -  carnival laser shooting gallery (ESP32)
  =====================================================================
   What this program does:
   - 3 rounds x 15 seconds, 3-second break between rounds
   - 4 pop-up targets (servo + LDR in the head of each target)
   - Each press of the TRIGGER fires one laser shot and uses one bullet.
     When the magazine is empty, the gun reloads by itself (2 seconds).
   - A hit only counts while the laser is actually firing, so a phone
     flashlight or sunlight cannot cheat.
   - The ESP32 makes its own WiFi network ("TargetFrenzy") for the
     dashboard laptop/tablet: open  http://192.168.4.1
   - The ESP32 also joins your phone hotspot to upload scores to Supabase.
   - Every score is saved in the ESP32's own memory FIRST, so nothing is
     lost if the internet or power goes out. Unsent scores are uploaded
     automatically when the internet comes back.

   Libraries needed: ESP32Servo (everything else comes with the ESP32 board)
   Board: "ESP32 Dev Module"   Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
  =====================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ESP32Servo.h>
#include "fonts.h"   // dashboard fonts (keep fonts.h in the same folder as this sketch)

// =====================================================================
// 1) SETTINGS YOU MUST CHANGE
// =====================================================================

// Your hotspot name/password and Supabase URL/key live in secrets.h
// (same folder as this sketch). secrets.h is NOT uploaded to GitHub.
#include "secrets.h"

// =====================================================================
// 2) PINS
// =====================================================================
const int TRIGGER_BUTTON_PIN = 19;  // TRIGGER button on the gun (same wire the old RELOAD button used)
const int START_BUTTON_PIN   = 23;  // red START button
const int RESET_BUTTON_PIN   = 21;  // yellow
const int LASER_PIN          = 25;  // goes to the 2N2222 base (through 1k resistor)
const int BUZZER_PIN         = 4;   // buzzer + leg (no resistor for an active buzzer)
const bool BUZZER_IS_ACTIVE  = true; // true = active buzzer (beeps by itself), false = passive buzzer

const int NUM_TARGETS = 3;                            // Target 4 removed
const int servoPins[NUM_TARGETS] = {13, 14, 27};      // (pin 26 now unused)
const int ldrPins[NUM_TARGETS]   = {34, 35, 32};      // ADC1 pins only! (pin 33 now unused)
const int ledPins[NUM_TARGETS]   = {16, 17, 18};      // LED for each target (pin 22 now unused)

// =====================================================================
// 3) GAME TUNING (safe to change)
// =====================================================================
const int servoDownAngle = 0;
const int servoUpAngle   = 90;

// --- Gun ---
// Rifle mode: HOLD the trigger and the gun keeps firing until you let go or run out of ammo.
const bool HOLD_TO_FIRE = true;           // true = rifle (hold to fire), false = pistol (one shot per press)
const bool AUTO_FIRE = false;             // true = fires by itself with no trigger (old mode)
const bool USE_AMMO  = true;              // true = shots run out, then the gun reloads by itself
const bool ONE_HIT_PER_SHOT = true;       // one bullet can only knock down one target
const int  MAG_SIZE = 20;                 // shots per magazine
const unsigned long FIRE_INTERVAL_MS = 200;  // time from one shot to the next (200 = 5 shots per second)
const unsigned long LASER_ON_MS      = 150;  // how long the laser flashes per shot (keep below FIRE_INTERVAL_MS)
const unsigned long RELOAD_MS        = 2000; // automatic reload time when the magazine is empty

// --- Hit detection ---
// For these LDR modules, brighter light = LOWER number.
// A hit = reading drops at least HIT_DROP below the target's normal level.
// Use LDR_TEST_MODE to find a good value for your booth.
const int HIT_DROP = 300;
const unsigned long SETTLE_MS = 250;      // ignore the LDR while the target is still swinging up
const unsigned long LASER_AFTERGLOW_MS = 20; // LDR settle time after a flash before learning room light
const bool LDR_TEST_MODE = false;         // true = calibration mode (see the guide)

// --- Rounds ---
const int NUM_ROUNDS = 3;
const unsigned long ROUND_MS           = 15000; // 15 seconds per round
const unsigned long BREAK_MS           = 3000;  // 3 seconds between rounds
const unsigned long START_COUNTDOWN_MS = 3000;  // 3 seconds before Round 1

const int pointsPerHit[NUM_ROUNDS]               = {50,   75,   100};
const unsigned long targetUpMs[NUM_ROUNDS]       = {2500, 1500, 900};  // how long a target stays up
const unsigned long minPauseMs[NUM_ROUNDS]       = {500,  400,  300};  // shortest wait before popping up
const unsigned long maxPauseMs[NUM_ROUNDS]       = {2000, 1500, 750};  // longest wait before popping up
const unsigned long HIT_COOLDOWN_MS = 1500;  // after a HIT, that target stays down at least this long (1 hit per pop-up)

const int LEADERBOARD_SIZE = 10;
const int MAX_NAME_LEN = 12;

// =====================================================================
// BUTTON TYPE
// (Must stay ABOVE every function: Arduino IDE adds hidden function
//  declarations before the first function, and they need to know "Button".)
// =====================================================================
// Leaderboard entry (defined up here so Arduino's hidden function declarations know it)
struct Entry { char id[12]; char name[MAX_NAME_LEN + 1]; int score; int shots; int hits; int num; };
struct Button { int pin; bool stable; bool lastRead; unsigned long changedAt; bool pressed; };
Button btnStart   = {START_BUTTON_PIN,   HIGH, HIGH, 0, false};
Button btnReset   = {RESET_BUTTON_PIN,   HIGH, HIGH, 0, false};
Button btnTrigger = {TRIGGER_BUTTON_PIN, HIGH, HIGH, 0, false};

// =====================================================================
// SOUNDS (passive buzzer)
// =====================================================================
struct Note { uint16_t freq; uint16_t ms; };   // freq 0 = silence
const Note SND_HIT[]       = {{1319, 60}, {1760, 120}};
const Note SND_EMPTY[]     = {{220, 120}, {0, 40}, {180, 160}};
const Note SND_RELOAD[]    = {{600, 70}, {0, 30}, {900, 70}};
const Note SND_READY[]     = {{1500, 60}};
const Note SND_DRY[]       = {{300, 30}};   // trigger pulled while reloading
const Note SND_TICK[]      = {{880, 100}};
const Note SND_GO[]        = {{1760, 350}};
const Note SND_ROUND_END[] = {{988, 120}, {784, 200}};
const Note SND_GAMEOVER[]  = {{523, 150}, {659, 150}, {784, 150}, {1047, 400}};
const Note SND_SAVED[]     = {{1047, 80}, {1319, 80}, {1568, 160}};
const Note SND_RESET[]     = {{400, 200}};

const Note* sndNotes = nullptr;
int sndCount = 0, sndIndex = 0;
unsigned long sndNoteEnd = 0;

#define PLAY(s) playSound(s, sizeof(s) / sizeof(s[0]))

void playSound(const Note* notes, int count) {
  sndNotes = notes; sndCount = count; sndIndex = 0; sndNoteEnd = 0;
}

void updateSound() {
  if (!sndNotes) return;
  unsigned long now = millis();
  if (sndNoteEnd != 0 && (long)(now - sndNoteEnd) < 0) return;
  if (sndIndex >= sndCount) {
    if (BUZZER_IS_ACTIVE) digitalWrite(BUZZER_PIN, LOW); else noTone(BUZZER_PIN);
    sndNotes = nullptr;
    return;
  }
  const Note& n = sndNotes[sndIndex++];
  if (BUZZER_IS_ACTIVE) digitalWrite(BUZZER_PIN, n.freq ? HIGH : LOW);
  else if (n.freq) tone(BUZZER_PIN, n.freq); else noTone(BUZZER_PIN);
  sndNoteEnd = now + n.ms;
}

// =====================================================================
// BUTTONS (debounced, detects the moment of pressing)
// =====================================================================
void updateButton(Button& b) {
  b.pressed = false;
  bool r = digitalRead(b.pin);
  unsigned long now = millis();
  if (r != b.lastRead) { b.lastRead = r; b.changedAt = now; }
  else if (r != b.stable && now - b.changedAt >= 30) {
    b.stable = r;
    const char* name = (b.pin == TRIGGER_BUTTON_PIN) ? "TRIGGER" :
                       (b.pin == START_BUTTON_PIN)   ? "START"   :
                       (b.pin == RESET_BUTTON_PIN)   ? "RESET"   : "?";
    if (r == LOW) {
      b.pressed = true;
      Serial.printf("[%s button pressed] (pin %d)\n", name, b.pin);
    } else if (b.pin == TRIGGER_BUTTON_PIN) {
      Serial.println("[TRIGGER button released]");
    }
  }
}

// =====================================================================
// GAME STATE
// =====================================================================
enum GameState { WAITING, COUNTDOWN, ROUND, BREAK, ENTER_NAME };
GameState gameState = WAITING;
int currentRound = 0;
unsigned long gameStateStart = 0;
int lastSecondShown = -1;

int score = 0;
int roundScores[NUM_ROUNDS] = {0, 0, 0};
int shots = 0, hits = 0;
int hitSeq = 0, lastHitTarget = -1, lastHitPoints = 0;
char gameId[12] = "";
char lastSavedId[12] = "";

// Gun
int ammo = MAG_SIZE;
bool reloading = false, emptyWarned = false, laserOn = false;
bool shotScored = false;   // this shot already hit a target
unsigned long reloadStart = 0, laserOnAt = 0, laserOffAt = 0, lastShotAt = 0;

// Targets
enum TargetState { T_IDLE, T_WAIT, T_UP };
Servo targetServos[NUM_TARGETS];
TargetState tState[NUM_TARGETS];
unsigned long tStart[NUM_TARGETS];
unsigned long tPause[NUM_TARGETS];
int baseline[NUM_TARGETS];

// Leaderboard (top 10 kept in RAM, everything kept in flash)
Entry top[LEADERBOARD_SIZE];
int topCount = 0;
const int RECENT_SIZE = 8;
Entry recent[RECENT_SIZE];   // newest first
int recentCount = 0;
int totalGames = 0;

// Storage / cloud
bool fsOk = false;
int pendingCount = 0;
unsigned long nextSyncAt = 0;
const char* SCORES_FILE  = "/scores.csv";
const char* PENDING_FILE = "/pending.csv";

WebServer server(80);

// Forward declarations
void beginRound(int r);
void endRound();
void resetGame();

unsigned long randomBetween(unsigned long lo, unsigned long hi) {
  return lo + (esp_random() % (hi - lo + 1));  // hardware random number generator
}

const char* stateName() {
  switch (gameState) {
    case WAITING:    return "WAITING";
    case COUNTDOWN:  return "COUNTDOWN";
    case ROUND:      return "ROUND";
    case BREAK:      return "BREAK";
    case ENTER_NAME: return "ENTER_NAME";
  }
  return "?";
}

// =====================================================================
// LASER / GUN
// =====================================================================
void laserOff() {
  if (laserOn) laserOffAt = millis();
  digitalWrite(LASER_PIN, LOW);
  laserOn = false;
}

void fireShot() {
  if (USE_AMMO) {
    if (ammo <= 0 || reloading) return;
    ammo--;
  }
  shots++;
  digitalWrite(LASER_PIN, HIGH);
  laserOn = true;
  shotScored = false;
  if (USE_AMMO) Serial.printf("  BANG! ammo %d/%d\n", ammo, MAG_SIZE);
  else          Serial.println("  BANG!");
  laserOnAt = millis();
  lastShotAt = millis();
}

void updateGun() {
  unsigned long now = millis();

  if (laserOn && now - laserOnAt >= LASER_ON_MS) laserOff();

  if (gameState != ROUND) { if (laserOn) laserOff(); return; }

  // Automatic reload finished?
  if (reloading && now - reloadStart >= RELOAD_MS) {
    reloading = false;
    ammo = MAG_SIZE;
    PLAY(SND_READY);
    Serial.println("Reloaded! Fire!");
  }

  // Firing
  bool wantShot;
  if (AUTO_FIRE)         wantShot = !laserOn && now - lastShotAt >= FIRE_INTERVAL_MS;
  else if (HOLD_TO_FIRE) wantShot = (btnTrigger.stable == LOW) && !laserOn;   // keep firing while held
  else                   wantShot = btnTrigger.pressed;
  if (reloading && !AUTO_FIRE && btnTrigger.pressed) PLAY(SND_DRY);   // click while reloading
  if (wantShot) {
    if (reloading) {
      // no shot while reloading
    } else if (now - lastShotAt >= FIRE_INTERVAL_MS) {
      fireShot();
      if (USE_AMMO && ammo == 0) {                 // magazine empty -> start reloading
        reloading = true;
        reloadStart = now;
        PLAY(SND_EMPTY);
        Serial.printf("Out of ammo! Reloading (%lu s)...\n", RELOAD_MS / 1000);
      }
    }
  }
}

// =====================================================================
// TARGETS
// =====================================================================
// Moves target i up or down AND switches its LED to match.
void setTarget(int i, bool up) {
  targetServos[i].write(up ? servoUpAngle : servoDownAngle);
  digitalWrite(ledPins[i], up ? HIGH : LOW);
}

void scheduleTarget(int i) {
  tState[i] = T_WAIT;
  tStart[i] = millis();
  tPause[i] = randomBetween(minPauseMs[currentRound], maxPauseMs[currentRound]);
}

void allTargetsDown() {
  for (int i = 0; i < NUM_TARGETS; i++) {
    setTarget(i, false);
    tState[i] = T_IDLE;
  }
}

void allTargetsUp() {
  for (int i = 0; i < NUM_TARGETS; i++) {
    setTarget(i, true);
    tState[i] = T_IDLE;
  }
}

void registerHit(int i) {
  shotScored = true;
  int pts = pointsPerHit[currentRound];
  score += pts;
  roundScores[currentRound] += pts;
  hits++;
  hitSeq++;
  lastHitTarget = i;
  lastHitPoints = pts;
  Serial.printf("Target %d HIT! +%d | Score: %d\n", i + 1, pts, score);
  PLAY(SND_HIT);
  setTarget(i, false);
  scheduleTarget(i);
  if (tPause[i] < HIT_COOLDOWN_MS) tPause[i] = HIT_COOLDOWN_MS;   // cooldown: no instant pop-up into the same beam
}

void handleTarget(int i) {
  unsigned long now = millis();
  switch (tState[i]) {
    case T_IDLE:
      break;

    case T_WAIT:
      if (now - tStart[i] >= tPause[i]) {
        setTarget(i, true);
        tState[i] = T_UP;
        tStart[i] = now;
        baseline[i] = -1;
      }
      break;

    case T_UP: {
      unsigned long upFor = now - tStart[i];
      if (upFor >= targetUpMs[currentRound]) {   // missed: timed out
        setTarget(i, false);
        scheduleTarget(i);
        break;
      }
      if (upFor < SETTLE_MS) break;              // still swinging up

      int reading = analogRead(ldrPins[i]);
      bool laserRecentlyOn = laserOn || (now - laserOffAt < LASER_AFTERGLOW_MS);

      if (baseline[i] < 0) {                     // learn this target's normal light level
        if (!laserRecentlyOn) baseline[i] = reading;
        break;
      }
      if (laserOn && !(ONE_HIT_PER_SHOT && shotScored) && reading < baseline[i] - HIT_DROP) {
        registerHit(i);
        break;
      }
      // Slowly follow changes in room light (ignore readings lit by the laser)
      if (!laserRecentlyOn && reading > baseline[i] - HIT_DROP / 2) {
        baseline[i] = (baseline[i] * 7 + reading) / 8;
      }
      break;
    }
  }
}

// =====================================================================
// GAME FLOW
// =====================================================================
void startNewGame() {
  score = 0; shots = 0; hits = 0;
  for (int r = 0; r < NUM_ROUNDS; r++) roundScores[r] = 0;
  lastHitTarget = -1; lastHitPoints = 0;
  snprintf(gameId, sizeof(gameId), "%08lx", (unsigned long)esp_random());
  allTargetsDown();   // targets lie down when START is pressed
  gameState = COUNTDOWN;
  gameStateStart = millis();
  lastSecondShown = -1;
  Serial.println("Get ready!");
}

void beginRound(int r) {
  currentRound = r;
  ammo = MAG_SIZE;
  reloading = false;
  emptyWarned = false;
  lastShotAt = millis();
  for (int i = 0; i < NUM_TARGETS; i++) scheduleTarget(i);
  gameState = ROUND;
  gameStateStart = millis();
  lastSecondShown = -1;
  PLAY(SND_GO);
  Serial.printf("=== ROUND %d START ===\n", r + 1);
}

void endRound() {
  allTargetsDown();
  laserOff();
  Serial.printf("=== ROUND %d FINISHED ===\n", currentRound + 1);
  if (currentRound >= NUM_ROUNDS - 1) {
    gameState = ENTER_NAME;
    gameStateStart = millis();
    PLAY(SND_GAMEOVER);
    Serial.printf("=== GAME OVER ===  Final Score: %d\n", score);
    Serial.println("Waiting for the player's name on the dashboard...");
    Serial.println("(Backup: type a name here in the Serial Monitor and press Enter)");
  } else {
    gameState = BREAK;
    gameStateStart = millis();
    lastSecondShown = -1;
    PLAY(SND_ROUND_END);
  }
}

void resetGame() {
  allTargetsUp();     // idle = targets standing
  laserOff();
  reloading = false;
  score = 0; shots = 0; hits = 0;
  for (int r = 0; r < NUM_ROUNDS; r++) roundScores[r] = 0;
  gameState = WAITING;
  PLAY(SND_RESET);
  Serial.println("Game reset. Waiting for next player to press START...");
}

void countdownTick(unsigned long total, const char* label) {
  unsigned long elapsed = millis() - gameStateStart;
  int secondsLeft = (int)((total - elapsed + 999) / 1000);
  if (secondsLeft != lastSecondShown) {
    lastSecondShown = secondsLeft;
    Serial.printf("%s %d...\n", label, secondsLeft);
    if (!sndNotes) PLAY(SND_TICK);   // don't cut off another sound that is still playing
  }
}

void updateGame() {
  unsigned long elapsed = millis() - gameStateStart;
  switch (gameState) {
    case WAITING:
      if (btnStart.pressed) startNewGame();
      break;

    case COUNTDOWN:
      if (elapsed >= START_COUNTDOWN_MS) beginRound(0);
      else countdownTick(START_COUNTDOWN_MS, "Starting in");
      break;

    case ROUND: {
      for (int i = 0; i < NUM_TARGETS; i++) handleTarget(i);
      unsigned long el = millis() - gameStateStart;
      int left = (el >= ROUND_MS) ? 0 : (int)((ROUND_MS - el + 999) / 1000);
      if (left != lastSecondShown && left > 0) {
        lastSecondShown = left;
        if (USE_AMMO) {
          Serial.printf("Round %d | %2d s left | Score: %d | Ammo: %d/%d%s\n",
                        currentRound + 1, left, score, ammo, MAG_SIZE,
                        reloading ? " (reloading)" : "");
        } else {
          Serial.printf("Round %d | %2d s left | Score: %d\n", currentRound + 1, left, score);
        }
      }
      if (millis() - gameStateStart >= ROUND_MS) endRound();
      break;
    }

    case BREAK:
      if (elapsed >= BREAK_MS) beginRound(currentRound + 1);
      else countdownTick(BREAK_MS, "Next round in");
      break;

    case ENTER_NAME:
      // The START button is ignored here on purpose:
      // a new game can only start after the name is saved (or RESET is pressed).
      break;
  }
}

// =====================================================================
// LEADERBOARD + SAVING
// =====================================================================
void fillEntry(Entry& e, const char* id, const char* name, int sc, int sh, int ht, int num) {
  strncpy(e.id, id, sizeof(e.id) - 1);   e.id[sizeof(e.id) - 1] = 0;
  strncpy(e.name, name, MAX_NAME_LEN);   e.name[MAX_NAME_LEN] = 0;
  e.score = sc; e.shots = sh; e.hits = ht; e.num = num;
}

void addRecent(const char* id, const char* name, int sc, int sh, int ht, int num) {
  int last = (recentCount < RECENT_SIZE) ? recentCount : RECENT_SIZE - 1;
  for (int i = last; i > 0; i--) recent[i] = recent[i - 1];
  fillEntry(recent[0], id, name, sc, sh, ht, num);
  if (recentCount < RECENT_SIZE) recentCount++;
}

void addToTop(const char* id, const char* name, int sc, int sh, int ht, int num) {
  int pos = topCount;
  for (int i = 0; i < topCount; i++) {
    if (sc > top[i].score) { pos = i; break; }
  }
  if (pos >= LEADERBOARD_SIZE) return;
  int last = (topCount < LEADERBOARD_SIZE) ? topCount : LEADERBOARD_SIZE - 1;
  for (int i = last; i > pos; i--) top[i] = top[i - 1];
  fillEntry(top[pos], id, name, sc, sh, ht, num);
  if (topCount < LEADERBOARD_SIZE) topCount++;
}

int rankOf(const char* id) {
  for (int i = 0; i < topCount; i++) if (strcmp(top[i].id, id) == 0) return i + 1;
  return 0;
}

// Only letters, numbers, space, - and _ ; uppercase like an arcade machine.
String cleanName(String raw) {
  String out = "";
  raw.trim();
  for (unsigned int i = 0; i < raw.length() && out.length() < (unsigned)MAX_NAME_LEN; i++) {
    char c = raw[i];
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_') out += c;
  }
  out.trim();
  if (out.length() == 0) out = "PLAYER";
  return out;
}

String resultLine(const char* id, const String& name) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s,%d,%d,%d,%d,%d,%d,%s\n", id, score,
           roundScores[0], roundScores[1], roundScores[2], shots, hits, name.c_str());
  return String(buf);
}

void appendToFile(const char* path, const String& line) {
  if (!fsOk) return;
  File f = LittleFS.open(path, FILE_APPEND);
  if (!f) { Serial.printf("Could not open %s\n", path); return; }
  f.print(line);
  f.close();
}

int countLines(const char* path) {
  if (!fsOk) return 0;
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return 0;
  int n = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length() > 0) n++;
  }
  f.close();
  return n;
}

void saveResult(String rawName) {
  String name = cleanName(rawName);
  String line = resultLine(gameId, name);
  appendToFile(SCORES_FILE, line);    // permanent local copy (survives power loss)
  appendToFile(PENDING_FILE, line);   // waiting to be uploaded to Supabase
  pendingCount++;
  totalGames++;
  addToTop(gameId, name.c_str(), score, shots, hits, totalGames);
  addRecent(gameId, name.c_str(), score, shots, hits, totalGames);
  strncpy(lastSavedId, gameId, sizeof(lastSavedId));
  Serial.printf("Saved: %s - %d points\n", name.c_str(), score);
  PLAY(SND_SAVED);
  allTargetsUp();     // ready for the next player
  gameState = WAITING;
  nextSyncAt = millis() + 1500;
  Serial.println("Press START for the next player!");
}

void loadScores() {
  if (!fsOk) return;
  File f = LittleFS.open(SCORES_FILE, FILE_READ);
  if (f) {
    while (f.available()) {
      String l = f.readStringUntil('\n');
      char id[12], name[MAX_NAME_LEN + 4];
      int sc, r1, r2, r3, sh, ht;
      if (sscanf(l.c_str(), "%11[^,],%d,%d,%d,%d,%d,%d,%15[^\r\n]", id, &sc, &r1, &r2, &r3, &sh, &ht, name) == 8) {
        name[MAX_NAME_LEN] = 0;
        totalGames++;
        addToTop(id, name, sc, sh, ht, totalGames);
        addRecent(id, name, sc, sh, ht, totalGames);
      }
    }
    f.close();
  }
  pendingCount = countLines(PENDING_FILE);
  Serial.printf("Loaded leaderboard (%d shown), %d score(s) waiting to upload\n", topCount, pendingCount);
}

// =====================================================================
// SUPABASE UPLOAD
// =====================================================================
bool supabaseConfigured() {
  return strstr(SUPABASE_URL, "YOUR-PROJECT") == nullptr && strstr(SUPABASE_KEY, "YOUR_") == nullptr;
}

// Returns true if the line is done (uploaded, duplicate, or broken).
bool uploadLine(const String& line) {
  char id[12], name[MAX_NAME_LEN + 4];
  int sc, r1, r2, r3, sh, ht;
  if (sscanf(line.c_str(), "%11[^,],%d,%d,%d,%d,%d,%d,%15[^\r\n]", id, &sc, &r1, &r2, &r3, &sh, &ht, name) != 8) {
    Serial.println("Skipping a broken saved line.");
    return true;
  }
  name[MAX_NAME_LEN] = 0;

  WiFiClientSecure client;
  client.setInsecure();   // OK for a school project; skips certificate checking
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  String url = String(SUPABASE_URL) + "/rest/v1/scores";
  if (!http.begin(client, url)) return false;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.addHeader("apikey", SUPABASE_KEY);
  // New "sb_publishable_..." keys go ONLY in the apikey header.
  // Old "eyJ..." anon keys also need the Authorization header.
  if (strncmp(SUPABASE_KEY, "sb_", 3) != 0) {
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  }

  char body[256];
  snprintf(body, sizeof(body),
           "{\"game_id\":\"%s\",\"player_name\":\"%s\",\"score\":%d,"
           "\"round1\":%d,\"round2\":%d,\"round3\":%d,\"shots\":%d,\"hits\":%d}",
           id, name, sc, r1, r2, r3, sh, ht);

  int code = http.POST((uint8_t*)body, strlen(body));
  String resp = (code > 0) ? http.getString() : "";
  http.end();

  if (code == 200 || code == 201 || code == 204) {
    Serial.printf("Uploaded %s (%d) to Supabase\n", name, sc);
    return true;
  }
  if (code == 409) {   // already uploaded before (game_id is unique)
    Serial.printf("%s was already in Supabase\n", name);
    return true;
  }
  Serial.printf("Supabase upload failed (code %d) %s\n", code, resp.c_str());
  if (code == 401) Serial.println("  -> Check SUPABASE_KEY.");
  if (code == 404) Serial.println("  -> Check SUPABASE_URL and that the table is named 'scores'.");
  if (code == 403 || resp.indexOf("42501") >= 0) Serial.println("  -> Run the SQL policy step in the guide.");
  return false;
}

void syncPending() {
  if (!fsOk || pendingCount <= 0) return;
  if (gameState != WAITING) return;              // never upload during a game (it would freeze it)
  if (WiFi.status() != WL_CONNECTED) return;
  if (!supabaseConfigured()) return;
  if ((long)(millis() - nextSyncAt) < 0) return;

  File f = LittleFS.open(PENDING_FILE, FILE_READ);
  if (!f) { pendingCount = 0; return; }
  String first = f.readStringUntil('\n');
  String rest = f.readString();
  f.close();
  first.trim();

  bool done = (first.length() == 0) ? true : uploadLine(first);
  if (done) {
    File w = LittleFS.open(PENDING_FILE, FILE_WRITE);
    if (w) { w.print(rest); w.close(); }
    pendingCount = countLines(PENDING_FILE);
    nextSyncAt = millis() + 500;
  } else {
    nextSyncAt = millis() + 15000;   // try again in 15 seconds
  }
}

// =====================================================================
// DASHBOARD WEB PAGE  (served at http://192.168.4.1)
// =====================================================================
const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Target Frenzy</title>
<style>
@font-face{font-family:"Rye";src:url(/f/rye.woff2) format("woff2");font-display:swap}
@font-face{font-family:"Barlow Condensed";src:url(/f/bc700.woff2) format("woff2");font-weight:700;font-display:swap}
@font-face{font-family:"Barlow";src:url(/f/b500.woff2) format("woff2");font-weight:400 500;font-display:swap}

:root{
  --tent:#b3131d;      /* tent canvas red: page */
  --tent-hi:#e0323b;   /* timer warning */
  --ink:#1b1411;       /* panels */
  --ink-line:#3b2f29;  /* rules, empty slots */
  --paper:#f6f1e9;     /* text on ink, ticket */
  --muted:#c3b6ab;     /* secondary text on ink */
  --bulb:#ffc21a;      /* marquee bulb: score, leader, live targets */
  --r-lg:14px; --r-sm:8px;
}
*{box-sizing:border-box;margin:0;padding:0}
html{background:var(--tent)}
body{font:500 17px/1.5 "Barlow",system-ui,sans-serif;color:var(--paper);min-height:100vh;padding-bottom:56px}
.num{font-family:"Barlow Condensed",system-ui,sans-serif;font-weight:700;font-variant-numeric:tabular-nums}
[hidden]{display:none!important}

/* header: centred title with a tent valance edge */
header{position:relative;background:var(--ink);padding:18px 24px 12px;text-align:center}
header::after{content:"";position:absolute;left:0;right:0;top:100%;height:16px;
  background:radial-gradient(circle at 16px 0,var(--ink) 15.5px,transparent 16px) 0 0/32px 16px repeat-x}
h1{font:400 clamp(36px,4.8vw,58px)/1 "Rye",Georgia,serif;letter-spacing:.01em}

/* the two views fade into each other */
.view{max-width:1240px;margin:40px auto 0;padding:0 24px;transition:opacity .6s ease-in-out}
.view.fading{opacity:0}

.grid-game{display:grid;grid-template-columns:minmax(0,1fr) 340px;gap:22px}
.grid-board{display:grid;grid-template-columns:minmax(0,3fr) minmax(0,2fr);gap:22px;align-items:start}
@media(max-width:900px){.grid-game,.grid-board{grid-template-columns:1fr}}
/* big screens / TVs: use the extra room */
@media(min-width:1600px){
  .view{max-width:1500px;margin-top:56px}
  .grid-game{grid-template-columns:minmax(0,1fr) 440px}
  #score{font-size:260px}.call{font-size:54px}.phase{font-size:24px}
  .ring{width:96px;height:96px}.targets{gap:56px;margin-top:44px}
  .ammo i{width:11px;height:32px}.ammo-label{font-size:24px}
  .list .nm,.list .sc{font-size:28px}.list li{padding:12px 10px}
}
.panel{background:var(--ink);border-radius:var(--r-lg);padding:28px 32px}

/* ---------- game panel ---------- */
.phase{font:700 20px/1.2 "Barlow Condensed",sans-serif;letter-spacing:.06em;text-transform:uppercase;color:var(--muted)}
.call{font:700 clamp(30px,3.6vw,46px)/1.1 "Barlow Condensed",sans-serif;margin-top:4px;text-wrap:balance}
.score-row{display:flex;align-items:baseline;gap:14px;margin:18px 0 6px;position:relative}
#score{font-size:clamp(120px,15vw,210px);line-height:.85;color:var(--bulb)}
.unit{font-size:20px;color:var(--muted)}
#pop{position:absolute;left:0;top:-8px;font-size:60px;color:var(--bulb);opacity:0;pointer-events:none}
#pop.go{animation:rise .7s ease-out}
@keyframes rise{from{opacity:1;transform:translateY(0)}to{opacity:0;transform:translateY(-36px)}}

.timer{display:flex;align-items:center;gap:16px;margin-top:22px;min-height:40px}
.bar{flex:1;height:10px;border-radius:5px;background:var(--ink-line);overflow:hidden}
#bar{display:block;height:100%;width:0;background:var(--paper);border-radius:5px;transition:width .2s linear}
#bar.low{background:var(--tent-hi)}
#time{font-size:34px;min-width:4.2ch;text-align:right}

.targets{list-style:none;display:flex;justify-content:center;gap:40px;margin-top:34px}
.targets li{display:flex;flex-direction:column;align-items:center;gap:6px;font-size:14px;color:var(--muted)}
.ring{width:72px;height:72px;border-radius:50%;border:3px solid var(--ink-line);transition:transform .15s ease-out,background-color .15s}
li.up .ring{border-color:var(--bulb);background:radial-gradient(circle,var(--tent) 0 22%,var(--paper) 23% 46%,var(--tent) 47% 70%,var(--paper) 71%)}
li.hit .ring{background:var(--bulb);border-color:var(--bulb);transform:scale(1.08)}
li.up,li.hit{color:var(--paper)}

.ammo-row{display:flex;align-items:center;justify-content:center;gap:16px;margin-top:28px;min-height:30px;flex-wrap:wrap}
.ammo-label{font:700 20px/1 "Barlow Condensed",sans-serif;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);white-space:nowrap}
.ammo-label b{color:var(--paper);font-variant-numeric:tabular-nums;margin-left:6px;letter-spacing:0}
.ammo{display:flex;align-items:center;gap:5px;flex-wrap:wrap}
.ammo i{width:9px;height:26px;border-radius:3px;background:var(--bulb)}
.ammo i.spent{background:var(--ink-line)}
.ammo .note{font:700 22px "Barlow Condensed",sans-serif;letter-spacing:.04em}

.minor{display:flex;gap:28px;margin-top:22px;padding-top:18px;border-top:1px solid var(--ink-line);color:var(--muted)}
.minor b{font:700 22px "Barlow Condensed",sans-serif;color:var(--paper);margin-left:6px}

/* ---------- score lists ---------- */
.board{display:flex;flex-direction:column}
h2.list-title{font:700 26px/1.1 "Barlow Condensed",sans-serif;margin-bottom:14px}
.list{list-style:none}
.list li{display:grid;grid-template-columns:2.4ch 1fr auto;gap:10px;align-items:baseline;padding:9px 8px;border-bottom:1px solid var(--ink-line)}
.list .rk{font:700 18px "Barlow Condensed",sans-serif;color:var(--muted)}
.list .nm{font:700 23px/1.15 "Barlow Condensed",sans-serif}
.list .sc{font:700 23px "Barlow Condensed",sans-serif;font-variant-numeric:tabular-nums}
.list .sub{display:block;font:500 14px/1.4 "Barlow",sans-serif;color:var(--muted)}
.list li.first .rk,.list li.first .nm,.list li.first .sc{color:var(--bulb)}
.list li.me{background:var(--tent);border-radius:6px;border-color:transparent}
.list li.me .rk,.list li.me .nm,.list li.me .sc,.list li.me .sub{color:#fff}
.list.compact li{grid-template-columns:1fr auto}
.list.compact .nm,.list.compact .sc{font-size:20px}
.empty{color:var(--muted);font-size:16px;padding:8px 0}

.summary{font:700 clamp(26px,3vw,36px)/1.25 "Barlow Condensed",sans-serif;color:#fff;margin-bottom:24px;text-wrap:balance}
.summary b{color:var(--bulb)}

/* swap button: colour eases in and out on hover */
.swap{margin-top:auto;padding-top:22px}
.swap button{width:100%;height:50px;border:0;border-radius:var(--r-sm);cursor:pointer;
  background:var(--ink-line);color:var(--paper);
  font:700 21px "Barlow Condensed",sans-serif;letter-spacing:.03em;
  transition:background-color .35s ease-in-out,color .35s ease-in-out}
.swap button:hover,.swap button:focus-visible{background:var(--bulb);color:var(--ink);outline:none}

/* ---------- game-over ticket ---------- */
#overlay{position:fixed;inset:0;background:rgba(14,10,8,.84);display:none;align-items:center;justify-content:center;padding:20px;z-index:5}
.ticket{position:relative;width:min(460px,100%);background:var(--paper);color:var(--ink);border-radius:var(--r-lg);padding:30px 32px 26px;
  -webkit-mask:radial-gradient(circle 14px at 0 164px,#0000 98%,#000) left/51% 100% no-repeat,radial-gradient(circle 14px at 100% 164px,#0000 98%,#000) right/51% 100% no-repeat;
          mask:radial-gradient(circle 14px at 0 164px,#0000 98%,#000) left/51% 100% no-repeat,radial-gradient(circle 14px at 100% 164px,#0000 98%,#000) right/51% 100% no-repeat}
.ticket h2{font:400 46px/1 "Rye",Georgia,serif;color:var(--tent)}
.final{margin-top:10px;font-size:18px;color:#5a4b42}
#final{font-size:72px;line-height:.9;color:var(--ink);margin-right:8px}
.perf{border:0;border-top:2px dashed #cbbfb2;margin:22px -4px 18px}
.ticket label{display:block;font-size:16px;margin-bottom:6px}
#name{width:100%;height:58px;border:2px solid var(--ink);border-radius:8px;padding:0 14px;font:700 30px "Barlow Condensed",sans-serif;text-transform:uppercase;background:#fff;color:var(--ink)}
#name:focus{outline:3px solid var(--bulb);outline-offset:1px}
.save{margin-top:14px;width:100%;height:58px;border:0;border-radius:8px;background:var(--tent);color:#fff;font:700 26px "Barlow Condensed",sans-serif;letter-spacing:.03em;cursor:pointer;transition:background-color .3s ease-in-out}
.save:hover{background:#8f0e17}
#msg{min-height:1.5em;margin-top:10px;font-size:16px}

/* ---------- quiet corner info ---------- */
#conn{position:fixed;right:14px;bottom:10px;font-size:12px;color:#f3c9c4;opacity:.8}
#sound{position:fixed;left:14px;bottom:10px;height:40px;padding:0 16px;border-radius:var(--r-sm);border:0;background:var(--ink);color:var(--paper);font:500 15px "Barlow",sans-serif;cursor:pointer;transition:background-color .3s ease-in-out,color .3s ease-in-out}
#sound:hover{background:var(--bulb);color:var(--ink)}

/* ---------- phones ---------- */
@media(max-width:600px){
  body{font-size:16px;padding-bottom:0}
  header{padding:14px 14px 8px}
  .view{margin-top:30px;padding:0 12px}
  .grid-game,.grid-board{gap:14px}
  .panel{padding:20px 18px}
  .call{font-size:28px}
  #score{font-size:104px}
  #pop{font-size:44px}
  #time{font-size:26px}
  .targets{gap:16px;margin-top:26px}
  .ring{width:58px;height:58px}
  .ammo-row{gap:10px}
  .ammo i{width:6px;height:20px;border-radius:2px}
  .ammo{gap:3px}
  .minor{gap:18px}
  .list li{padding:8px 4px}
  .list .nm,.list .sc{font-size:20px}
  .summary{font-size:24px;margin-bottom:16px}
  .swap button{height:48px;font-size:19px}
  .ticket{padding:30px 20px 20px}
  /* corner info goes into the page flow so nothing covers the game */
  #sound{position:static;display:block;margin:16px auto 0}
  #conn{position:static;text-align:center;padding:10px 12px 16px}
}
/* phones held sideways / short screens */
@media(max-height:500px) and (orientation:landscape){
  #score{font-size:90px}
  .view{margin-top:26px}
  .targets{margin-top:18px}
}
</style></head><body>

<header><h1>Target Frenzy</h1></header>

<!-- VIEW 1: the live game -->
<div id="viewGame" class="view">
  <div class="grid-game">
    <section class="panel game" aria-live="polite">
      <p id="phase" class="phase">Loading</p>
      <h2 id="status" class="call">&nbsp;</h2>
      <div class="score-row">
        <span id="score" class="num">0</span><span class="unit">points</span>
        <span id="pop" class="num"></span>
      </div>
      <div class="timer"><div class="bar"><i id="bar"></i></div><span id="time" class="num"></span></div>
      <ul id="targets" class="targets"></ul>
      <div id="ammoRow" class="ammo-row" hidden>
        <span class="ammo-label">Ammo<b id="ammoCount"></b></span>
        <div id="ammo" class="ammo"></div>
      </div>
      <p class="minor"><span>Hits<b id="hits">0</b></span><span>Accuracy<b id="acc">–</b></span></p>
    </section>

    <aside class="panel board">
      <h2 class="list-title">Top scores</h2>
      <ol id="lb" class="list"></ol>
      <div class="swap"><button type="button" class="swapBtn">Show full leaderboard</button></div>
    </aside>
  </div>
</div>

<!-- VIEW 2: the leaderboard -->
<div id="viewBoard" class="view" hidden>
  <p id="summary" class="summary">&nbsp;</p>
  <div class="grid-board">
    <section class="panel board">
      <h2 class="list-title">All-time top 10</h2>
      <ol id="top10" class="list"></ol>
      <div class="swap"><button type="button" class="swapBtn">Back to the game</button></div>
    </section>
    <section class="panel">
      <h2 class="list-title">Latest games</h2>
      <ol id="recent" class="list compact"></ol>
    </section>
  </div>
</div>

<div id="overlay" role="dialog" aria-modal="true" aria-labelledby="overTitle">
  <form id="nameForm" class="ticket">
    <h2 id="overTitle">Game Over</h2>
    <p class="final"><span id="final" class="num">0</span>points</p>
    <hr class="perf">
    <label for="name">Your name for the leaderboard</label>
    <input id="name" maxlength="12" autocomplete="off" spellcheck="false">
    <button class="save" type="submit">Save my score</button>
    <p id="msg"></p>
  </form>
</div>

<button id="sound" type="button">Turn on sound</button>
<p id="conn">Connecting to the game…</p>

<script>
let ctx=null,last=null,lastSeq=-1,myId='',flashT=null,builtTargets=0,board=null;
const $=id=>document.getElementById(id);

/* ---------------- sounds ---------------- */
function beep(f,d,t=0,type='square',vol=.12){if(!ctx)return;const o=ctx.createOscillator(),g=ctx.createGain();o.type=type;
  o.frequency.value=f;const s=ctx.currentTime+t;g.gain.setValueAtTime(vol,s);g.gain.exponentialRampToValueAtTime(.001,s+d);
  o.connect(g);g.connect(ctx.destination);o.start(s);o.stop(s+d);}
function zapAt(t){  // laser "pew": a fast downward pitch sweep, scheduled at an exact time
  const o=ctx.createOscillator(),g=ctx.createGain();
  o.type='sawtooth';o.frequency.setValueAtTime(1800,t);o.frequency.exponentialRampToValueAtTime(180,t+.12);
  g.gain.setValueAtTime(.0001,t);g.gain.exponentialRampToValueAtTime(.08,t+.005);g.gain.exponentialRampToValueAtTime(.0001,t+.13);
  o.connect(g);g.connect(ctx.destination);o.start(t);o.stop(t+.14);}
function zap(){if(ctx)zapAt(ctx.currentTime)}

// Steady laser beat: while the gun is firing, one "pew" every fireMs, timed by the audio clock (no wobble)
let laserOn=false,nextZap=0,fireSec=.2;
function startLaser(ms){if(!ctx)return;fireSec=ms/1000;if(!laserOn){laserOn=true;nextZap=ctx.currentTime+.01}}
function stopLaser(){laserOn=false}
setInterval(()=>{if(!ctx||!laserOn)return;while(nextZap<ctx.currentTime+.1){zapAt(nextZap);nextZap+=fireSec}},25);

function noiseClick(t,freq,dur,vol){  // short mechanical click (for the "ka-chunk")
  const n=Math.floor(ctx.sampleRate*dur),buf=ctx.createBuffer(1,n,ctx.sampleRate),d=buf.getChannelData(0);
  for(let i=0;i<n;i++)d[i]=(Math.random()*2-1)*(1-i/n);
  const src=ctx.createBufferSource(),f=ctx.createBiquadFilter(),g=ctx.createGain();
  src.buffer=buf;f.type='bandpass';f.frequency.value=freq;f.Q.value=2;g.gain.value=vol;
  src.connect(f);f.connect(g);g.connect(ctx.destination);src.start(t);}

// Reload: an arcade "charging up" whine that rises for the whole reload time
let charge=null;
function startCharge(ms){
  if(!ctx)return;stopCharge();
  const t=ctx.currentTime,d=Math.max(.3,ms/1000-.05);
  const o=ctx.createOscillator(),g=ctx.createGain(),lfo=ctx.createOscillator(),lg=ctx.createGain();
  o.type='square';o.frequency.setValueAtTime(140,t);o.frequency.exponentialRampToValueAtTime(1400,t+d);
  lfo.frequency.setValueAtTime(8,t);lfo.frequency.linearRampToValueAtTime(28,t+d);lg.gain.value=.025;   // warble that speeds up
  g.gain.setValueAtTime(.0001,t);g.gain.exponentialRampToValueAtTime(.045,t+.08);g.gain.setValueAtTime(.045,t+d-.05);g.gain.exponentialRampToValueAtTime(.0001,t+d);
  lfo.connect(lg);lg.connect(g.gain);o.connect(g);g.connect(ctx.destination);
  o.start(t);lfo.start(t);o.stop(t+d+.02);lfo.stop(t+d+.02);
  charge={o,lfo};
}
function stopCharge(){if(charge){try{charge.o.stop();charge.lfo.stop()}catch(e){}charge=null}}

// Fully reloaded: "ka-chunk" (gun cocking) + bright ready chime
function readyCue(){
  if(!ctx)return;const t=ctx.currentTime;
  noiseClick(t,2600,.03,.5);noiseClick(t+.09,1200,.05,.6);
  beep(1568,.09,.17,'square',.09);beep(2093,.22,.26,'square',.09);
}

const SFX={hit(){beep(1046,.08);beep(1568,.18,.08)},tick(){beep(660,.1)},go(){beep(1320,.35)},
  over(){[523,659,784,1047].forEach((f,i)=>beep(f,.2,i*.18))},saved(){[1047,1319,1568].forEach((f,i)=>beep(f,.12,i*.1))}};

$('sound').onclick=()=>{ctx=new (window.AudioContext||window.webkitAudioContext)();SFX.saved();$('sound').hidden=true};

function pop(t){const p=$('pop');p.textContent=t;p.classList.remove('go');void p.offsetWidth;p.classList.add('go')}

/* ---------------- two views: game <-> leaderboard ---------------- */
const SWAP_EVERY_MS=60000;   // auto-swap every minute, only while waiting for a player
let view='game',lastSwap=Date.now();
function showView(v){
  if(v===view)return;
  const out=view==='game'?$('viewGame'):$('viewBoard'),inn=v==='game'?$('viewGame'):$('viewBoard');
  view=v;lastSwap=Date.now();
  out.classList.add('fading');
  setTimeout(()=>{out.hidden=true;inn.classList.add('fading');inn.hidden=false;
    requestAnimationFrame(()=>requestAnimationFrame(()=>inn.classList.remove('fading')))},600);
  if(v==='board')loadBoard();
}
document.querySelectorAll('.swapBtn').forEach(b=>b.onclick=()=>showView(view==='game'?'board':'game'));
setInterval(()=>{
  const idle=!last||last.state==='WAITING';
  if(!idle&&view!=='game')showView('game');                       // a game started: show it right away
  else if(idle&&Date.now()-lastSwap>=SWAP_EVERY_MS)showView(view==='game'?'board':'game');
},1000);

/* ---------------- game view ---------------- */
function buildTargets(n){
  const ul=$('targets');ul.innerHTML='';
  for(let i=0;i<n;i++){const li=document.createElement('li');
    li.innerHTML='<span class="ring"></span><span>Target '+(i+1)+'</span>';ul.appendChild(li)}
  builtTargets=n;
}

function render(s){
  const prev=last;last=s;
  if(builtTargets!==s.targets.length)buildTargets(s.targets.length);

  $('score').textContent=s.score;
  const phase={WAITING:'Next player',COUNTDOWN:'Get ready',ROUND:'Round '+s.round+' of '+s.rounds,
    BREAK:'Round '+s.round+' of '+s.rounds+' done',ENTER_NAME:'Game over'};
  const call={WAITING:'Press the red START button',COUNTDOWN:'Starting in '+s.countdown,
    ROUND:'Shoot!',BREAK:'Next round in '+s.countdown,ENTER_NAME:'Type your name to save the score'};
  $('phase').textContent=phase[s.state]||s.state;
  $('status').textContent=call[s.state]||'';

  const playing=s.state==='ROUND';
  $('time').textContent=playing?(s.timeLeft/1000).toFixed(1)+'s':'';
  $('bar').style.width=playing?Math.max(0,100*s.timeLeft/s.roundMs)+'%':'0';
  $('bar').classList.toggle('low',playing&&s.timeLeft<3000);

  $('hits').textContent=s.hits;
  $('acc').textContent=s.shots?Math.round(100*s.hits/s.shots)+'%':'–';

  const lis=$('targets').children;
  for(let i=0;i<lis.length;i++){if(!lis[i].classList.contains('hit'))lis[i].className=s.targets[i]?'up':''}

  // ammo: always shown; full magazine while no round is running
  $('ammoRow').hidden=!(s.mag>0);
  if(s.mag>0){
    const reloading=playing&&s.reloading, ammo=playing?s.ammo:s.mag;
    let a='';
    if(reloading){a='<span class="note">Reloading…</span>';$('ammoCount').textContent='';}
    else{for(let i=0;i<s.mag;i++)a+='<i class="'+(i<ammo?'':'spent')+'"></i>';$('ammoCount').textContent=ammo+' / '+s.mag;}
    $('ammo').innerHTML=a;
  }

  // laser: steady beat while firing (hold-to-fire), or one "pew" per shot (pistol mode)
  if(playing&&s.holdFire){if(s.firing)startLaser(s.fireMs);else stopLaser()}
  else{stopLaser();if(prev&&playing&&s.shots>prev.shots)zap()}

  // reload: charging sound for the whole reload, then the "ready" cue
  if(prev&&playing&&prev.state==='ROUND'){
    if(s.reloading&&!prev.reloading)startCharge(s.reloadMs);
    if(!s.reloading&&prev.reloading){stopCharge();readyCue()}
  }
  if(!playing)stopCharge();

  if(lastSeq>=0&&s.hitSeq>lastSeq){
    SFX.hit();pop('+'+s.lastHitPts);
    const t=lis[s.lastHitTarget];
    if(t){t.className='hit';clearTimeout(flashT);flashT=setTimeout(()=>{for(const x of lis)x.classList.remove('hit')},280)}
  }
  lastSeq=s.hitSeq;

  if(prev){
    if((s.state==='COUNTDOWN'||s.state==='BREAK')&&s.countdown!==prev.countdown)SFX.tick();
    if(playing&&prev.state!=='ROUND')SFX.go();
    if(s.state==='ENTER_NAME'&&prev.state!=='ENTER_NAME'){SFX.over();$('name').value='';$('msg').textContent=''}
    if(s.state!==prev.state||s.lastId!==prev.lastId)loadBoard();
  }

  $('overlay').style.display=s.state==='ENTER_NAME'?'flex':'none';
  if(s.state==='ENTER_NAME'){$('final').textContent=s.score;if(document.activeElement!==$('name'))$('name').focus()}

  $('conn').textContent='Game connected · Internet '+(s.wifi?'on':'off')+' · '+
    (s.pending?s.pending+(s.pending===1?' score':' scores')+' waiting to upload':'all scores saved to the cloud');
}

async function poll(){
  try{const r=await fetch('/state',{cache:'no-store'});render(await r.json())}
  catch(e){$('conn').textContent='Lost connection to the game. Retrying…'}
  setTimeout(poll,last&&last.state==='ROUND'?100:250);   // faster during a round so the laser sound stays in sync
}

/* ---------------- leaderboard data (from the ESP32) ---------------- */
function row(e,o){
  const li=document.createElement('li');
  if(o.first)li.classList.add('first');
  if(e.id===(myId||(last&&last.lastId)))li.classList.add('me');
  if(o.rank){const rk=document.createElement('span');rk.className='rk';rk.textContent=o.rank;li.appendChild(rk)}
  const nm=document.createElement('span');nm.className='nm';nm.textContent=e.name;
  if(o.sub){const sub=document.createElement('span');sub.className='sub';sub.textContent=o.sub;nm.appendChild(sub)}
  li.appendChild(nm);
  const sc=document.createElement('span');sc.className='sc';sc.textContent=e.score;li.appendChild(sc);
  return li;
}
function fill(ol,items,mk,emptyText){
  ol.innerHTML='';
  if(!items.length){const li=document.createElement('li');li.className='empty';li.textContent=emptyText;ol.appendChild(li);return}
  items.forEach((e,i)=>ol.appendChild(mk(e,i)));
}
async function loadBoard(){
  if(last&&last.state==='ROUND')return;   // don't load the ESP32 during a round
  try{
    const r=await fetch('/board',{cache:'no-store'});board=await r.json();
    const top=board.top,acc=e=>e.shots?Math.round(100*e.hits/e.shots)+'% accuracy':'no shots';
    fill($('lb'),top,(e,i)=>row(e,{rank:String(i+1),first:i===0}),'No scores yet. Be the first.');
    fill($('top10'),top,(e,i)=>row(e,{rank:String(i+1),first:i===0,sub:acc(e)+' · game #'+e.num}),'No scores yet.');
    fill($('recent'),board.recent,e=>row(e,{sub:'game #'+e.num}),'Waiting for the first game.');
    const sm=$('summary');
    if(!top.length)sm.textContent='No games yet. The first score sets the record.';
    else{sm.innerHTML='';const b=t=>{const x=document.createElement('b');x.textContent=t;return x};
      sm.append(b(board.total),board.total===1?' game played. Record ':' games played. Record ',b(top[0].score),' by ',b(top[0].name),'.');}
  }catch(e){}
}

$('nameForm').onsubmit=async ev=>{
  ev.preventDefault();
  const n=$('name').value.trim();
  if(!n){$('msg').textContent='Type a name first.';return}
  try{
    const r=await fetch('/name',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:'name='+encodeURIComponent(n)});
    const d=await r.json();
    if(d.ok){myId=d.id;SFX.saved();pop(d.rank?'#'+d.rank:'Saved');
      $('msg').textContent=d.rank?'Saved. You are number '+d.rank+'.':'Saved.';loadBoard()}
    else $('msg').textContent='Could not save: '+d.error;
  }catch(e){$('msg').textContent='Could not reach the game. Try again.'}
};

setInterval(loadBoard,5000);loadBoard();poll();
</script>
</body></html>)HTML";

void handleRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void sendFont(const uint8_t* data, size_t len) {
  server.sendHeader("Cache-Control", "public, max-age=31536000");
  server.send_P(200, "font/woff2", (PGM_P)data, len);
}

void handleState() {
  unsigned long elapsed = millis() - gameStateStart;
  long timeLeft = 0;
  int countdown = 0;
  if (gameState == ROUND) timeLeft = (long)ROUND_MS - (long)elapsed;
  if (gameState == COUNTDOWN) countdown = (int)((START_COUNTDOWN_MS - min(elapsed, START_COUNTDOWN_MS) + 999) / 1000);
  if (gameState == BREAK)     countdown = (int)((BREAK_MS - min(elapsed, BREAK_MS) + 999) / 1000);
  if (timeLeft < 0) timeLeft = 0;

  // "firing" = the laser is shooting right now (the dashboard plays its laser sound on a steady beat)
  bool firing = gameState == ROUND && !reloading && (!USE_AMMO || ammo > 0) &&
                (AUTO_FIRE || (HOLD_TO_FIRE && btnTrigger.stable == LOW));

  char buf[640];
  snprintf(buf, sizeof(buf),
    "{\"state\":\"%s\",\"round\":%d,\"rounds\":%d,\"roundMs\":%lu,\"score\":%d,\"timeLeft\":%ld,\"countdown\":%d,"
    "\"ammo\":%d,\"mag\":%d,\"reloading\":%s,\"shots\":%d,\"hits\":%d,\"hitSeq\":%d,"
    "\"lastHitTarget\":%d,\"lastHitPts\":%d,\"targets\":[%d,%d,%d],"
    "\"firing\":%s,\"holdFire\":%s,\"fireMs\":%lu,\"reloadMs\":%lu,"
    "\"wifi\":%s,\"pending\":%d,\"lastId\":\"%s\"}",
    stateName(), currentRound + 1, NUM_ROUNDS, ROUND_MS, score, timeLeft, countdown,
    ammo, USE_AMMO ? MAG_SIZE : 0, reloading ? "true" : "false", shots, hits, hitSeq,
    lastHitTarget, lastHitPoints,
    tState[0] == T_UP, tState[1] == T_UP, tState[2] == T_UP,
    firing ? "true" : "false", (AUTO_FIRE || HOLD_TO_FIRE) ? "true" : "false", FIRE_INTERVAL_MS, RELOAD_MS,
    WiFi.status() == WL_CONNECTED ? "true" : "false", pendingCount, lastSavedId);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buf);
}

String entryJson(const Entry& e) {
  return "{\"id\":\"" + String(e.id) + "\",\"name\":\"" + String(e.name) + "\",\"score\":" + String(e.score) +
         ",\"shots\":" + String(e.shots) + ",\"hits\":" + String(e.hits) + ",\"num\":" + String(e.num) + "}";
}

// Everything both dashboard views need: top 10, latest games, total games played
void handleBoard() {
  String json = "{\"total\":" + String(totalGames) + ",\"top\":[";
  for (int i = 0; i < topCount; i++) { if (i) json += ","; json += entryJson(top[i]); }
  json += "],\"recent\":[";
  for (int i = 0; i < recentCount; i++) { if (i) json += ","; json += entryJson(recent[i]); }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleLeaderboard() {
  String json = "[";
  for (int i = 0; i < topCount; i++) {
    if (i) json += ",";
    json += "{\"id\":\"" + String(top[i].id) + "\",\"name\":\"" + String(top[i].name) +
            "\",\"score\":" + String(top[i].score) + "}";
  }
  json += "]";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleName() {
  if (gameState != ENTER_NAME) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"the game is not waiting for a name\"}");
    return;
  }
  saveResult(server.arg("name"));
  String json = "{\"ok\":true,\"id\":\"" + String(lastSavedId) + "\",\"rank\":" + String(rankOf(lastSavedId)) + "}";
  server.send(200, "application/json", json);
}

// =====================================================================
// LDR TEST MODE (calibration)
// =====================================================================
void ldrTestLoop() {
  static unsigned long lastPrint = 0;
  // Hold the TRIGGER button = laser stays on, so you can aim at each target.
  digitalWrite(LASER_PIN, btnTrigger.stable == LOW ? HIGH : LOW);
  if (millis() - lastPrint >= 250) {
    lastPrint = millis();
    Serial.printf("LASER %s | T1: %4d   T2: %4d   T3: %4d\n",
                  btnTrigger.stable == LOW ? "ON " : "off",
                  analogRead(ldrPins[0]), analogRead(ldrPins[1]),
                  analogRead(ldrPins[2]));
  }
}

// =====================================================================
// SETUP + LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== TARGET FRENZY ===");

  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(TRIGGER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  for (int i = 0; i < NUM_TARGETS; i++) {
    targetServos[i].setPeriodHertz(50);
    targetServos[i].attach(servoPins[i], 500, 2400);
    pinMode(ledPins[i], OUTPUT);
    setTarget(i, true);   // targets stand up while waiting
    tState[i] = T_IDLE;
  }

  fsOk = LittleFS.begin(true);   // true = format the first time
  if (!fsOk) Serial.println("WARNING: storage not working, scores will only be kept until power off");
  loadScores();

  // WiFi: own network for the dashboard + join the phone hotspot for internet
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(BOOTH_WIFI_NAME, BOOTH_WIFI_PASSWORD);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);   // retries are done slowly in maintainHotspot() so the dashboard WiFi stays steady
  WiFi.begin(HOTSPOT_NAME, HOTSPOT_PASSWORD);
  Serial.printf("Dashboard WiFi: \"%s\" (password: %s)\n", BOOTH_WIFI_NAME, BOOTH_WIFI_PASSWORD);
  Serial.print("Dashboard address: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/state", HTTP_GET, handleState);
  server.on("/leaderboard", HTTP_GET, handleLeaderboard);
  server.on("/board", HTTP_GET, handleBoard);
  server.on("/name", HTTP_POST, handleName);
  server.on("/f/rye.woff2",   HTTP_GET, [] { sendFont(FONT_RYE,   FONT_RYE_LEN); });
  server.on("/f/bc700.woff2", HTTP_GET, [] { sendFont(FONT_BC700, FONT_BC700_LEN); });
  server.on("/f/b500.woff2",  HTTP_GET, [] { sendFont(FONT_B500,  FONT_B500_LEN); });
  server.begin();

  if (LDR_TEST_MODE) {
    Serial.println("*** LDR TEST MODE *** All targets are UP. Hold TRIGGER to turn the laser on.");
  } else {
    Serial.println("Press the START button to begin!");
  }
}

// While the ESP32 searches for the hotspot it hops between WiFi channels, which
// kicks the dashboard laptop off TargetFrenzy. So: only retry once a minute,
// and never during a game.
const unsigned long HOTSPOT_RETRY_MS = 60000;
void maintainHotspot() {
  static unsigned long lastTry = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  if (gameState != WAITING) return;
  if (millis() - lastTry < HOTSPOT_RETRY_MS) return;
  lastTry = millis();
  if (millis() < HOTSPOT_RETRY_MS) return;   // first try already happened in setup()
  Serial.println("Hotspot not connected - trying again...");
  WiFi.disconnect();
  WiFi.begin(HOTSPOT_NAME, HOTSPOT_PASSWORD);
}

void printWifiChanges() {
  static bool wasConnected = false;
  bool now = (WiFi.status() == WL_CONNECTED);
  if (now != wasConnected) {
    wasConnected = now;
    if (now) { Serial.print("Internet hotspot connected. IP: "); Serial.println(WiFi.localIP()); }
    else Serial.println("Internet hotspot disconnected (scores are still saved on the ESP32).");
  }
}

void readSerialName() {
  static String typed = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (typed.length() > 0 && gameState == ENTER_NAME) saveResult(typed);
      typed = "";
    } else if (typed.length() < 40) {
      typed += c;
    }
  }
}

void loop() {
  server.handleClient();
  updateButton(btnStart);
  updateButton(btnReset);
  updateButton(btnTrigger);
  if (btnTrigger.pressed && !LDR_TEST_MODE && gameState != ROUND)
    Serial.println("  (the gun only fires during a round - press START first)");
  updateSound();
  printWifiChanges();
  maintainHotspot();

  if (LDR_TEST_MODE) { ldrTestLoop(); return; }

  if (btnReset.pressed) resetGame();
  updateGun();
  updateGame();
  readSerialName();
  syncPending();
}
