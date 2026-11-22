// FILE: mods/ShipControl/sc_logic_steer.hpp
// WHAT: pure steering rules of the ShipControl move job: approach Throttle, phase 1 decision, low-power state, aim view offset, ore refresh, surface distance. No game calls.
// WHY: the rules are checked without the game (tests/test_shipcontrol.cpp). Values and formulas ported 1:1 from MiningBenchMod mb_steer.hpp (move_aim_step, view_aim_drive) and mb_logic.hpp.
//      The mining copies stay until SC2b slice C.
// DEPENDS ON (exact): <algorithm>, <cmath>. Pure std: no windows.h, no sc_math.hpp.
// USED BY: sc_frame.hpp, tests/test_shipcontrol.cpp
// GAME FACTS: none. Units: m, s, deg, Throttle steps (game steps -10..+10).
#pragma once

#include <algorithm>
#include <cmath>

namespace
{
    // WHAT: heading error below this (deg): phase 1 face done. Mining MV_FACE_DONE_DEG.
    constexpr double SM_FACE_DONE_DEG = 2.0;
    // WHAT: face fails after this many s in phase 1. Mining MV_FACE_TIMEOUT_S.
    constexpr double SM_FACE_TIMEOUT_S = 30.0;
    // WHAT: approach enters phase 2 below this heading error (deg). Mining MV_APPROACH_START_DEG.
    constexpr double SM_APPROACH_START_DEG = 10.0;
    // WHAT: heading error above this (deg) forces the approach Throttle target to 0. Mining MV_APPROACH_HEAD_DEG.
    constexpr double SM_APPROACH_HEAD_DEG = 10.0;
    // WHAT: approach fails after this many s in total. Mining MV_APPROACH_TIMEOUT_S.
    constexpr double SM_APPROACH_TIMEOUT_S = 300.0;
    // WHAT: top approach Throttle step (steps). Mining MV_APPROACH_MAX_THR.
    constexpr int SM_APPROACH_MAX_THR = 10;
    // WHAT: applied step rises by 1 at most once per this many s; it falls straight to the target. Mining MV_THR_RAMP_S.
    constexpr double SM_THR_RAMP_S = 0.5;
    // WHAT: overshoot brake step (steps). Mining MV_BRAKE_THR.
    constexpr int SM_BRAKE_THR = -2;
    // WHAT: brake until speed is below this (m/s). Mining MV_BRAKE_DONE_MS.
    constexpr double SM_BRAKE_DONE_MS = 1.0;
    // WHAT: rate sample period (s). Mining MV_SAMPLE_S.
    constexpr double SM_SAMPLE_S = 0.25;
    // WHAT: aim offset magnitude cap (deg). Mining MV_VIEW_MAX_DEG.
    constexpr double SM_VIEW_MAX_DEG = 20.0;
    // WHAT: dead zone added to the heading error for the aim offset (deg). Mining MV_VIEW_DEADZONE_DEG.
    constexpr double SM_VIEW_DEADZONE_DEG = 7.0;
    // WHAT: resend period of the aim offset (s). Mining MV_VIEW_PERIOD_S.
    constexpr double SM_VIEW_PERIOD_S = 0.1;
    // WHAT: raw ViewRotation units per degree. Mining VIEW_RAW_PER_DEG (mb_steer.hpp).
    constexpr double SM_VIEW_RAW_PER_DEG = 65536.0;
    // WHAT: ore actor is re-read within this distance (m). Mining ORE_REFRESH_M (aim_calc_ore).
    constexpr double SM_ORE_REFRESH_M = 100.0;
    // WHAT: uu (Unreal units) per m. Mining MV_UU_PER_M.
    constexpr double SM_UU_PER_M = 100.0;
    // WHAT: raw fixed-point units per m (65536). Mining RAW_PER_METER / g_raw_per_m default.
    constexpr double SM_RAW_PER_M = 65536.0;
    // WHAT: chunk size in m, and the same in raw units. Mining AF_CHUNK_M (2048 m) and the 134217728 raw of pt_rloc.
    constexpr double SM_CHUNK_M = 2048.0;
    constexpr double SM_CHUNK_RAW = 134217728.0;

    // Moved verbatim from mb_logic.hpp (approach Throttle, approach timeout, low-power cap, no-power test).
    constexpr double APPROACH_RAMP_M = 500.0;        // approach: full Throttle beyond this distance to the stand-off point (m), linear ramp inside

    // WHAT: approach Throttle step for the distance to the stand-off point (m). max_thr beyond APPROACH_RAMP_M; inside it linear down to 1
    //       (ceil, so the last metres still move); 0 at or past the stand-off point. speed_mps is not used by the ramp (the caller brakes on it).
    // WHY: full speed on the long leg, a smooth slow-down near the rock. Ramp is distance-only: no measured deceleration exists.
    inline int approach_throttle(double dist_to_standoff_m, double speed_mps, int max_thr)
    {
        (void)speed_mps;
        if (max_thr < 1 || dist_to_standoff_m <= 0.0) return 0;
        if (dist_to_standoff_m >= APPROACH_RAMP_M) return max_thr;
        return std::clamp(static_cast<int>(std::ceil(max_thr * dist_to_standoff_m / APPROACH_RAMP_M)), 1, max_thr);
    }

    // WHAT: approach timeout (s): base, or base / max_frac in low power (max_frac = FlightModel MaxLowPowerSpeed, a fraction 0..1). Other max_frac -> base.
    inline double approach_timeout_s(double base, bool lp, double max_frac)
    {
        if (!lp || max_frac <= 0.0 || max_frac >= 1.0) return base;
        return base / max_frac;
    }

    // WHAT: Throttle step cap: min(cap, lp_thr) in low power when lp_thr > 0 (FlightModel LowPowerThrottle), else cap.
    inline int lowpower_throttle_cap(int cap, bool lp, int lp_thr) { return (lp && lp_thr > 0) ? (std::min)(cap, lp_thr) : cap; }

    // WHAT: no power: the grid consumes (cons > 0), produces nothing (prod <= 0) and efficiency is 0. This is the lost state (grace LOWPOWER_GRACE_S).
    inline bool power_no_power(double cons, double prod, double eff) { return cons > 0.0 && prod <= 0.0 && eff <= 0.0; }

    // WHAT: no-power test from one flight snapshot. known = grid read or low-power flag readable. dead = grid read -> power_no_power, else (flag known and on).
    inline bool power_dead_from(bool grid_ok, double prod, double cons, double eff, bool lp_known, bool lp_on, bool& known)
    {
        known = grid_ok || lp_known;
        return grid_ok ? power_no_power(cons, prod, eff) : (lp_known && lp_on);
    }

    // WHAT: low-power state of the approach. dead = no power: grid read -> power_no_power, else the low-power flag when known and on.
    //       lp = dead and the low-power limits read (lim_ok). thr / frac are set only when lp, else 0. Returns lp.
    // WHY: same rule as mining power_dead + lp_state (mb_steer.hpp), pure so it can be checked.
    inline bool lp_state_from(bool grid_ok, double prod, double cons, double eff, bool lp_known, bool lp_on, bool lim_ok, int lp_thr, double lp_frac,
                              bool& lp, int& thr, double& frac)
    {
        const bool dead = grid_ok ? power_no_power(cons, prod, eff) : (lp_known && lp_on);
        lp = dead && lim_ok;
        thr = lp ? lp_thr : 0;
        frac = lp ? lp_frac : 0.0;
        return lp;
    }

    // WHAT: phase 1 outcome. NONE = keep aiming. FACE_OK = face done. FACE_TIMEOUT = face failed. TO_PHASE2 = approach starts.
    enum Phase1 { P1_NONE, P1_FACE_OK, P1_FACE_TIMEOUT, P1_TO_PHASE2 };

    // WHAT: phase 1 decision (move_aim_step step 3). Face (approach false): err < SM_FACE_DONE_DEG ok; pt >= SM_FACE_TIMEOUT_S timeout.
    //       Approach: err < SM_APPROACH_START_DEG -> phase 2.
    inline Phase1 phase1_decide(bool approach, double err, double pt)
    {
        if (!approach)
        {
            if (err < SM_FACE_DONE_DEG) return P1_FACE_OK;
            if (pt >= SM_FACE_TIMEOUT_S) return P1_FACE_TIMEOUT;
            return P1_NONE;
        }
        return err < SM_APPROACH_START_DEG ? P1_TO_PHASE2 : P1_NONE;
    }

    // WHAT: result of approach_step. thr / thr_t = applied step and ramp timer. target = step wanted. done = stopped past the stop distance (caller ends the job, Throttle 0).
    struct ApproachOut { int thr; double thr_t; int target; bool done; };

    // WHAT: one approach Throttle step (move_aim_step steps 8-9, mb_steer.hpp). remaining = surface - stop distance (m); v_ms = speed (m/s);
    //       err = heading error (deg); steps = ship Throttle steps; lp / lp_thr = low power and its Throttle cap.
    //       remaining <= 0: brake (SM_BRAKE_THR) while v_ms >= SM_BRAKE_DONE_MS, else done. Otherwise cap = lowpower_throttle_cap(min(SM_APPROACH_MAX_THR, steps)),
    //       target = 0 when err > SM_APPROACH_HEAD_DEG, else approach_throttle(remaining). The step rises by 1 per SM_THR_RAMP_S and falls straight to target.
    inline ApproachOut approach_step(double remaining, double v_ms, double err, int steps, bool lp, int lp_thr, int thr, double thr_t, double dt)
    {
        ApproachOut o{thr, thr_t, 0, false};
        if (remaining <= 0.0)
        {
            if (v_ms < SM_BRAKE_DONE_MS) { o.done = true; o.thr = 0; o.thr_t = 0.0; return o; }
            o.target = SM_BRAKE_THR;
            o.thr = SM_BRAKE_THR;
            o.thr_t = 0.0;
            return o;
        }
        if (o.thr < 0) o.thr = 0;   // pushed back out after a brake: restart forward from 0
        const int cap = lowpower_throttle_cap((std::min)(SM_APPROACH_MAX_THR, steps), lp, lp_thr);
        o.target = err > SM_APPROACH_HEAD_DEG ? 0 : approach_throttle(remaining, v_ms, cap);
        if (o.target > o.thr)
        {
            o.thr_t += dt;
            if (o.thr_t >= SM_THR_RAMP_S) { o.thr_t = 0.0; ++o.thr; }
        }
        else { o.thr = o.target; o.thr_t = 0.0; }
        return o;
    }

    // ---- QUICK drive profile (escorts / defenders / attackers): no ramp, throttle jumps between steps, hard brake on the goal. ----
    // WHAT: assumed seconds the ship needs to stop from vmax at 100% brake. CALIBRATE in game (unverified assumption).
    constexpr double SQ_TAU_S = 2.0;
    // WHAT: heading error above this (deg) holds the throttle at 0 in the quick profile.
    constexpr double SQ_HEAD_DEG = 30.0;
    // WHAT: quick profile done below this speed (m/s) once past the stop point.
    constexpr double SQ_DONE_MS = 1.0;

    // WHAT: result of approach_step_quick. thr = step to write now (-10..cap). target = step wanted. done = stopped past the point (caller ends the job).
    struct QuickOut { int thr; bool done; int target; };

    // WHAT: one QUICK approach step. remaining_m = surface - stop distance (m); v_ms = closing speed (m/s); err_deg = heading error; steps_cap = ship steps;
    //       lp / lp_thr = low power and its cap; thr_prev = last written step (hysteresis); vmax_ms = max speed (m/s); brake_pct = braking strength (% of SQ_TAU_S model, <=0 = 100);
    //       overshoot_m = metres past the goal that are tolerated before the hard brake.
    //       rem = remaining - overshoot. rem <= 0: v < SQ_DONE_MS done, else -10 (v > 25% vmax), -4 (> 5%) or -2 (slow: no backing away).
    //       Else want_v = rem / (SQ_TAU_S * brake_pct/100); want_step = ceil(want_v/vmax*10) capped. err > SQ_HEAD_DEG: 0.
    //       Braking already (thr_prev < 0): -10 while v > want_v, then release to want_step. v > want_v + band: -10. Else want_step directly (no ramp).
    inline QuickOut approach_step_quick(double remaining_m, double v_ms, double err_deg, int steps_cap, bool lp, int lp_thr, int thr_prev, double vmax_ms, int brake_pct, int overshoot_m)
    {
        const int cap = lowpower_throttle_cap((std::min)(SM_APPROACH_MAX_THR, steps_cap), lp, lp_thr);
        QuickOut o{0, false, 0};
        const double rem = remaining_m - overshoot_m;
        if (rem <= 0.0)
        {
            if (v_ms < SQ_DONE_MS) { o.done = true; return o; }
            o.thr = v_ms > 0.25 * vmax_ms ? -10 : (v_ms > 0.05 * vmax_ms ? -4 : -2);
            o.target = o.thr;
            return o;
        }
        const double bp = brake_pct > 0 ? brake_pct : 100;
        const double want_v = rem / (SQ_TAU_S * bp / 100.0);
        const int want_step = want_v >= vmax_ms ? cap : std::clamp(static_cast<int>(std::ceil(want_v / vmax_ms * 10.0)), 0, cap);
        const double band = (std::max)(10.0, 0.1 * vmax_ms);
        if (err_deg > SQ_HEAD_DEG) { o.thr = 0; o.target = 0; return o; }
        o.target = want_step;
        if (thr_prev < 0) o.thr = v_ms > want_v ? -10 : want_step;
        else o.thr = v_ms > want_v + band ? -10 : want_step;
        o.thr = std::clamp(o.thr, -10, (std::max)(cap, 0));
        return o;
    }

    // ---- QUICK + follow (API 6/7): match the captain's speed in the slot with normal throttle only. ----
    // WHAT: position gain (1/s, ~ 1/SQ_TAU_S), speed-mismatch gain, in-slot band (m).
    constexpr double SQ_KP = 0.5;
    constexpr double SQ_KD = 0.1;
    constexpr double SQ_BAND_M = 5.0;

    // WHAT: result of quick_match_throttle. done is always false (follow never finishes by itself; the caller cancels / restarts the job).
    struct MatchOut { int thr; bool done; };

    // WHAT: Throttle step that holds the captain's speed in the slot. slot_err_m = distance still to go along the line (>0 = goal ahead);
    //       v_cap_ms = captain speed along that line; v_own_ms = own forward speed (>= 0); vmax_est_ms = own max speed at step 10;
    //       steps_cap = highest step allowed (already low-power capped); thr_prev = last written step (brake hysteresis);
    //       kp (1/s) position gain, kd speed-mismatch gain, band_m = |err| inside which the P term stays small, brake_pct scales kp (<=0 = 100),
    //       overshoot_m = metres past the slot before a position reverse is allowed.
    //       v_des = clamp(v_cap + kp*err, -0.3 vmax, vmax); v_cmd = v_des - kd*(v_own - v_cap); thr = round(10 v_cmd / vmax) in [0, cap].
    //       Negative thr only to kill speed: v_own > v_des + band (hard brake, -10 above 25% vmax, -4 above 5%, else -2; held while v_own > v_des),
    //       or v_cmd < 0 with err < -overshoot_m (limited to the same tiers). Inside band_m the captain speed is held: thr = feed-forward round(10 v_cap / vmax) + small P.
    inline MatchOut quick_match_throttle(double slot_err_m, double v_cap_ms, double v_own_ms, double vmax_est_ms, int steps_cap, int thr_prev, double kp, double kd, double band_m, int brake_pct, int overshoot_m)
    {
        (void)band_m;   // the band is implicit: v_des = v_cap + kp*err is already the captain speed plus a small P term when err is small
        const int cap = std::clamp(steps_cap, 0, SM_APPROACH_MAX_THR);
        const double vmax = (std::max)(vmax_est_ms, 1.0);
        const double kpe = kp * (brake_pct > 0 ? brake_pct : 100) / 100.0;
        const double v_des = std::clamp(v_cap_ms + kpe * slot_err_m, -0.3 * vmax, vmax);
        const double v_cmd = v_des - kd * (v_own_ms - v_cap_ms);
        const int thr_raw = static_cast<int>(std::lround(10.0 * v_cmd / vmax));
        const double vband = (std::max)(10.0, 0.1 * vmax);
        const int hard = v_own_ms > 0.25 * vmax ? -10 : (v_own_ms > 0.05 * vmax ? -4 : -2);
        int thr;
        if (thr_prev < 0) thr = v_own_ms > v_des ? hard : std::clamp(thr_raw, 0, cap);
        else if (v_own_ms > v_des + vband) thr = hard;
        else if (thr_raw < 0) thr = slot_err_m < -static_cast<double>(overshoot_m) ? (std::max)(thr_raw, hard) : 0;
        else thr = (std::min)(thr_raw, cap);
        return MatchOut{std::clamp(thr, -10, cap), false};
    }

    // ---- follow, smooth near the slot (API 7) ----
    // WHAT: zones by |slot err| (m): above SQ_FAR_M the plain quick_match_throttle (full throttle / hard brake allowed); below it an EMA-smoothed proportional throttle;
    //       below SQ_NEAR_M also a throttle rate limit and the sign hysteresis; below SQ_DEAD_M pure feed-forward.
    constexpr double SQ_FAR_M = 80.0;
    constexpr double SQ_NEAR_M = 40.0;
    constexpr double SQ_DEAD_M = 3.0;
    constexpr double SQ_EMA = 0.3;          // v_cmd smoothing
    constexpr double SQ_RATE_S = 0.25;      // near zone: one step of thr per this many s
    constexpr double SQ_RATE_DEAD_S = 0.5;  // dead zone: one step per this many s
    constexpr double SQ_SIGN_S = 0.5;       // a thr sign change must persist this long (near zone)
    // WHAT: state of quick_match_step (one per job).
    struct MatchState { double ema = 0.0; bool have = false; double since_chg = 0.0; double opp_t = 0.0; };

    // WHAT: smooth follow throttle. Args as quick_match_throttle plus st / dt (s since the last call) and no kp/kd/band (SQ_KP, SQ_KD constants).
    //       |err| > SQ_FAR_M: quick_match_throttle. Else v_cmd = clamp(v_cap + kp*err - kd*(v_own - v_cap)) smoothed by EMA SQ_EMA, thr = round(10 v_cmd/vmax);
    //       |err| < SQ_DEAD_M: thr = feed-forward round(10 v_cap/vmax). thr < 0 only for err < -2 SQ_DEAD_M: limited to -10/-4/-2 by speed when v_own > v_cap + band, else at most -2 (back to the slot),
    //       or (err >= SQ_NEAR_M) v_own > v_des + band: hard brake. Near zone and |v_own - v_cap| < band: thr moves 1 step per SQ_RATE_S (SQ_RATE_DEAD_S in the dead zone),
    //       and a sign change (pos <-> neg) waits SQ_SIGN_S. A large speed mismatch changes thr freely (a fast approach must brake at once).
    inline int quick_match_step(MatchState& st, double dt, double err, double v_cap, double v_own, double vmax_est, int steps_cap, int thr_prev, int brake_pct, int overshoot_m)
    {
        const double ae = std::fabs(err);
        if (ae > SQ_FAR_M)
        {
            st.have = false; st.since_chg = SQ_RATE_S; st.opp_t = 0.0;
            return quick_match_throttle(err, v_cap, v_own, vmax_est, steps_cap, thr_prev, SQ_KP, SQ_KD, SQ_BAND_M, brake_pct, overshoot_m).thr;
        }
        const int cap = std::clamp(steps_cap, 0, SM_APPROACH_MAX_THR);
        const double vmax = (std::max)(vmax_est, 1.0);
        const double kpe = SQ_KP * (brake_pct > 0 ? brake_pct : 100) / 100.0;
        const double rel = v_own - v_cap;
        const double v_des = std::clamp(v_cap + kpe * err, -0.3 * vmax, vmax);
        const double raw = std::clamp(v_des - SQ_KD * rel, -0.3 * vmax, vmax);
        st.ema = st.have ? SQ_EMA * raw + (1.0 - SQ_EMA) * st.ema : raw;
        st.have = true;
        const double vband = (std::max)(10.0, 0.1 * vmax);
        const int hard = v_own > 0.25 * vmax ? -10 : (v_own > 0.05 * vmax ? -4 : -2);
        const bool nearz = ae < SQ_NEAR_M, dead = ae < SQ_DEAD_M;
        int want = dead ? std::clamp(static_cast<int>(std::lround(10.0 * v_cap / vmax)), 0, cap) : static_cast<int>(std::lround(10.0 * st.ema / vmax));
        if (!nearz && err >= 0.0 && v_own > v_des + vband) want = hard;        // approaching far too fast: brake now
        else if (want < 0) want = err < -2.0 * SQ_DEAD_M ? (std::max)(want, v_own > v_cap + vband ? hard : -2) : 0;   // reverse: hard only to kill speed, else a gentle -2 back to the slot
        want = std::clamp(want, -10, cap);
        if (!nearz) { st.since_chg = SQ_RATE_S; st.opp_t = 0.0; return want; }
        if (static_cast<long long>(thr_prev) * want < 0)
        {
            st.opp_t += dt;
            if (st.opp_t < SQ_SIGN_S) want = 0;
            else st.opp_t = 0.0;
        }
        else st.opp_t = 0.0;
        if (std::fabs(rel) >= vband) { st.since_chg = SQ_RATE_S; return want; }
        const double period = dead ? SQ_RATE_DEAD_S : SQ_RATE_S;
        st.since_chg += dt;
        if (want == thr_prev) return want;
        if (st.since_chg < period) return thr_prev;
        st.since_chg = 0.0;
        return thr_prev + (want > thr_prev ? 1 : -1);
    }

    // WHAT: learns the max speed at step 10 (m/s). Steady throttle (>= 1.5 s within 1 step), thr >= 2, v_own >= 1: ratio = v_own / (thr/10),
    //       EMA 0.2 into est, clamped to 0.5x..2x of init. Otherwise est unchanged.
    inline double vmax_learn(double est, double init, int thr, double v_own_ms, double steady_s)
    {
        if (steady_s < 1.5 || thr < 2 || v_own_ms < 1.0 || init <= 0.0) return est;
        return std::clamp(0.8 * est + 0.2 * (v_own_ms / (thr / 10.0)), 0.5 * init, 2.0 * init);
    }

    // WHAT: the ore actor is re-read when within SM_ORE_REFRESH_M (m) and not read yet, or the last read is 1 s old.
    inline bool ore_refresh_due(double dist_m, bool fresh, double ore_t) { return dist_m <= SM_ORE_REFRESH_M && (!fresh || ore_t >= 1.0); }

    // WHAT: aim offset of one view send (view_aim_drive steps 2-4, mb_steer.hpp). err = heading error (deg); a = unit rotation axis in body axes.
    //       err < SM_FACE_DONE_DEG: yaw = pit = 0, aligned = true. Else mag = min(err + SM_VIEW_DEADZONE_DEG, SM_VIEW_MAX_DEG),
    //       yaw = mag * (a . Z) / |a|, pit = mag * (a . -Y) / |a| (yaw axis = body +Z, pitch axis = body -Y). |a| <= 1e-9 gives 0.
    //       max_deg = the offset cap (default SM_VIEW_MAX_DEG; quick profile passes more). Always returns true: yaw / pit are written.
    inline bool view_offset(double err, const double a[3], double& yaw_deg, double& pit_deg, bool& aligned, double max_deg = SM_VIEW_MAX_DEG)
    {
        aligned = err < SM_FACE_DONE_DEG;
        yaw_deg = 0.0;
        pit_deg = 0.0;
        if (aligned) return true;
        const double n = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (n <= 1e-9) return true;
        const double mag = (std::min)(err + SM_VIEW_DEADZONE_DEG, max_deg);
        yaw_deg = mag * a[2] / n;
        pit_deg = mag * (-a[1]) / n;
        return true;
    }

    // WHAT: follow steering offset (deg). err <= SQ_HEAD_DEG: |err| < SQ_VIEW_DEAD_DEG -> 0 (aligned); else mag = min(SQ_VIEW_GAIN * err, max_deg); a change of a
    //       non-zero component is limited to SQ_VIEW_SLEW deg/s (dt = s since the last send; prev_* = last sent). err > SQ_HEAD_DEG: the hard view_offset, no slew.
    constexpr double SQ_VIEW_DEAD_DEG = 2.5;
    constexpr double SQ_VIEW_GAIN = 0.5;
    constexpr double SQ_VIEW_SLEW = 60.0;
    inline void view_offset_follow(double err, const double a[3], double prev_yaw, double prev_pit, double dt, double max_deg, double& yaw, double& pit, bool& aligned)
    {
        if (err > SQ_HEAD_DEG) { view_offset(err, a, yaw, pit, aligned, max_deg); return; }
        aligned = err < SQ_VIEW_DEAD_DEG;
        yaw = 0.0;
        pit = 0.0;
        if (aligned) return;
        const double n = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (n <= 1e-9) return;
        const double mag = (std::min)(SQ_VIEW_GAIN * err, max_deg);
        const double lim = SQ_VIEW_SLEW * dt;
        yaw = std::clamp(mag * a[2] / n, prev_yaw - lim, prev_yaw + lim);
        pit = std::clamp(mag * (-a[1]) / n, prev_pit - lim, prev_pit + lim);
    }

    // WHAT: surface distance (m) to a target: |tgt - ship| - rad_m. Points in m. rad_m = 0 for points and ore.
    inline double remain_dist_m(const double ship_m[3], const double tgt_m[3], double rad_m)
    {
        const double dx = tgt_m[0] - ship_m[0], dy = tgt_m[1] - ship_m[1], dz = tgt_m[2] - ship_m[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz) - rad_m;
    }
}
