#include "pch-il2cpp.h"
#include "PJDodge.h"
#include "PJDodgeTypes.h"
#include "PJDodgeCore.h"
#include "PJDodgeSensors.h"
#include "PJDodgeDebug.h"

#include "MovementRuntime.h"
#include "ProjectileTracking.h"
#include "SteerInput.h"
#include "DangerPlanner.h"
#include "gui/tabs/TestTAB.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <windows.h>

namespace PJDodge {
namespace {

std::atomic<bool>  g_enabled{ false };
std::atomic<float> g_horizonMs{ 600.f };
std::atomic<float> g_aoeHorizonMs{ 2500.f };
std::atomic<float> g_leadMs{ 40.f };
std::atomic<float> g_hitScale{ 1.0f };
std::atomic<bool>  g_safeWalk{ true };
std::atomic<bool>  g_speedScale{ true };
std::atomic<bool>  g_predictionAccuracy{ true };
std::atomic<bool>  g_debugOverlay{ true };
std::atomic<bool>  g_lockFollow{ false };

// ── Player authority + humanization ─────────────────────────────────────────
// The core already prefers the player's heading when it is safe, but "safe"
// means clearance over the whole horizon; in a busy room that almost never
// holds, so the controller ends up driving through every input the player
// makes. These three knobs decide when the bot is allowed to take the wheel
// and how abruptly it may turn it. Emergencies (impact sooner than
// kEmergencyOverrideMs) ignore all of them — a reflex save is the one thing
// that must never be softened.
std::atomic<bool>  g_manualPriority{ true };
std::atomic<float> g_manualHoldMs{ 350.f };
std::atomic<bool>  g_humanize{ true };
std::atomic<float> g_reactionMs{ 70.f };
std::atomic<float> g_turnRateDeg{ 900.f };

// Game-update thread only.
CoreState  g_state;
Snapshot   g_snapshot;
CoreOutput g_out;

// Game-update thread only — humanization memory across frames.
double g_lastSteerMs      = 0.0;   // last time WASD was held
double g_overrideSinceMs  = 0.0;   // start of the current override episode (0 = none)
double g_lastOverrideWantMs = 0.0; // last frame the core asked for an override
Vec2   g_appliedDir{};             // heading actually issued last frame

std::mutex    g_debugMutex;

// Heap-backed on purpose: as a plain global, MSVC (LTCG) const-promoted the
// identical snapshot in ZDodge into read-only .rdata, and PublishDebug's memcpy
// access-violated on the first byte. This one happens to land in .data today, but
// the promotion picked arbitrarily between identical globals — runtime-allocated
// storage cannot be const-promoted, so it can't regress.
// Intentionally never freed — the render thread may publish during DLL unload.
DebugSnapshot& DebugSlot()
{
    static DebugSnapshot* const slot = new DebugSnapshot();
    return *slot;
}

float Clamp(float value, float lo, float hi)
{
    if (!std::isfinite(value)) return lo;
    return std::clamp(value, lo, hi);
}

// Rotate `from` toward `to` by at most maxRad. Both unit; returns unit.
Vec2 RotateToward(Vec2 from, Vec2 to, float maxRad)
{
    const float cross = from.x * to.y - from.y * to.x;
    const float dot   = Dot(from, to);
    const float delta = std::atan2(cross, dot);
    if (std::fabs(delta) <= maxRad) return to;
    const float step = (delta > 0.f ? maxRad : -maxRad);
    const float cs = std::cos(step), sn = std::sin(step);
    return { from.x * cs - from.y * sn, from.x * sn + from.y * cs };
}

void ResetHumanizeState()
{
    g_lastSteerMs = 0.0;
    g_overrideSinceMs = 0.0;
    g_lastOverrideWantMs = 0.0;
    g_appliedDir = Vec2{};
}

Settings ReadSettings()
{
    Settings s{};
    s.horizonMs    = Clamp(g_horizonMs.load(std::memory_order_relaxed), 200.f, 2000.f);
    s.aoeHorizonMs = Clamp(g_aoeHorizonMs.load(std::memory_order_relaxed), 200.f, 5000.f);
    s.leadMs       = Clamp(g_leadMs.load(std::memory_order_relaxed), 0.f, 250.f);
    s.hitScale     = Clamp(g_hitScale.load(std::memory_order_relaxed), 0.25f, 2.5f);
    s.safeWalk     = g_safeWalk.load(std::memory_order_relaxed);
    s.speedScale   = g_speedScale.load(std::memory_order_relaxed);
    s.predictionAccuracy = g_predictionAccuracy.load(std::memory_order_relaxed);
    s.debugOverlay = g_debugOverlay.load(std::memory_order_relaxed);
    return s;
}

void PublishDebug(const DebugSnapshot& snap)
{
    std::lock_guard<std::mutex> lock(g_debugMutex);
    DebugSlot() = snap;
}

void PublishMinimal(Decision decision, Vec2 player)
{
    DebugSnapshot d{};
    d.active = IsEnabled();
    d.decision = decision;
    d.player = player;
    PublishDebug(d);
}

} // namespace

void SetEnabled(bool enabled)
{
    if (enabled) ProjectileTracking::Install();
    g_enabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        g_state.Reset();
        ResetHumanizeState();
        PublishDebug(DebugSnapshot{});
    }
}

bool IsEnabled() { return g_enabled.load(std::memory_order_relaxed); }

void OnEnter()
{
    ProjectileTracking::Install();
    g_state.Reset();
    ResetHumanizeState();
    PublishDebug(DebugSnapshot{});
}

void Tick(void* player, float px, float py, float dt)
{
    if (!IsEnabled()) return;
    if (!player || !std::isfinite(px) || !std::isfinite(py)) return;
    if (!DodgeRuntime::EnsureResolved()) return;

    const Settings settings = ReadSettings();
    ProjectileTracking::SetPredictionAccuracy(settings.predictionAccuracy);
    const SteerInput::SteerState steer = SteerInput::Get();

    int32_t hp = 0, maxHp = 0;
    float spd = 0.f, tilesPerSec = 0.f;
    TestTAB::ReadDodgePlayerStats(hp, maxHp, spd, tilesPerSec);

    Sensors::Build(g_snapshot, px, py, settings);
    if (g_snapshot.projectileSourceUnavailable) {
        g_state.Reset();
        ResetHumanizeState();
        PublishMinimal(Decision::None, { px, py });
        return;
    }

    CoreInput in{};
    in.player = { px, py };
    in.intentDir = steer.active ? Vec2{ steer.dirX, steer.dirY } : Vec2{};

    if (!steer.active && g_lockFollow.load(std::memory_order_relaxed)) {
        float gx = 0.f, gy = 0.f;
        if (DangerPlanner::GetExternalGoal(gx, gy)) {
            const float dx = gx - px, dy = gy - py;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > 0.3f)
                in.intentDir = { dx / d, dy / d };
        }
    }

    in.moveSpeed = std::max(0.f, std::isfinite(tilesPerSec) ? tilesPerSec : 0.f) / 1000.f;
    in.nowMs = static_cast<double>(GetTickCount64());
    in.movementLocked = false;
    in.playerOnHazard = settings.safeWalk && Sensors::IsHazardAt(px, py);
    in.settings = settings;
    in.env.canOccupy = &Sensors::CanOccupy;
    in.env.isHazard = &Sensors::IsHazardAt;
    in.sensors = &g_snapshot;

    Core::Evaluate(in, g_state, g_out);

    const float frameMs = Clamp(dt * 1000.f, 1.f, 250.f);
    Vec2 moveTarget = in.player;
    bool moveFailed = false;

    // ── Player authority + humanization ─────────────────────────────────────
    // A reflex save is never softened; everything gentler defers.
    const bool emergency = g_out.earliestImpactMs < kEmergencyOverrideMs;

    if (steer.active) g_lastSteerMs = in.nowMs;

    // Episode tracking for the reaction delay. The core can flip override on and
    // off between adjacent frames as a candidate crosses the safe-clearance line;
    // if each flip restarted the episode, the delay would re-arm forever and the
    // dodge would never actually fire. Treat a short gap as the same episode.
    constexpr double kEpisodeGapMs = 150.0;
    if (g_out.overrideActive) {
        if (g_overrideSinceMs == 0.0 || (in.nowMs - g_lastOverrideWantMs) > kEpisodeGapMs)
            g_overrideSinceMs = in.nowMs;
        g_lastOverrideWantMs = in.nowMs;
    } else if (g_overrideSinceMs != 0.0 && (in.nowMs - g_lastOverrideWantMs) > kEpisodeGapMs) {
        g_overrideSinceMs = 0.0;
    }

    bool overrideActive = g_out.overrideActive;
    bool yieldedToPlayer = false;

    // HazardEscape is exempt alongside emergencies: standing in lava carries no
    // "impact time", so it never reads as urgent, and deferring to a held key
    // would leave the player burning on the tile they are already standing on.
    if (overrideActive && !emergency && g_out.decision != Decision::HazardEscape) {
        // Hands on the keys wins. The hold window keeps the controller from
        // snatching the character back between keystrokes, which is what made
        // manual movement feel like fighting the bot.
        const float holdMs = Clamp(g_manualHoldMs.load(std::memory_order_relaxed), 0.f, 2000.f);
        if (g_manualPriority.load(std::memory_order_relaxed) &&
            (in.nowMs - g_lastSteerMs) < static_cast<double>(holdMs)) {
            overrideActive = false;
            yieldedToPlayer = true;
        }
        // A human sees the shot, then moves. Reacting on the same frame the
        // threat becomes relevant is the single most robotic thing here.
        else if (g_humanize.load(std::memory_order_relaxed) &&
                 (in.nowMs - g_overrideSinceMs) <
                     static_cast<double>(Clamp(g_reactionMs.load(std::memory_order_relaxed), 0.f, 250.f))) {
            overrideActive = false;
        }
    }

    // Slew-limit the heading. Snapping between 32 fixed compass directions on
    // consecutive frames is what reads as machine movement; a bounded turn rate
    // produces the same path with a human's wrist on it.
    Vec2 appliedVel = g_out.velocity;
    if (overrideActive && !emergency && g_out.decision != Decision::HazardEscape &&
        g_humanize.load(std::memory_order_relaxed)) {
        const float speedLen = Len(appliedVel);
        if (speedLen > 1e-6f && LenSq(g_appliedDir) > 1e-6f) {
            const float maxRad = Clamp(g_turnRateDeg.load(std::memory_order_relaxed), 90.f, 3600.f)
                               * 0.01745329252f * (frameMs / 1000.f);
            appliedVel = Mul(RotateToward(g_appliedDir, Mul(appliedVel, 1.f / speedLen), maxRad), speedLen);
        }
    }
    g_appliedDir = overrideActive ? Normalize(appliedVel) : Vec2{};

    // Lock follow auto-walk: when the core says the intent is safe (NoThreat or
    // PreserveSafeIntent → overrideActive=false), walk the intent ourselves.
    // NoThreat returns before wall validation runs, so probe the move target
    // against walls here to avoid walking into them.
    bool lockWalk = false;
    if (!overrideActive && !yieldedToPlayer &&
        g_lockFollow.load(std::memory_order_relaxed) &&
        LenSq(in.intentDir) > 1e-6f &&
        (g_out.decision == Decision::NoThreat || g_out.decision == Decision::PreserveSafeIntent)) {
        const Vec2 probe = Add(in.player, Mul(g_out.velocity, std::max(frameMs, 100.f)));
        lockWalk = Sensors::CanOccupy(probe.x, probe.y, settings.safeWalk);
        if (lockWalk) appliedVel = g_out.velocity;
    }

    if (overrideActive || lockWalk) {
        // The candidate ray was validated, but the velocity we ended up with
        // may not be that ray — speed matching, the intent blends and the slew
        // limiter all bend it, and the tile map itself is a snapshot up to
        // ~100 ms old. Probe the endpoint we are about to commit to, and when
        // it is blocked, slide along the wall on whichever axis is still open
        // instead of walking into it. Sliding is also what a player does.
        const bool safeWalkProbe = settings.safeWalk && g_out.decision != Decision::HazardEscape;
        moveTarget = Add(in.player, Mul(appliedVel, frameMs));

        if (!Sensors::CanOccupy(moveTarget.x, moveTarget.y, safeWalkProbe)) {
            const Vec2 slideX{ in.player.x + appliedVel.x * frameMs, in.player.y };
            const Vec2 slideY{ in.player.x, in.player.y + appliedVel.y * frameMs };
            if (std::fabs(appliedVel.x) > 1e-6f &&
                Sensors::CanOccupy(slideX.x, slideX.y, safeWalkProbe)) {
                moveTarget = slideX;
            } else if (std::fabs(appliedVel.y) > 1e-6f &&
                       Sensors::CanOccupy(slideY.x, slideY.y, safeWalkProbe)) {
                moveTarget = slideY;
            } else {
                moveTarget = in.player;   // boxed in — hold rather than clip
            }
        }

        if (moveTarget.x != in.player.x || moveTarget.y != in.player.y) {
            if (!DodgeRuntime::CallMoveTo(player, moveTarget.x, moveTarget.y))
                moveFailed = true;
        }
    }

    if (settings.debugOverlay) {
        static DebugSnapshot d;   // large (holds the sensor snapshot) — keep off the stack
        d.active = true;
        d.decision = g_out.decision;
        d.player = in.player;
        d.intentDir = in.intentDir;
        d.moveTarget = moveTarget;
        d.overrideActive = overrideActive || lockWalk;
        d.moveFailed = moveFailed;
        d.candidate = g_out.candidate;
        d.speedScale = g_out.speedScale;
        d.threatCount = g_out.threatCount;
        d.earliestImpactMs = g_out.earliestImpactMs;
        d.speed = in.moveSpeed;
        d.leadMs = settings.leadMs;
        d.horizonMs = settings.horizonMs;
        {
            const ProjectileTracking::PredictionDiag pd = ProjectileTracking::GetPredictionDiag();
            d.predEnabled = pd.enabled;
            d.predClockErrMs = pd.emaAbsTauMs;
            d.predModelErrTiles = pd.emaCrossTiles;
        }
        for (int i = 0; i < kCandidateCount; ++i) d.candidates[i] = g_out.candidates[i];
        d.sensors = g_snapshot;
        PublishDebug(d);
    }
}

void RenderSettings()
{
    float horizon = GetHorizonMs();
    float lead    = GetLeadMs();
    float hit     = GetHitScale();
    bool  safe    = GetSafeWalk();
    bool  spd     = GetSpeedScale();
    bool  dbg     = GetDebugOverlay();

    if (ImGui::SliderFloat("Horizon ms##pjdodge", &horizon, 300.f, 1200.f)) SetHorizonMs(horizon);
    if (ImGui::SliderFloat("Command lead ms##pjdodge", &lead, 0.f, 150.f)) SetLeadMs(lead);

    float aoeH = GetAoeHorizonMs();
    if (ImGui::SliderFloat("AoE horizon ms##pjdodge", &aoeH, 500.f, 5000.f)) SetAoeHorizonMs(aoeH);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Telegraphed blasts run on 1.5-3 s fuses. The projectile horizon\n"
                          "is far too short for them - this window is theirs alone.");
    if (ImGui::SliderFloat("Hit scale##pjdodge", &hit, 0.5f, 1.5f)) SetHitScale(hit);
    if (ImGui::Checkbox("Safe walk (avoid damaging ground)##pjdodge", &safe)) SetSafeWalk(safe);
    if (ImGui::Checkbox("Match intent speed##pjdodge", &spd)) SetSpeedScale(spd);

    bool pred = GetPredictionAccuracy();
    if (ImGui::Checkbox("Prediction accuracy (clock calibration)##pjdodge", &pred)) SetPredictionAccuracy(pred);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fits a per-projectile time correction from the live position so\n"
                          "predictions sit on the true trajectory (removes clock jitter +\n"
                          "spawn-hook latency). Overlay shows clock error / model error.");
    {
        const ProjectileTracking::PredictionDiag pd = ProjectileTracking::GetPredictionDiag();
        ImGui::SameLine();
        ImGui::TextDisabled("clk %.1fms  model %.03f/%.03f", pd.emaAbsTauMs, pd.emaCrossTiles, pd.maxCrossTiles);
    }

    if (ImGui::Checkbox("Debug overlay##pjdodge", &dbg)) SetDebugOverlay(dbg);

    ImGui::Separator();
    ImGui::TextDisabled("PLAYER AUTHORITY");

    bool manual = GetManualPriority();
    if (ImGui::Checkbox("WASD takes priority##pjdodge", &manual)) SetManualPriority(manual);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("While you are steering, the controller stands down for\n"
                          "everything except an imminent hit. Emergency saves always fire.");

    float hold = GetManualHoldMs();
    if (ImGui::SliderFloat("Manual hold ms##pjdodge", &hold, 0.f, 1000.f)) SetManualHoldMs(hold);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How long after your last keypress the controller keeps\n"
                          "standing down. Stops it grabbing the wheel between keystrokes.");

    bool human = GetHumanize();
    if (ImGui::Checkbox("Humanize movement##pjdodge", &human)) SetHumanize(human);

    float react = GetReactionMs();
    if (ImGui::SliderFloat("Reaction ms##pjdodge", &react, 0.f, 200.f)) SetReactionMs(react);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Delay before a non-urgent dodge starts. 0 = instant (robotic).");

    float turn = GetTurnRateDeg();
    if (ImGui::SliderFloat("Turn rate deg/s##pjdodge", &turn, 180.f, 2400.f)) SetTurnRateDeg(turn);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Caps how fast the heading may rotate, so it curves instead of\n"
                          "snapping between compass directions. Emergencies ignore this.");

    ImGui::Separator();
    bool lockF = GetLockFollow();
    if (ImGui::Checkbox("Lock follow (walk toward lock target)##pjdodge", &lockF)) SetLockFollow(lockF);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When on, consumes the enemy lock / follow target as the\n"
                          "walk direction when no WASD input is active. WASD always\n"
                          "overrides. Off = pure dodge, no auto-walk.");
}

void RenderDebugOverlay(float camX, float camY, float angle, float zoom, float cx, float cy)
{
    if (!IsEnabled() || !GetDebugOverlay()) return;
    // large — keep off the render-thread stack; heap-backed for the same reason as DebugSlot()
    static DebugSnapshot* const snap = new DebugSnapshot();
    { std::lock_guard<std::mutex> lock(g_debugMutex); *snap = DebugSlot(); }
    Debug::Render(*snap, camX, camY, angle, zoom, cx, cy);
}

DiagView GetDiagView()
{
    DiagView v{};
    v.enabled = IsEnabled();
    std::lock_guard<std::mutex> lock(g_debugMutex);
    const DebugSnapshot& d = DebugSlot();
    v.decision = static_cast<int>(d.decision);
    v.playerX = d.player.x;
    v.playerY = d.player.y;
    v.overrideActive = d.overrideActive;
    const Vec2 dir = d.candidates[std::clamp(d.candidate, 0, kCandidateCount - 1)].dir;
    v.velXPerSec = d.overrideActive ? dir.x * d.speed * d.speedScale * 1000.f : 0.f;
    v.velYPerSec = d.overrideActive ? dir.y * d.speed * d.speedScale * 1000.f : 0.f;
    v.candidate = d.candidate;
    v.speedScale = d.speedScale;
    v.threatCount = d.threatCount;
    v.earliestImpactMs = d.earliestImpactMs >= kMaxTimeMs ? -1.f : d.earliestImpactMs;
    v.projectiles = d.sensors.projectileCount;
    v.aoes = d.sensors.aoeCount;
    v.enemies = d.sensors.enemyCount;
    const ProjectileTracking::PredictionDiag pd = ProjectileTracking::GetPredictionDiag();
    v.predEnabled       = pd.enabled;
    v.predCalibrated    = pd.calibrated;
    v.predClockErrMs    = pd.emaAbsTauMs;
    v.predModelErrTiles = pd.emaCrossTiles;
    v.predModelMaxTiles = pd.maxCrossTiles;
    return v;
}

void  SetHorizonMs(float ms) { g_horizonMs.store(Clamp(ms, 200.f, 2000.f), std::memory_order_relaxed); }
float GetHorizonMs() { return g_horizonMs.load(std::memory_order_relaxed); }
void  SetLeadMs(float ms) { g_leadMs.store(Clamp(ms, 0.f, 250.f), std::memory_order_relaxed); }
float GetLeadMs() { return g_leadMs.load(std::memory_order_relaxed); }
void  SetHitScale(float s) { g_hitScale.store(Clamp(s, 0.25f, 2.5f), std::memory_order_relaxed); }
float GetHitScale() { return g_hitScale.load(std::memory_order_relaxed); }
void  SetSafeWalk(bool en) { g_safeWalk.store(en, std::memory_order_relaxed); }
bool  GetSafeWalk() { return g_safeWalk.load(std::memory_order_relaxed); }
void  SetSpeedScale(bool en) { g_speedScale.store(en, std::memory_order_relaxed); }
bool  GetSpeedScale() { return g_speedScale.load(std::memory_order_relaxed); }
void  SetPredictionAccuracy(bool en) {
    g_predictionAccuracy.store(en, std::memory_order_relaxed);
    ProjectileTracking::SetPredictionAccuracy(en);
}
bool  GetPredictionAccuracy() { return g_predictionAccuracy.load(std::memory_order_relaxed); }
void  SetDebugOverlay(bool en) { g_debugOverlay.store(en, std::memory_order_relaxed); }
bool  GetDebugOverlay() { return g_debugOverlay.load(std::memory_order_relaxed); }
void  SetLockFollow(bool en) { g_lockFollow.store(en, std::memory_order_relaxed); }
bool  GetLockFollow() { return g_lockFollow.load(std::memory_order_relaxed); }
void  SetAoeHorizonMs(float ms) { g_aoeHorizonMs.store(Clamp(ms, 200.f, 5000.f), std::memory_order_relaxed); }
float GetAoeHorizonMs() { return g_aoeHorizonMs.load(std::memory_order_relaxed); }
void  SetManualPriority(bool en) { g_manualPriority.store(en, std::memory_order_relaxed); }
bool  GetManualPriority() { return g_manualPriority.load(std::memory_order_relaxed); }
void  SetManualHoldMs(float ms) { g_manualHoldMs.store(Clamp(ms, 0.f, 2000.f), std::memory_order_relaxed); }
float GetManualHoldMs() { return g_manualHoldMs.load(std::memory_order_relaxed); }
void  SetHumanize(bool en) { g_humanize.store(en, std::memory_order_relaxed); }
bool  GetHumanize() { return g_humanize.load(std::memory_order_relaxed); }
void  SetReactionMs(float ms) { g_reactionMs.store(Clamp(ms, 0.f, 250.f), std::memory_order_relaxed); }
float GetReactionMs() { return g_reactionMs.load(std::memory_order_relaxed); }
void  SetTurnRateDeg(float d) { g_turnRateDeg.store(Clamp(d, 90.f, 3600.f), std::memory_order_relaxed); }
float GetTurnRateDeg() { return g_turnRateDeg.load(std::memory_order_relaxed); }

} // namespace PJDodge
