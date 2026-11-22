// FILE: mods/ShipControl/sc_move.hpp
// WHAT: move jobs of ShipControl (SC2b A2): the job state, one step per engine tick (step_job), the end of a job (finish_job), the tick over all jobs
//       (sc_move_tick) and the SEH wrapper (sc_seh_run). The job is driven from one MSCore_ship_flight snapshot per tick and writes through MSCore only.
// WHY: ported 1:1 from MiningBenchMod mb_steer.hpp (move_job_fn, move_aim_step, view_aim_drive, warp_wait_step, move_start). Every tlog text is byte-identical
//      to the legacy line. The mining copies stay until SC2b slice C (legacy is never deleted in SC2b).
// DEPENDS ON (exact): sc_api.h (ScHost, ScMoveReq, ScMoveStatus), sc_mscore.hpp (MsFlight, FL_*, FF_*, ms_*), sc_speed.hpp (g_spd, SpeedMon),
//                     sc_frame.hpp (frame_from_flight, aim_pt, pt_rloc_calc, sc_calib_learn, g_calib), sc_logic_steer.hpp (SM_*, phase1_decide,
//                     approach_step, view_offset, ore_refresh_due, lp_state_from, approach_timeout_s, remain_dist_m), sc_logic_path.hpp (warp_settle_done),
//                     <cmath>, <cstdint>, <cstring>, <cwchar>, <map>, <string>, <vector>. No UE4SS headers (tests include this file).
// USED BY: dllmain.cpp (SC_move_* exports, SC_tick_frame, SC_release, SC_release_all), tests/test_shipcontrol.cpp (sc_view_raw).
// GAME FACTS: none here. Units: uu (100 per m), raw (65536 per m), chunk 2048 m, Throttle steps.
// NOTE: a job holds no game object. Every tick re-reads the ship from MSCore by id, so a freed ship is never used.
#pragma once

#include "sc_api.h"
#include "sc_mscore.hpp"
#include "sc_speed.hpp"
#include "sc_frame.hpp"
#include "sc_logic_steer.hpp"
#include "sc_logic_path.hpp"
#include "sc_bubble.hpp"   // SC3: ladder state (bub_braking)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

namespace
{
    // WHAT: one move job (legacy MoveJob, the ShipControl part). kind 0 = face_rock, 1 = approach_rock. mode 0 = fixed point pt_uu (+ handle),
    //       1 = chunk point (sys, ch, loc raw), 2 = floating ore (ore_loc, refreshed through the host).
    //       phase 1 = aim, 2 = approach. t = total s, pt = s in the phase, samp / rate = turn-rate sample, log_acc = s since the last trace line.
    //       err = last heading error (deg, -1 = none yet). thr = applied Throttle step, thr_t = ramp timer. warp_wait / warp_t = far-trip warp settle.
    //       owner = the owner that started the job (SC_move_cancel / SC_release check it).
    struct ScJob
    {
        int kind = 0;
        int sid = 0;
        int seq = -1;
        int mode = 0;
        int steps = 0;
        int cid = 0;
        int tag = 0;
        int sys = 0;
        int64_t ch[3] = {0, 0, 0};
        double loc[3] = {0, 0, 0};
        double pt_uu[3] = {0, 0, 0};
        void* handle = nullptr;
        double radius_m = 0.0;
        double stop_m = 0.0;
        int phase = 1;
        double t = 0.0, pt = 0.0, samp = 0.0, log_acc = 0.0, view_t = 0.0, ore_t = 0.0, thr_t = 0.0;
        double qs[4] = {1, 0, 0, 0};
        double rate = 0.0;
        double err = -1.0;
        bool aligned = false, ore_fresh = false;
        int thr = 0;
        bool warp_wait = false;
        double warp_t = 0.0;
        bool thr_warned = false;
        double ore_loc[3] = {0, 0, 0};
        std::string owner;
        // level 6 quick profile: profile 1 = quick. v_close = closing speed EMA (m/s) from the distance change, prev_dist_m = last aim dist (-1 = none), vmax_ms = last known max speed, q_log = s since the last quick log line.
        int profile = 0, brake_pct = 0, turn_pct = 0, overshoot_m = 0;
        double prev_dist_m = -1.0, v_close = 0.0, vmax_ms = 0.0, q_log = 0.0;
        // level 7 follow: ff_cms = captain speed along the line (cm/s), follow / ff_valid flags, vmax_init / vmax_est = max speed at step 10 (m/s, learned),
        // thr_last / steady_t = last step and seconds it stayed within 1 step (learning gate).
        int ff_cms = 0, follow = 0, ff_valid = 0, thr_last = 0;
        double vmax_init = 0.0, vmax_est = 0.0, steady_t = 0.0;
    };

    // WHAT: a job that ended in this tick: its ship, command seq and result text. Delivered to on_move_done after the registry lock is released.
    struct Done { int sid; int seq; std::wstring res; };

    // WHAT: the live move jobs, one per ship id.
    std::map<int, ScJob> g_jobs_mv;
    // WHAT: aim trace counter, shared by all ships (legacy view_aim_drive static trace_n).
    int g_trace_n = 0;
    // WHAT: learned max speed at step 10 per ship id (survives job restarts). ponytail: never pruned (a few bytes per ship id seen); prune by live ids if it ever grows.
    std::map<int, double> g_vmax_est;

    // WHAT: aim offset raw value of deg (ViewRotation raw per degree = SM_VIEW_RAW_PER_DEG, rounded). Pure.
    // WHY: 1:1 with view_aim_drive (llround of deg * 65536).
    inline long long sc_view_raw(double deg) { return static_cast<long long>(std::llround(deg * SM_VIEW_RAW_PER_DEG)); }

    // WHAT: job kind as the legacy kind text (fault line).
    const wchar_t* job_kind_text(int kind) { return kind == 1 ? L"approach_rock" : L"face_rock"; }

    // WHAT: host tlog line (mining's tlog). Null or throwing callback = nothing.
    void host_tlog(const ScHost& h, const std::wstring& s)
    {
        if (!h.tlog) return;
        try { h.tlog(s.c_str()); } catch (...) {}
    }

    // WHAT: host log line (SC log). Null or throwing callback = nothing.
    void host_logw(const ScHost& h, const std::wstring& s)
    {
        if (!h.log) return;
        try { h.log(s.c_str()); } catch (...) {}
    }

    // WHAT: host liveness of a rock actor (target_ok). A null callback = not alive (fail closed).
    bool host_target_ok(const ScHost& h, int sid, void* handle)
    {
        if (!h.target_ok) return false;
        try { return h.target_ok(sid, handle) == 1; }
        catch (...) { return false; }
    }

    // WHAT: host ore actor location (uu). false = not read (null callback counts as not read).
    bool host_ore_loc(const ScHost& h, int sid, void* handle, double out[3])
    {
        if (!h.ore_loc) return false;
        try { return h.ore_loc(sid, handle, out) == 1; }
        catch (...) { return false; }
    }

    // WHAT: host bubble brake (1 = holds Throttle 0). Null or throwing = no brake.
    bool host_braking(const ScHost& h, int sid)
    {
        if (!h.braking) return false;
        try { return h.braking(sid) == 1; }
        catch (...) { return false; }
    }

    // WHAT: writes the Throttle step of job j. A failed write is logged once per job (fault line).
    void write_thr(ScJob& j, int step, const ScHost& h)
    {
        std::wstring e;
        if (ms_set_throttle(j.sid, step, e)) return;
        if (j.thr_warned) return;
        j.thr_warned = true;
        host_logw(h, L"MOVE throttle write failed ship=" + std::to_wstring(j.sid) + L": " + e);
    }

    // WHAT: view_zero: send the aim offset (0,0) to console cid. cid <= 0 = nothing was sent. A failure is only traced (legacy view_zero).
    void view_zero_sc(int cid, const ScHost& h)
    {
        if (cid <= 0) return;
        std::wstring e;
        if (!ms_view_send(cid, 0, 0, e)) host_tlog(h, L"view_zero failed: " + e);
    }

    // WHAT: ends the job of ship sid: optionally stop the aim offset and the ship (writes), erase the job, queue on_move_done. Callers return at once.
    // WHY: one exit for every job end. writes = false for the killed / MSCore-unusable paths (no game writes).
    void finish_job(int sid, const std::wstring& res, const ScHost& h, std::vector<Done>& done, bool writes)
    {
        auto it = g_jobs_mv.find(sid);
        if (it == g_jobs_mv.end()) return;
        const ScJob j = it->second;
        g_jobs_mv.erase(it);
        if (writes)
        {
            view_zero_sc(j.cid, h);
            std::wstring e;
            ms_zero(sid, e);
        }
        done.push_back({sid, j.seq, res});
    }

    // WHAT: ids of the move jobs of owner `only` (API 5: a tick steps only its owner's jobs).
    std::vector<int> move_ids(const std::string& only)
    {
        std::vector<int> ids;
        for (const auto& kv : g_jobs_mv) if (kv.second.owner == only) ids.push_back(kv.first);
        return ids;
    }

    // WHAT: ends every job (killed / MSCore unusable) with res. writes as in finish_job.
    void finish_all(const std::wstring& res, const ScHost& h, std::vector<Done>& done, bool writes, const std::string& only)
    {
        std::vector<int> ids = move_ids(only);
        for (int sid : ids) finish_job(sid, res, h, done, writes);
    }

    // WHAT: runs fn(p) under SEH. 0 = ok, else the exception code. C++ exceptions (0xE06D7363) are not taken here.
    // WHY: MSVC forbids __try in a function with C++ objects, so this function has none; the work (and its objects) sits in fn.
    DWORD sc_seh_run(void (*fn)(void*), void* p)
    {
        __try { fn(p); return 0; }
        __except (GetExceptionCode() == 0xE06D7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
    }

    // WHAT: surface-free remaining distance (m) of the warp settle: from the ship (loc uu) to the job's target. 1e9 when unknown.
    // WHY: 1:1 with warp_remain_m (mb_steer.hpp). Mode 0 with a registry tag asks the host (the registry holds the rock centre and radius).
    //      A mode 0 job with tag 0 uses ore_loc, which is zero for it (legacy: the else branch of warp_remain_m; kept as the code does).
    double warp_remain_m(const ScJob& j, const double loc_uu[3], const ScHost& h)
    {
        double p[3] = {loc_uu[0] / SM_UU_PER_M, loc_uu[1] / SM_UU_PER_M, loc_uu[2] / SM_UU_PER_M};
        double q[3] = {0, 0, 0};
        double rad = 0.0;
        if (j.mode == 1)
        {
            for (int i = 0; i < 3; ++i) q[i] = static_cast<double>(j.ch[i]) * SM_CHUNK_M + j.loc[i] / SM_RAW_PER_M;
        }
        else if (j.mode == 0 && j.tag > 0)
        {
            double out = 0.0;
            if (!h.remain_m) return 1e9;
            try { if (h.remain_m(j.sid, p, &out) == 1) return out; }
            catch (...) {}
            return 1e9;
        }
        else
        {
            for (int i = 0; i < 3; ++i) q[i] = j.ore_loc[i] / SM_UU_PER_M;
        }
        return remain_dist_m(p, q, rad);
    }

    // WHAT: one job tick. Legacy move_job_fn + move_aim_step, in the legacy order:
    //       1 flight snapshot (mask), 2 quat, 3 calibration, 4 timers and turn rate, 5 frame, 6 warp wait, 7 aim numbers (rock / point / ore),
    //       8 err, 9 phase 1 decision, 10 aim offset (view_send), 11 bubble brake, 12 low power and approach timeout, 13 approach Throttle step, 14 Throttle write, 15 trace.
    //       Every finish is followed by return (the job is erased).
    void step_job(ScJob& j, double dt, const ScHost& h, std::vector<Done>& done)
    {
        const int sid = j.sid;
        // 1. snapshot: POWER only for the approach (phase 2, not warping)
        const int mask = FL_POSE | (j.profile == 1 ? FL_LIMITS : 0) | ((j.kind == 1 && j.phase == 2 && !j.warp_wait) ? FL_POWER : 0);
        MsFlight f;
        std::memset(&f, 0, sizeof(f));
        f.size = static_cast<int32_t>(sizeof(MsFlight));
        const int rc = ms_flight(sid, mask, &f);
        if (rc < 0) { finish_job(sid, L"error: MSCore flight unavailable", h, done, true); return; }
        if (rc == 0 || !(f.flags & FF_ALIVE)) { finish_job(sid, L"error: ship not found during move", h, done, true); return; }
        // 2. quaternion
        if (!(f.flags & FF_OBJ_POSE)) { finish_job(sid, L"error: Quat unreadable", h, done, true); return; }
        // 3. calibration (learned once; the speed monitor usually learns it first)
        if (sc_calib_learn(f))
            host_logw(h, L"ACTFRAME calibrated qmode=" + std::to_wstring(g_calib.qmode) + L" ax=" + std::to_wstring(g_calib.ax[0]) + L"," +
                             std::to_wstring(g_calib.ax[1]) + L"," + std::to_wstring(g_calib.ax[2]));
        // 4. timers and turn rate (quat sample every SM_SAMPLE_S)
        j.t += dt;
        j.pt += dt;
        j.samp += dt;
        j.log_acc += dt;
        if (j.samp >= SM_SAMPLE_S)
        {
            j.rate = quat_angle_deg(j.qs, f.quat) / j.samp;
            std::memcpy(j.qs, f.quat, sizeof(j.qs));
            j.samp = 0.0;
        }
        // 5. frame (a failed frame ends the job below, except during the warp wait)
        ScFrame fr;
        std::wstring why;
        const bool frame_ok = frame_from_flight(f, fr, why);
        // 6. warp wait: no thrust until warp_settle_done
        if (j.warp_wait)
        {
            j.warp_t += dt;
            write_thr(j, 0, h);
            j.thr = 0;
            j.thr_t = 0.0;
            const double rem = frame_ok ? warp_remain_m(j, fr.loc, h) : 1e9;
            if (!warp_settle_done(j.warp_t, rem)) return;
            j.warp_wait = false;
            host_tlog(h, L"far warp landed ship=" + std::to_wstring(sid) + L" after " + std::to_wstring(j.warp_t) + L" s");
            return;
        }
        if (!frame_ok) { finish_job(sid, j.mode == 1 ? L"error: point lost" : L"error: rock lost", h, done, true); return; }
        // 7. aim numbers
        ScAim aim;
        if (j.mode == 1)
        {
            double rl[3] = {0, 0, 0};
            if (!pt_rloc_calc(fr, f.sys, f.ch, f.loc_raw, j.sys, j.ch, j.loc, rl) || !aim_pt(fr, rl, aim))
            { finish_job(sid, L"error: point lost", h, done, true); return; }
        }
        else if (j.mode == 2)
        {
            j.ore_t += dt;
            const double dv[3] = {j.ore_loc[0] - fr.loc[0], j.ore_loc[1] - fr.loc[1], j.ore_loc[2] - fr.loc[2]};
            if (ore_refresh_due(norm3(dv) / SM_UU_PER_M, j.ore_fresh, j.ore_t))
            {
                double p[3] = {0, 0, 0};
                if (!host_ore_loc(h, sid, j.handle, p)) { finish_job(sid, L"error: rock lost", h, done, true); return; }
                std::memcpy(j.ore_loc, p, sizeof(p));
                j.ore_fresh = true;
                j.ore_t = 0.0;
            }
            if (!aim_pt(fr, j.ore_loc, aim)) { finish_job(sid, L"error: rock lost", h, done, true); return; }
        }
        else
        {
            if (j.handle && !host_target_ok(h, sid, j.handle)) { finish_job(sid, L"error: rock lost", h, done, true); return; }
            if (!aim_pt(fr, j.pt_uu, aim)) { finish_job(sid, L"error: rock lost", h, done, true); return; }
        }
        // 8. heading error
        const double err = aim.err_deg;
        j.err = err;
        const bool approach = j.kind == 1;
        const bool quick = j.profile == 1;
        // quick: closing speed from the distance change (the speed monitor value is stale/slow), EMA 0.3, never below 0
        if (quick)
        {
            if (j.prev_dist_m >= 0.0 && dt >= 0.005) j.v_close = 0.7 * j.v_close + 0.3 * (std::max)(0.0, (j.prev_dist_m - aim.dist_m) / dt);
            j.prev_dist_m = aim.dist_m;
            const double vm = (f.max_mod_raw > 0 ? f.max_mod_raw : f.max_base_raw) / SM_RAW_PER_M;
            if (vm > 0.0) j.vmax_ms = vm;
            if (j.vmax_init <= 0.0)
            {
                j.vmax_init = j.vmax_ms > 0.0 ? j.vmax_ms : 100.0;
                const auto kv = g_vmax_est.find(sid);
                j.vmax_est = kv != g_vmax_est.end() ? std::clamp(kv->second, 0.5 * j.vmax_init, 2.0 * j.vmax_init) : j.vmax_init;
            }
        }
        // 9. phase 1 (aim) ends
        if (j.phase == 1)
        {
            Phase1 p1 = phase1_decide(approach, err, j.pt);
            if (quick && approach && p1 == P1_NONE && err < SQ_HEAD_DEG) p1 = P1_TO_PHASE2;   // quick: start the approach at a wide error
            if (p1 == P1_FACE_OK) { finish_job(sid, L"ok heading_error=" + std::to_wstring(err) + L"deg", h, done, true); return; }
            if (p1 == P1_FACE_TIMEOUT) { finish_job(sid, L"error: face timeout err=" + std::to_wstring(err), h, done, true); return; }
            if (p1 == P1_TO_PHASE2)
            {
                j.phase = 2;
                j.pt = 0.0;
                host_tlog(h, L"approach phase, err=" + std::to_wstring(err));
            }
        }
        // 10. aim offset (view_send) every SM_VIEW_PERIOD_S, both phases
        j.view_t += dt;
        if (j.view_t >= SM_VIEW_PERIOD_S)
        {
            j.view_t = 0.0;
            double yaw = 0.0, pit = 0.0;
            bool aligned_now = false;
            view_offset(err, aim.a_body, yaw, pit, aligned_now, quick ? (std::min)(20.0 * (j.turn_pct > 0 ? j.turn_pct : 150) / 100.0, 60.0) : SM_VIEW_MAX_DEG);
            std::wstring e;
            if (aligned_now)
            {
                if (!j.aligned)
                {
                    j.aligned = true;
                    if (!ms_view_send(j.cid, 0, 0, e)) { finish_job(sid, L"error: view_send " + e, h, done, true); return; }
                }
            }
            else
            {
                j.aligned = false;
                ++g_trace_n;
                if (g_trace_n % 10 == 0)
                    host_tlog(h, L"aim err=" + std::to_wstring(err) + L" offset yaw=" + std::to_wstring(yaw) + L" pitch=" + std::to_wstring(pit) +
                                 L" rate_deg_s=" + std::to_wstring(j.rate));
                if (!ms_view_send(j.cid, sc_view_raw(yaw), sc_view_raw(pit), e)) { finish_job(sid, L"error: view_send " + e, h, done, true); return; }
            }
        }
        // 11. bubble brake: no thrust until stopped (SC3: ShipControl's own ladder, or the host's legacy brake)
        if (bub_braking(sid) || host_braking(h, sid))
        {
            write_thr(j, 0, h);
            j.thr = 0;
            j.thr_t = 0.0;
            return;
        }
        if (j.phase != 2) return;
        // 12. low power (power read in this snapshot, or one extra read on the transition tick) and the approach timeout
        bool lp = false;
        int lp_thr = 0;
        double lp_frac = 0.0;
        {
            MsFlight pw = f;
            if (!(mask & FL_POWER))
            {
                std::memset(&pw, 0, sizeof(pw));
                pw.size = static_cast<int32_t>(sizeof(MsFlight));
                ms_flight(sid, FL_POWER, &pw);
            }
            lp_state_from((pw.flags & FF_GRID) != 0, pw.grid_prod, pw.grid_cons, pw.grid_eff, (pw.flags & FF_LP_KNOWN) != 0, (pw.flags & FF_LP_ON) != 0,
                          (pw.flags & FF_LP_LIMITS) != 0, pw.lp_thr, pw.lp_frac, lp, lp_thr, lp_frac);
        }
        if (j.t >= approach_timeout_s(SM_APPROACH_TIMEOUT_S, lp, lp_frac)) { finish_job(sid, L"error: approach timeout", h, done, true); return; }
        // 13. surface and stop distance (m), speed (m/s), approach Throttle step
        const double surface = aim.dist_m - j.radius_m;
        const double remaining = surface - j.stop_m;
        double v_ms = 0.0;
        if (g_spd.watch.count(sid))
        {
            auto m = g_spd.mon.find(sid);
            if (m != g_spd.mon.end() && m->second.have) v_ms = m->second.v / SM_RAW_PER_M;
        }
        ApproachOut o;
        if (quick && j.follow && j.ff_valid)
        {
            // follow: own speed = speed monitor magnitude when it has a sample, else the closing speed (the goal is fixed during a job, Fleet restarts it).
            // The monitor is total speed, so sideways drift counts as forward; thrust is held at 0 above SQ_HEAD_DEG anyway.
            double v_own = j.v_close;
            if (g_spd.watch.count(sid))
            {
                auto m = g_spd.mon.find(sid);
                if (m != g_spd.mon.end() && m->second.have) v_own = std::fabs(m->second.v) / SM_RAW_PER_M;
            }
            v_ms = v_own;
            const double v_cap = j.ff_cms / 100.0;
            const double slot = err > 90.0 ? -remaining : remaining;   // goal behind the nose = overshot the slot
            const int cap = lowpower_throttle_cap((std::min)(SM_APPROACH_MAX_THR, j.steps), lp, lp_thr);
            int thr = 0;
            if (err > SQ_HEAD_DEG && err <= 90.0) thr = 0;   // turn first, hold the throttle
            else thr = quick_match_throttle(slot, v_cap, v_own, j.vmax_est, cap, j.thr, SQ_KP, SQ_KD, SQ_BAND_M, j.brake_pct, j.overshoot_m).thr;
            j.steady_t = std::abs(thr - j.thr_last) <= 1 ? j.steady_t + dt : 0.0;
            j.thr_last = thr;
            j.vmax_est = vmax_learn(j.vmax_est, j.vmax_init, thr, v_own, j.steady_t);
            g_vmax_est[sid] = j.vmax_est;
            o = ApproachOut{thr, 0.0, thr, false};
            j.q_log += dt;
            if (j.q_log >= 0.25)
            {
                j.q_log = 0.0;
                host_logw(h, L"ShipControl match ship=" + std::to_wstring(sid) + L" v_cap=" + std::to_wstring(v_cap) + L" v_own=" + std::to_wstring(v_own) +
                                 L" thr=" + std::to_wstring(thr) + L" err=" + std::to_wstring(slot) + L" vmax_est=" + std::to_wstring(j.vmax_est));
            }
        }
        else if (quick)
        {
            v_ms = j.v_close;
            const QuickOut q = approach_step_quick(remaining, v_ms, err, j.steps, lp, lp_thr, j.thr, j.vmax_ms > 0.0 ? j.vmax_ms : 100.0, j.brake_pct, j.overshoot_m);
            o = ApproachOut{q.thr, 0.0, q.target, q.done};
            j.q_log += dt;
            if (j.q_log >= 0.25)
            {
                j.q_log = 0.0;
                host_logw(h, L"ShipControl quick ship=" + std::to_wstring(sid) + L" thr=" + std::to_wstring(q.thr) + L" target=" + std::to_wstring(q.target) +
                                 L" v_close=" + std::to_wstring(v_ms) + L" remaining=" + std::to_wstring(remaining));
            }
        }
        else o = approach_step(remaining, v_ms, err, j.steps, lp, lp_thr, j.thr, j.thr_t, dt);
        j.thr = o.thr;
        j.thr_t = o.thr_t;
        if (o.done)
        {
            finish_job(sid, L"ok surface_m=" + std::to_wstring(surface) + L" speed=" + std::to_wstring(v_ms) + L" m/s", h, done, true);
            return;
        }
        // 14. Throttle write, trace about once per second
        write_thr(j, j.thr, h);
        if (j.log_acc >= 1.0)
        {
            j.log_acc = 0.0;
            host_tlog(h, L"approach surface_m=" + std::to_wstring(surface) + L" md=" + std::to_wstring(j.stop_m) + L" remaining_m=" + std::to_wstring(remaining) +
                         L" err=" + std::to_wstring(err) + L" target=" + std::to_wstring(o.target) + L" thr=" + std::to_wstring(j.thr) +
                         L" speed_m_s=" + std::to_wstring(v_ms) + L" pt=" + std::to_wstring(j.mode == 1 ? 1 : 0));
        }
    }

    // WHAT: SEH context of one job step.
    struct StepCtx { ScJob* j; double dt; const ScHost* h; std::vector<Done>* done; };
    // WHAT: plain function for sc_seh_run (no __try here, so the C++ objects of step_job are allowed).
    void step_fn(void* p)
    {
        auto* c = static_cast<StepCtx*>(p);
        step_job(*c->j, c->dt, *c->h, *c->done);
    }
    // WHAT: SEH context of the fault finish.
    struct FaultCtx { int sid; const ScHost* h; std::vector<Done>* done; };
    void fault_finish_fn(void* p)
    {
        auto* c = static_cast<FaultCtx*>(p);
        finish_job(c->sid, L"error: target lost", *c->h, *c->done, true);
    }

    // WHAT: 8 hex digits of an exception code.
    std::wstring hex32(unsigned long v)
    {
        wchar_t b[16] = {0};
        std::swprintf(b, 16, L"%08lx", v);
        return b;
    }

    // WHAT: move job tick over every live job. Copies the id list first (a finish erases its entry).
    //       A fault inside a job ends that job only ("error: target lost", logged once). Returns the number of jobs stepped.
    int sc_move_tick(double dt, const ScHost& h, std::vector<Done>& done, const std::string& only)
    {
        std::vector<int> ids = move_ids(only);
        int n = 0;
        for (int sid : ids)
        {
            auto it = g_jobs_mv.find(sid);
            if (it == g_jobs_mv.end()) continue;
            ++n;
            StepCtx c{&it->second, dt, &h, &done};
            const DWORD code = sc_seh_run(step_fn, &c);
            if (code == 0) continue;
            auto jt = g_jobs_mv.find(sid);
            const wchar_t* kind = jt != g_jobs_mv.end() ? job_kind_text(jt->second.kind) : L"?";
            host_logw(h, L"MOVE fault ship=" + std::to_wstring(sid) + L" job=" + kind + L" code=0x" + hex32(code));
            if (jt != g_jobs_mv.end())
            {
                FaultCtx fc{sid, &h, &done};
                if (sc_seh_run(fault_finish_fn, &fc) != 0) host_logw(h, L"MOVE fault finish faulted too ship=" + std::to_wstring(sid));
            }
            g_jobs_mv.erase(sid);
        }
        return n;
    }

    // WHAT: starts a move job for ship sid (owner already checked by the caller). Preconditions read before the first write.
    //       Order: 1 no job running, 2 MSCore bound, 3 flight snapshot (ship alive), 4 PilotConsoleObject, 5 Throttle steps, 6 build the job,
    //       7 warp (kind 1, host.warp_begin), 8 store the job and set req.out_cid / out_steps. false = why holds the 'error: ...' text.
    bool move_start_job(const std::string& owner, int sid, ScMoveReq& req, const ScHost& h, std::wstring& why)
    {
        if (g_jobs_mv.count(sid)) { why = L"error: move job already running"; return false; }
        std::wstring e;
        if (!ms_load(e)) { why = L"error: MSCore flight unavailable"; return false; }
        MsFlight f;
        std::memset(&f, 0, sizeof(f));
        f.size = static_cast<int32_t>(sizeof(MsFlight));
        const int rc = ms_flight(sid, FL_POSE | FL_STATE, &f);
        if (rc < 0) { why = L"error: MSCore flight unavailable"; return false; }
        if (rc == 0 || !(f.flags & FF_ALIVE)) { why = L"error: ship not found"; return false; }
        const int cid = ms_console(sid);
        if (cid <= 0) { why = L"error: PilotConsoleObject not found"; return false; }
        const int steps = (f.flags & FF_STEPS) ? f.steps : 0;
        if (steps < 1) { why = L"error: Throttle steps unreadable"; return false; }
        ScJob j;
        j.kind = req.kind == 1 ? 1 : 0;
        j.sid = sid;
        j.seq = req.seq;
        j.mode = req.mode;
        j.steps = steps;
        j.cid = cid;
        j.tag = req.tag;
        j.sys = req.sys;
        std::memcpy(j.ch, req.ch, sizeof(j.ch));
        std::memcpy(j.loc, req.loc, sizeof(j.loc));
        std::memcpy(j.pt_uu, req.pt_uu, sizeof(j.pt_uu));
        j.handle = req.handle;
        j.radius_m = req.radius_m;
        j.stop_m = req.stop_m;
        j.profile = req.profile == 1 ? 1 : 0;
        j.brake_pct = req.brake_pct;
        j.turn_pct = req.turn_pct;
        j.overshoot_m = req.overshoot_m;
        j.ff_cms = req.ff_cms;
        j.follow = req.follow == 1 ? 1 : 0;
        j.ff_valid = req.ff_valid == 1 ? 1 : 0;
        j.phase = 1;
        j.owner = owner;
        if (f.flags & FF_OBJ_POSE) std::memcpy(j.qs, f.quat, sizeof(j.qs));
        if (j.mode == 2) std::memcpy(j.ore_loc, req.pt_uu, sizeof(j.ore_loc));
        if (j.kind == 1 && h.warp_begin)
        {
            int w = 0;
            try { w = h.warp_begin(sid, &req); }
            catch (...) { w = 0; }
            if (w == 1) { j.warp_wait = true; j.warp_t = 0.0; }
        }
        req.out_cid = cid;
        req.out_steps = steps;
        g_jobs_mv[sid] = j;
        return true;
    }

    // WHAT: cancels the job of ship sid for owner: aim offset stopped (view_zero), job erased. No zero, no callback. 1 = cancelled or none, 0 = other owner.
    int move_cancel_job(const std::string& owner, int sid, const ScHost& h)
    {
        auto it = g_jobs_mv.find(sid);
        if (it == g_jobs_mv.end()) return 1;
        if (it->second.owner != owner) return 0;
        const ScJob j = it->second;
        g_jobs_mv.erase(it);
        view_zero_sc(j.cid, h);
        return 1;
    }

    // WHAT: drops the job of sid when it belongs to owner (SC_release / SC_release_all): view_zero and ms_zero (MSCore bound), erase, no callback.
    void drop_job(int sid, const std::string& owner, const ScHost& h)
    {
        auto it = g_jobs_mv.find(sid);
        if (it == g_jobs_mv.end() || it->second.owner != owner) return;
        const ScJob j = it->second;
        g_jobs_mv.erase(it);
        view_zero_sc(j.cid, h);
        std::wstring e, why;
        if (ms_load(why)) ms_zero(sid, e);
    }

    // WHAT: drops every job of owner (SC_release_all).
    void drop_jobs_owned(const std::string& owner, const ScHost& h)
    {
        std::vector<int> ids;
        for (const auto& kv : g_jobs_mv) ids.push_back(kv.first);
        for (int sid : ids) drop_job(sid, owner, h);
    }

    // WHAT: status of the job of ship sid. No job: active 0, the rest 0 / -1 (err) / zeros.
    void fill_status(int sid, ScMoveStatus& o)
    {
        auto it = g_jobs_mv.find(sid);
        o.active = it != g_jobs_mv.end() ? 1 : 0;
        o.phase = o.active ? it->second.phase : 0;
        o.warp_wait = o.active && it->second.warp_wait ? 1 : 0;
        o.thr = o.active ? it->second.thr : 0;
        o.kind = o.active ? it->second.kind : 0;
        o.err_deg = o.active ? it->second.err : -1.0;
        o.t_s = o.active ? it->second.t : 0.0;
        for (int i = 0; i < 3; ++i) o.ore_loc[i] = o.active ? it->second.ore_loc[i] : 0.0;
    }
}
