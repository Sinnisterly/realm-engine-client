#pragma once

#include <cstdint>

// PJDodge — predictive auto-dodge (DodgeMode 6).
//
// Layer 1: 34 straight candidates × exact Chebyshev segment CCD against every
// threat path, AoE landing, wall and hazard tile; survival-lexicographic
// selection; intent-preservation ladder; hysteresis; command-latency lead.
// Layer 2: piecewise-heading escape search (receding horizon) when no straight
// candidate survives the whole window.
namespace PJDodge {

// Snapshot for the diag/MCP bridge.
struct DiagView {
    bool  enabled = false;
    int   decision = 0;
    float playerX = 0.f, playerY = 0.f;
    bool  overrideActive = false;
    float velXPerSec = 0.f, velYPerSec = 0.f;
    int   candidate = 0;
    float speedScale = 1.f;
    int   threatCount = 0;
    float earliestImpactMs = 0.f;
    int   projectiles = 0;
    int   aoes = 0;
    int   enemies = 0;
    // Prediction-accuracy residual stats.
    bool  predEnabled = false;
    int   predCalibrated = 0;
    float predClockErrMs = 0.f;    // typical clock error being corrected (ms)
    float predModelErrTiles = 0.f; // typical unexplained model error (tiles)
    float predModelMaxTiles = 0.f; // worst-case unexplained model error (tiles)
};

void SetEnabled(bool enabled);
bool IsEnabled();
void OnEnter();

// Game-update-thread tick (called from the AppEngineManager::Update detour).
void Tick(void* player, float px, float py, float dt);

// ImGui settings block (render thread, inside the Test tab).
void RenderSettings();

// World overlay (render thread).
void RenderDebugOverlay(float camX, float camY, float angle, float zoom, float cx, float cy);

DiagView GetDiagView();

// Knobs (atomic; IPC + GUI).
void  SetHorizonMs(float ms);   float GetHorizonMs();
void  SetLeadMs(float ms);      float GetLeadMs();
// Telegraphed blasts get their own, much longer window than projectiles.
void  SetAoeHorizonMs(float ms); float GetAoeHorizonMs();
void  SetHitScale(float s);     float GetHitScale();
void  SetSafeWalk(bool en);     bool  GetSafeWalk();
void  SetSpeedScale(bool en);   bool  GetSpeedScale();
void  SetPredictionAccuracy(bool en); bool GetPredictionAccuracy();
void  SetDebugOverlay(bool en); bool  GetDebugOverlay();
// When on, PJDodge consumes DangerPlanner's external goal (enemy lock / follow)
// as the intent direction when no WASD input is active. The character walks
// toward the lock target while dodging; WASD always overrides.
void  SetLockFollow(bool en);   bool  GetLockFollow();

// Player authority: while WASD is held (and for ManualHoldMs after the last
// press) the controller stands down for anything short of an imminent hit.
void  SetManualPriority(bool en); bool  GetManualPriority();
void  SetManualHoldMs(float ms);  float GetManualHoldMs();
// Humanization: a reaction delay before non-urgent dodges start, and a cap on
// how fast the heading may rotate. Emergency saves bypass both.
void  SetHumanize(bool en);       bool  GetHumanize();
void  SetReactionMs(float ms);    float GetReactionMs();
void  SetTurnRateDeg(float deg);  float GetTurnRateDeg();

} // namespace PJDodge
