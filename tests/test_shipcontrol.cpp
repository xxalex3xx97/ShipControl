// FILE: tests/test_shipcontrol.cpp
// WHAT: unit checks for the ShipControl owner registry (sc_owner.hpp): claim, same-owner claim, refused claim, release, release_all, owner_of. No game needed.
// WHY: the registry decides which behaviour may drive a ship; the checks keep the refuse rules fixed.
// RUN: tests\run.ps1 (builds with cl, runs, exit code 0 = all PASS). Or by hand: see run.ps1.
// DEPENDS ON: sc_owner.hpp (pure std, no UE headers).
// NOTE: CHECK counts failures and keeps going; the summary line prints the pass and fail counts.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "sc_logic_path.hpp"
#include "sc_owner.hpp"
#include "sc_speed.hpp"   // SC2a: speed judge (pure part); includes sc_mscore.hpp (windows.h only)
#include "sc_logic_steer.hpp"   // SC2b A1: moved steering rules
#include "sc_logic_bubble.hpp"  // SC3 A1: moved bubble ladder (pure)
#include "sc_frame.hpp"         // SC2b A1: frame, aim, calibration
#include "sc_hosts.hpp"
#include "sc_bubble_run.hpp"   // API 5: bub_ids
#include "sc_move.hpp"          // SC2b A2: move jobs (ABI via sc_api.h, sc_view_raw); includes sc_bubble.hpp (SC3 A2 ladder state)

static int g_pass = 0;
static int g_fail = 0;
#define NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))
#define NEARB(a, b, eps) (std::fabs((double)(a) - (double)(b)) <= (eps))   // value form, for use inside CHECK(...)

#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void t_warp_settle()
{
    CHECK(!warp_settle_done(1.0, 100.0));          // under the 2 s minimum: wait even when arrived
    CHECK(warp_settle_done(2.0, 300.0));           // arrived at the dock
    CHECK(!warp_settle_done(5.0, 1500.0));         // still far
    CHECK(!warp_settle_done(5.0, -1.0));           // position unknown: wait
    CHECK(warp_settle_done(30.0, -1.0));           // cap: go on after 30 s
    CHECK(warp_settle_done(5.0, 1000.0));          // exactly the arrival distance counts
}

static void t_hazard_dir()
{
    // circle directions: base turned 90 deg per step, z kept
    const double base[3] = {1.0, 0.0, 0.5};
    double d[3];
    hazard_dir(base, 0, d); CHECK(d[0] == 1.0 && d[1] == 0.0 && d[2] == 0.5);
    hazard_dir(base, 1, d); NEAR(d[0], 0.0, 1e-9); NEAR(d[1], 1.0, 1e-9); CHECK(d[2] == 0.5);
    hazard_dir(base, 2, d); NEAR(d[0], -1.0, 1e-9); NEAR(d[1], 0.0, 1e-9);
    hazard_dir(base, 4, d); NEAR(d[0], 1.0, 1e-9); NEAR(d[1], 0.0, 1e-9);
    hazard_dir(base, -1, d); NEAR(d[0], 0.0, 1e-9); NEAR(d[1], -1.0, 1e-9);
}
static void t_path_block_geom()
{
    const double a[3] = {0, 0, 0}, b[3] = {1000, 0, 0};
    double d = -1.0;
    const double front[3] = {500, 100, 0}, behind[3] = {-500, 0, 0}, beyond[3] = {1500, 0, 0}, tangent[3] = {500, 150, 0}, inside[3] = {500, 149.9, 0};
    CHECK(seg_sphere_blocks(a, b, front, 150, d));
    NEAR(d, 100.0, 1e-9);
    CHECK(!seg_sphere_blocks(a, b, behind, 600, d));           // behind the start
    CHECK(!seg_sphere_blocks(a, b, beyond, 600, d));           // past the end (even inside the radius there)
    CHECK(!seg_sphere_blocks(a, b, tangent, 150, d));          // distance == radius: clear
    CHECK(seg_sphere_blocks(a, b, inside, 150, d));            // just inside
    CHECK(!seg_sphere_blocks(a, a, front, 150, d));            // zero-length segment: nothing between
    // the blocker nearest along the path is returned
    std::vector<PathRock> rk(2);
    rk[0].c[0] = 800; rk[0].c[1] = 0; rk[0].c[2] = 0; rk[0].r = 100;
    rk[1].c[0] = 300; rk[1].c[1] = 0; rk[1].c[2] = 0; rk[1].r = 50;
    double dd = -1.0;
    CHECK(path_first_blocker(a, b, rk.data(), rk.size(), dd) == 1);
    CHECK(path_first_blocker(a, b, rk.data(), 1, dd) == 0);
    rk[0].r = 0.0; rk[1].r = 0.0;
    CHECK(path_first_blocker(a, b, rk.data(), rk.size(), dd) == -1);             // radius 0 never blocks
}
static void t_path_waypoint_geom()
{
    const double a[3] = {0, 0, 0}, b[3] = {1000, 0, 0};
    const double c[3] = {500, 100, 0};
    double w[3] = {0, 0, 0};
    path_waypoint(a, b, c, 400.0, w);
    NEAR(w[0], 500.0, 1e-9); NEAR(w[1], 500.0, 1e-9); NEAR(w[2], 0.0, 1e-9);   // beside the blocker, away from the path
    const double v[3] = {w[0] - c[0], w[1] - c[1], w[2] - c[2]};
    NEAR(v[0] * 1000.0 + v[1] * 0.0 + v[2] * 0.0, 0.0, 1e-6);                 // perpendicular to the path
    NEAR(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), 400.0, 1e-9);     // exactly radius + clearance from the blocker
    // blocker exactly on the path line: still perpendicular, still at radius
    const double on[3] = {500, 0, 0};
    path_waypoint(a, b, on, 300.0, w);
    NEAR(w[0], 500.0, 1e-9); NEAR(w[1], -300.0, 1e-9); NEAR(w[2], 0.0, 1e-9);
}
static void t_path2_gates()
{
    // replan gate: a waypoint flight blocks new plans; one plan per target per 20 s
    CHECK(replan_gate(true, 5, 0, 0, 1000) == RG_WP_ACTIVE);
    CHECK(replan_gate(false, 5, 5, 10000, 29999) == RG_COOLDOWN);   // same target, 19.9 s
    CHECK(replan_gate(false, 5, 5, 10000, 30000) == RG_OK);         // 20 s: allowed again
    CHECK(replan_gate(false, 6, 5, 10000, 11000) == RG_OK);         // another target: no cooldown
    CHECK(replan_gate(false, 5, 5, 0, 11000) == RG_OK);             // no plan yet (last_ms 0)
    // start grace: overlapping the start and (moving away or first 60 m inside) is ignored
    CHECK(grace_ignore(10.0, 12.0, 8.0, true, false));              // overlap, moving away
    CHECK(grace_ignore(10.0, 12.0, 8.0, false, true));              // overlap, first 60 m inside
    CHECK(!grace_ignore(10.0, 12.0, 8.0, false, false));            // overlap but ahead and clear of the first 60 m
    CHECK(!grace_ignore(45.0, 12.0, 8.0, true, true));              // not overlapping the start (45 > 12+8+20=40)
    CHECK(grace_ignore(39.9, 12.0, 8.0, true, false));              // inside the 40 m start zone
    // strikes: the third block of the same blocker on the same target skips the target
    CHECK(!strikes_reached(1) && !strikes_reached(2) && strikes_reached(3));
}
static void t_tune_geom_path()
{
    CHECK(std::fabs(block_r_m(10.0, 8.0) - 16.0) < 1e-9);          // 10 x 0.8 + 8
    CHECK(std::fabs(block_r_m(35.0, 8.0) - 36.0) < 1e-9);          // 35 x 0.8 + 8
    CHECK(std::fabs(block_r_m(-1.0, 8.0) - 8.0) < 1e-9);           // negative radius counts as 0
    // side points: dir along +X, centre at (100,0,0), offset 50: side +1 is -Y, side -1 is +Y
    const double c[3] = {100, 0, 0}, dx[3] = {1, 0, 0}, dz[3] = {0, 0, 1};
    double o[3];
    side_point(c, dx, 50.0, 1, o);
    CHECK(std::fabs(o[0] - 100) < 1e-9 && std::fabs(o[1] + 50) < 1e-9 && std::fabs(o[2]) < 1e-9);
    side_point(c, dx, 50.0, -1, o);
    CHECK(std::fabs(o[1] - 50) < 1e-9);
    side_point(c, dz, 50.0, 1, o);                                  // dir along Z: +Y fallback
    CHECK(std::fabs(o[1] - 50) < 1e-9 && std::fabs(o[0] - 100) < 1e-9);
    // candidates: 0 = +1.0x, 1 = -1.0x, 2 = +1.6x, 3 = -1.6x (block radius 20)
    double w[3];
    waypoint_candidate(0, c, dx, 20.0, w); CHECK(std::fabs(w[1] + 20) < 1e-9);
    waypoint_candidate(1, c, dx, 20.0, w); CHECK(std::fabs(w[1] - 20) < 1e-9);
    waypoint_candidate(2, c, dx, 20.0, w); CHECK(std::fabs(w[1] + 32) < 1e-9);
    waypoint_candidate(3, c, dx, 20.0, w); CHECK(std::fabs(w[1] - 32) < 1e-9);
    const bool none[4] = {false, false, false, false};
    const bool second[4] = {false, true, false, true};
    CHECK(waypoint_pick(none, 4) == -1);
    CHECK(waypoint_pick(second, 4) == 1);                            // first clear candidate in order
    // leg clearance through path_first_blocker: rock at 20 m off a 1 km leg blocks (block 36), at 50 m does not (block 14.4)
    const double a[3] = {0, 0, 0}, b[3] = {1000, 0, 0};
    PathRock pr[2];
    pr[0].c[0] = 500; pr[0].c[1] = 20; pr[0].c[2] = 0; pr[0].r = block_r_m(35.0, 8.0);
    pr[1].c[0] = 500; pr[1].c[1] = 50; pr[1].c[2] = 0; pr[1].r = block_r_m(8.0, 8.0);
    double dd = 0.0;
    CHECK(path_first_blocker(a, b, pr, 1, dd) == 0);
    CHECK(path_first_blocker(a, b, pr + 1, 1, dd) == -1);
}

// SC2b A1: moved steering rules (sc_logic_steer.hpp) and frame / aim maths (sc_math.hpp, sc_frame.hpp).
// Vectors: approach_throttle, approach_timeout_s, lowpower_throttle_cap, power_no_power = test_logic.cpp.
static void t_moved_steer()
{
    CHECK(approach_throttle(5000.0, 40.0, 10) == 10);
    CHECK(approach_throttle(500.0, 40.0, 10) == 10);
    CHECK(approach_throttle(250.0, 40.0, 10) == 5);
    CHECK(approach_throttle(250.0, 40.0, 4) == 2);
    CHECK(approach_throttle(1.0, 0.0, 10) == 1);
    CHECK(approach_throttle(0.0, 0.0, 10) == 0);
    CHECK(approach_throttle(-3.0, 0.0, 10) == 0);
    CHECK(approach_throttle(250.0, 40.0, 0) == 0);
    for (double d = 1.0; d < 600.0; d += 7.0) { const int t = approach_throttle(d, 0.0, 10); CHECK(t >= 1 && t <= 10); }
    NEAR(approach_timeout_s(300.0, true, 0.333), 300.0 / 0.333, 1e-9);
    CHECK(approach_timeout_s(300.0, false, 0.333) == 300.0);
    CHECK(approach_timeout_s(300.0, true, 0.0) == 300.0);
    CHECK(approach_timeout_s(300.0, true, 1.0) == 300.0);
    CHECK(lowpower_throttle_cap(10, true, 5) == 5);
    CHECK(lowpower_throttle_cap(3, true, 5) == 3);
    CHECK(lowpower_throttle_cap(10, false, 5) == 10);
    CHECK(lowpower_throttle_cap(10, true, 0) == 10);
    CHECK(power_no_power(40.0, 0.0, 0.0));
    CHECK(!power_no_power(0.0, 0.0, 0.0));
    CHECK(!power_no_power(200.0, 80.0, 0.4));
}

// SC6 D3a: no-power test from one flight snapshot (sc_logic_steer.hpp power_dead_from).
static void t_power_dead_from()
{
    bool known = false;
    CHECK(power_dead_from(true, 0.0, 40.0, 0.0, false, false, known) && known);     // grid dead
    CHECK(!power_dead_from(true, 80.0, 200.0, 0.4, false, false, known) && known);  // brown-out, grid still produces
    CHECK(!power_dead_from(true, 200.0, 80.0, 1.0, true, true, known) && known);    // grid read wins over the flag (grid fine, flag on)
    CHECK(power_dead_from(false, 0.0, 0.0, 0.0, true, true, known) && known);       // no grid, flag on
    CHECK(!power_dead_from(false, 0.0, 0.0, 0.0, true, false, known) && known);     // no grid, flag off
    CHECK(!power_dead_from(false, 0.0, 0.0, 0.0, false, true, known) && !known);    // nothing known
}

static void t_lp_state_from()
{
    bool lp = false;
    int thr = -1;
    double frac = -1.0;
    CHECK(lp_state_from(true, 0.0, 40.0, 0.0, false, false, true, 5, 0.333, lp, thr, frac) && lp && thr == 5);   // grid dead, limits ok
    NEAR(frac, 0.333, 1e-12);
    CHECK(!lp_state_from(true, 80.0, 200.0, 0.4, false, false, true, 5, 0.333, lp, thr, frac) && !lp && thr == 0 && frac == 0.0);   // brown-out
    CHECK(lp_state_from(false, 0.0, 0.0, 0.0, true, true, true, 5, 0.333, lp, thr, frac) && lp && thr == 5);   // no grid, flag known and on
    CHECK(!lp_state_from(false, 0.0, 0.0, 0.0, false, false, true, 5, 0.333, lp, thr, frac) && !lp);          // flag unknown
    CHECK(!lp_state_from(true, 0.0, 40.0, 0.0, false, false, false, 5, 0.333, lp, thr, frac) && !lp && thr == 0 && frac == 0.0);   // dead, limits unreadable
}

static void t_phase1_steer()
{
    CHECK(phase1_decide(false, 1.9, 0.0) == P1_FACE_OK);
    CHECK(phase1_decide(false, 5.0, 30.0) == P1_FACE_TIMEOUT);
    CHECK(phase1_decide(false, 5.0, 29.9) == P1_NONE);
    CHECK(phase1_decide(true, 9.9, 0.0) == P1_TO_PHASE2);
    CHECK(phase1_decide(true, 10.0, 0.0) == P1_NONE);
}

static void t_approach_step_quick()
{
    // args: remaining, v, err, cap, lp, lp_thr, thr_prev, vmax, brake_pct, overshoot
    QuickOut q = approach_step_quick(5000.0, 0.0, 0.0, 10, false, 0, 0, 100.0, 100, 0);
    CHECK(q.thr == 10 && q.target == 10 && !q.done);                       // far: full throttle at once, no ramp
    q = approach_step_quick(30.0, 80.0, 0.0, 10, false, 0, 10, 100.0, 100, 0);
    CHECK(q.thr == -10);                                                   // want_v 15, v 80 > 25: hard brake
    q = approach_step_quick(-5.0, 50.0, 0.0, 10, false, 0, 10, 100.0, 100, 0);
    CHECK(q.thr == -10 && !q.done);                                        // past the goal, fast
    q = approach_step_quick(10.0, 50.0, 0.0, 10, false, 0, 10, 100.0, 100, 20);
    CHECK(q.thr == -10);                                                   // overshoot_m eats the remaining
    q = approach_step_quick(0.0, 5.0, 0.0, 10, false, 0, 3, 100.0, 100, 0);
    CHECK(q.thr == -2 && !q.done);                                         // past the goal, slow: gentle reverse
    q = approach_step_quick(0.0, 20.0, 0.0, 10, false, 0, 3, 100.0, 100, 0);
    CHECK(q.thr == -4);
    q = approach_step_quick(0.0, 0.5, 0.0, 10, false, 0, -2, 100.0, 100, 0);
    CHECK(q.done && q.thr == 0);
    q = approach_step_quick(5000.0, 0.0, 40.0, 10, false, 0, 5, 100.0, 100, 0);
    CHECK(q.target == 0 && q.thr == 0);                                    // err > 30
    q = approach_step_quick(200.0, 90.0, 0.0, 10, false, 0, -10, 100.0, 100, 0);   // want_v 100 -> v 90 <= want_v
    CHECK(q.thr > 0);                                                      // release
    q = approach_step_quick(100.0, 90.0, 0.0, 10, false, 0, -10, 100.0, 100, 0);   // want_v 50, v 90 > 50
    CHECK(q.thr == -10);                                                   // keeps braking
    q = approach_step_quick(5000.0, 0.0, 0.0, 10, true, 3, 0, 100.0, 100, 0);
    CHECK(q.thr == 3 && q.target == 3);                                    // low-power cap
    q = approach_step_quick(5000.0, 0.0, 0.0, 4, false, 0, 0, 100.0, 100, 0);
    CHECK(q.thr == 4);                                                     // steps cap
    for (double rem = -50.0; rem <= 3000.0; rem += 7.0)
        for (double v = 0.0; v <= 150.0; v += 5.0)
            for (int tp = -10; tp <= 10; tp += 5)
            {
                q = approach_step_quick(rem, v, 0.0, 7, false, 0, tp, 100.0, 100, 0);
                CHECK(q.thr >= -10 && q.thr <= 7);
            }
    // 1D sim: speed follows thr*vmax/10 with a first-order lag (0.8 s), 10 Hz, stop at 100 m; must not overshoot > overshoot_m, must not reverse
    for (int os : {0, 10})
    {
        double pos = 0.0, v = 0.0, vmax = 100.0, minv = 0.0;
        int thr = 0;
        bool done = false;
        for (int i = 0; i < 6000 && !done; ++i)
        {
            q = approach_step_quick(100.0 - pos, v, 0.0, 10, false, 0, thr, vmax, 100, os);
            thr = q.thr;
            done = q.done;
            if (done) break;
            v += (thr * vmax / 10.0 - v) * (0.1 / 0.8);
            pos += v * 0.1;
            minv = (std::min)(minv, v);
        }
        CHECK(done);
        CHECK(pos <= 100.0 + os + 1e-9 || pos - (100.0 + os) < 1.0);         // within the tolerance (+1 m sample slack)
        CHECK(minv > -SQ_DONE_MS - 1e-9);                                    // never really reverses
        if (pos > 100.0 + os + 1e-9) std::printf("sim os=%d pos=%f\n", os, pos);
    }
}

static void t_approach_step_fast()
{
    // args: remaining, v, err, cap, lp, lp_thr, thr_prev, overshoot
    QuickOut q = approach_step_fast(5000.0, 0.0, 0.0, 10, false, 0, 0, 0);
    CHECK(q.thr == 10 && q.target == 10 && !q.done);                       // far: full at once
    q = approach_step_fast(SQ_FULL_M, 0.0, 0.0, 10, false, 0, 0, 0);
    CHECK(q.thr == 10);
    q = approach_step_fast(30.0, 0.0, 0.0, 10, false, 0, 0, 0);
    CHECK(q.thr == 5);                                                     // proportional
    q = approach_step_fast(SQ_STOP_M + 0.5, 99.0, 0.0, 10, false, 0, 0, 0);
    CHECK(q.thr == 1 && !q.done);                                          // just outside the stop distance: creep step 1
    q = approach_step_fast(SQ_STOP_M, 99.0, 0.0, 10, false, 0, 7, 0);
    CHECK(q.thr == 0 && q.done);
    q = approach_step_fast(-50.0, 99.0, 0.0, 10, false, 0, 7, 0);
    CHECK(q.thr == 0 && q.done);                                           // past the goal: stop, never reverse
    q = approach_step_fast(5000.0, 0.0, 40.0, 10, false, 0, 5, 0);
    CHECK(q.thr == 0 && q.target == 0 && !q.done);                         // err > head: turn first
    q = approach_step_fast(5000.0, 0.0, 0.0, 10, true, 3, 0, 0);
    CHECK(q.thr == 3);                                                     // low-power cap
    q = approach_step_fast(5000.0, 0.0, 0.0, 4, false, 0, 0, 0);
    CHECK(q.thr == 4);                                                     // steps cap
    int prev = 0;
    for (double rem = 0.0; rem <= 200.0; rem += 0.5)                     // monotone in remaining, never negative, never above cap
    {
        q = approach_step_fast(rem, 0.0, 0.0, 7, false, 0, 0, 0);
        CHECK(q.thr >= prev && q.thr >= 0 && q.thr <= 7);
        prev = q.thr;
    }
    double acc = 0.0;
    bool a = step_due(acc, 0.04, SQ_STEP_S), b = step_due(acc, 0.04, SQ_STEP_S), c = step_due(acc, 0.04, SQ_STEP_S);
    CHECK(!a && !b && c && acc == 0.0);
    // instant-stop dynamics: speed = thr * vmax / 10 at once, decision every 0.1 s. Still goal 5 km away; moving goals start 200 m behind.
    for (double vmax : {100.0, 200.0})
        for (double vg : {0.0, 10.0, 40.0, 100.0})
        {
            double pos = 0.0, goal = vg == 0.0 ? 5000.0 : 200.0, dec = 1e9;
            int thr = 0, flips = 0, last_sign = 0, maxthr = 0, minthr = 0;
            bool done = false;
            for (int i = 0; i < 40000 && !done; ++i)                      // 0.01 s per tick, 400 s
            {
                if (dec >= SQ_STEP_S)
                {
                    dec = 0.0;
                    q = approach_step_fast(goal - pos, 0.0, 0.0, 10, false, 0, thr, 0);
                    thr = q.thr;
                    done = q.done;
                    if (done) break;
                }
                else dec += 0.01;
                pos += thr * vmax / 10.0 * 0.01;
                goal += vg * 0.01;
                maxthr = (std::max)(maxthr, thr);
                minthr = (std::min)(minthr, thr);
                const int s2 = thr > 0 ? 1 : thr < 0 ? -1 : 0;
                if (s2 != 0) { if (last_sign != 0 && s2 != last_sign) ++flips; last_sign = s2; }
            }
            const double err = goal - pos;
            if (vg == 0.0) CHECK(done && err <= SQ_STOP_M + 1e-9 && err >= -1.0);
            else CHECK(vg >= vmax || (err <= 30.0 && (done || err > SQ_STOP_M)));   // inside Fleet's 30 m dead zone (or stopped; a goal as fast as the ship cannot be caught)
            CHECK(maxthr <= 10 && minthr >= 0 && flips == 0);
            std::printf("fast sim vmax=%.0f vg=%.0f err=%.1f thr=%d\n", vmax, vg, err, thr);
        }
}

static void t_quick_match()
{
    // args: err, v_cap, v_own, vmax_est, cap, thr_prev, kp, kd, band, brake_pct, overshoot
    auto M = [](double err, double vc, double vo, double vm, int cap, int tp) { return quick_match_throttle(err, vc, vo, vm, cap, tp, SQ_KP, SQ_KD, SQ_BAND_M, 100, 10); };
    CHECK(M(0.0, 0.0, 0.0, 100.0, 10, 0).thr == 0);                  // captain stopped, in band
    CHECK(M(2.0, 0.0, 0.0, 100.0, 10, 0).thr == 0);                  // small P term does not move it
    CHECK(M(0.0, 50.0, 50.0, 100.0, 10, 5).thr == 5);               // captain at 50% of vmax: step 5
    CHECK(M(0.0, 50.0, 50.0, 200.0, 10, 5).thr == 3 && M(0.0, 50.0, 50.0, 100.0, 10, 5).thr != M(0.0, 50.0, 50.0, 200.0, 10, 5).thr);   // vmax_est matters
    CHECK(M(500.0, 40.0, 40.0, 100.0, 10, 4).thr == 10);            // far behind: full throttle
    CHECK(M(500.0, 40.0, 40.0, 100.0, 5, 4).thr == 5);              // cap
    CHECK(!M(500.0, 40.0, 40.0, 100.0, 10, 4).done);
    CHECK(M(-50.0, 0.0, 30.0, 100.0, 10, 3).thr == -10);            // overshot, fast: hard brake
    CHECK(M(-50.0, 0.0, 10.0, 100.0, 10, 3).thr < 0 && M(-50.0, 0.0, 10.0, 100.0, 10, 3).thr >= -4);   // slower: limited reverse
    CHECK(M(-50.0, 40.0, 60.0, 100.0, 10, 3).thr < 0);              // ahead of the slot and faster than the captain: negative
    CHECK(M(-3.0, 40.0, 40.0, 100.0, 10, 4).thr >= 0);              // slightly ahead, same speed: no reverse
    CHECK(M(30.0, 0.0, 70.0, 100.0, 10, -10).thr == -10);           // hysteresis: still braking while v_own > v_des
    CHECK(M(200.0, 0.0, 70.0, 100.0, 10, -10).thr > 0);             // v_des 100 > v_own: release
    for (double e = -300.0; e <= 600.0; e += 13.0)
        for (double vc = -20.0; vc <= 120.0; vc += 10.0)
            for (double vo = 0.0; vo <= 200.0; vo += 10.0)
                for (int tp = -10; tp <= 10; tp += 5)
                {
                    const int t = M(e, vc, vo, 100.0, 6, tp).thr;
                    CHECK(t >= -10 && t <= 6);
                }
    // 1D sim: speed follows thr*vmax/10 with a 0.8 s lag (10 Hz); captain at v_c; ship starts 200 m behind its slot
    for (double vc : {40.0, 0.0})
    {
        double cap_pos = 200.0, pos = 0.0, v = 0.0, vmax = 100.0, vmax_est = 100.0, maxneg = 0.0, steady = 0.0, mint = 0.0;
        int thr = 0, last = 0, full_ticks = 0;
        for (int i = 0; i < 1500; ++i)
        {
            const double err = cap_pos - pos;
            thr = quick_match_throttle(err, vc, v, vmax_est, 10, thr, SQ_KP, SQ_KD, SQ_BAND_M, 100, 10).thr;
            steady = std::abs(thr - last) <= 1 ? steady + 0.1 : 0.0;
            last = thr;
            if (thr >= 10) ++full_ticks;
            mint = (std::min)(mint, static_cast<double>(thr));
            v += (thr * vmax / 10.0 - v) * (0.1 / 0.8);
            if (v < 0.0) v = 0.0;                                           // closing speed is not negative
            pos += v * 0.1;
            cap_pos += vc * 0.1;
            maxneg = (std::min)(maxneg, cap_pos - pos);
        }
        const double err = cap_pos - pos;
        CHECK(std::fabs(err) < SQ_BAND_M);                                  // converged in the band
        CHECK(thr == static_cast<int>(std::lround(vc / vmax * 10.0)) || std::abs(thr - vc / vmax * 10.0) <= 1.0);   // holds the captain speed
        CHECK(maxneg > -2.0 * 10.0);                                        // never rams (err >= -2 * overshoot_m)
        CHECK(full_ticks < 400 && mint >= -10.0);                           // full throttle only for the catch-up
        if (vc == 0.0) CHECK(v < 1.0);                                      // captain at rest: ends at rest
    }
    // vmax_est learning
    double est = 100.0;
    for (int i = 0; i < 60; ++i) est = vmax_learn(est, 100.0, 5, 40.0, 2.0);   // truth 80 m/s
    CHECK(NEARB(est, 80.0, 1.0));
    CHECK(vmax_learn(100.0, 100.0, 5, 40.0, 1.0) == 100.0);                 // not steady long enough
    CHECK(vmax_learn(100.0, 100.0, 1, 40.0, 2.0) == 100.0 && vmax_learn(100.0, 100.0, 5, 0.5, 2.0) == 100.0);
    CHECK(vmax_learn(100.0, 100.0, 5, 400.0, 2.0) <= 200.0 + 1e-9);         // clamp holds at 2x init
    est = 100.0;
    for (int i = 0; i < 400; ++i) est = vmax_learn(est, 100.0, 2, 200.0, 2.0);
    CHECK(NEARB(est, 200.0, 1e-9));
}

static void t_move_goal()
{
    g_jobs_mv.clear();
    const int64_t ch[3] = {1, 2, 3};
    const double loc[3] = {10.0, 20.0, 30.0}, pt[3] = {5.0, 6.0, 7.0};
    CHECK(move_goal_job("fleet", 7, 4, ch, loc, pt, 500, 1, nullptr) == 0);          // no job
    ScJob j;
    j.sid = 7;
    j.owner = "fleet";
    j.phase = 2;
    j.thr = 6;
    j.thr_t = 0.3;
    j.view_t = 0.05;
    j.v_close = 33.0;
    j.prev_dist_m = 120.0;
    j.q_log = 0.1;
    j.vmax_est = 90.0;
    j.mode = 1;
    g_jobs_mv[7] = j;
    CHECK(move_goal_job("other", 7, 4, ch, loc, pt, 500, 1, nullptr) == 0);          // other owner
    CHECK(move_goal_job("fleet", 7, 4, ch, loc, pt, 500, 1, nullptr) == 1);
    const ScJob& k = g_jobs_mv[7];
    CHECK(k.sys == 4 && k.ch[0] == 1 && k.ch[2] == 3 && k.loc[1] == 20.0 && k.pt_uu[2] == 7.0);   // goal rewritten
    CHECK(k.ff_cms == 500 && k.ff_valid == 1 && k.goal_jump);
    CHECK(k.phase == 2 && k.thr == 6 && k.thr_t == 0.3 && k.view_t == 0.05);                         // aim / throttle state kept
    CHECK(k.v_close == 33.0 && k.prev_dist_m == 120.0 && k.q_log == 0.1 && k.vmax_est == 90.0);
    CHECK(move_goal_job("fleet", 7, 4, nullptr, nullptr, nullptr, -100, 0, nullptr) == 1 && g_jobs_mv[7].ch[1] == 2 && g_jobs_mv[7].ff_valid == 0);   // null = unchanged
    g_jobs_mv.clear();
}

// API 8 A1: goal velocity extrapolation (sc_goal_extrap) and its job wiring (move_goal_job vel_ms).
static void t_goal_extrap()
{
    const double R = SM_RAW_PER_M;
    const double loc[3] = {1000.0, 2000.0, 3000.0};
    double out[3] = {0, 0, 0};
    const double vx[3] = {100.0, 0.0, 0.0}, vyz[3] = {0.0, -40.0, 10.0};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    sc_goal_extrap(loc, vx, 0.25, R, out);
    CHECK(NEARB(out[0], 1000.0 + 25.0 * R, 1e-6) && out[1] == 2000.0 && out[2] == 3000.0);       // 25 m along +X
    sc_goal_extrap(loc, vyz, 0.1, R, out);
    CHECK(NEARB(out[0], 1000.0, 1e-9) && NEARB(out[1], 2000.0 - 4.0 * R, 1e-3) && NEARB(out[2], 3000.0 + 1.0 * R, 1e-3));
    sc_goal_extrap(loc, vx, 0.0, R, out);
    CHECK(out[0] == loc[0] && out[1] == loc[1] && out[2] == loc[2]);                                 // age 0: unchanged
    sc_goal_extrap(loc, vx, -1.0, R, out);
    CHECK(out[0] == loc[0] && out[1] == loc[1] && out[2] == loc[2]);                                 // age < 0: unchanged
    sc_goal_extrap(loc, vx, 2.0, R, out);
    CHECK(NEARB(out[0], 1000.0 + 60.0 * R, 1e-6));                                                   // age clamped to 0.6 s
    const double vnan[3] = {nan, -40.0, 10.0};
    sc_goal_extrap(loc, vnan, 0.1, R, out);
    CHECK(out[0] == loc[0] && NEARB(out[1], 2000.0 - 4.0 * R, 1e-3));                                // NaN v.x: that term is 0
    sc_goal_extrap(loc, vx, nan, R, out);
    CHECK(out[0] == loc[0]);                                                                         // NaN age: 0

    // job wiring: vel set -> gv_on, age 0; a later tick ages it; vel null clears it
    g_jobs_mv.clear();
    const int64_t ch[3] = {0, 0, 0};
    ScJob j;
    j.sid = 9;
    j.owner = "fleet";
    j.mode = 1;
    g_jobs_mv[9] = j;
    CHECK(move_goal_job("fleet", 9, 4, ch, loc, nullptr, 0, 0, vx) == 1 && g_jobs_mv[9].gv_on && g_jobs_mv[9].gv_age == 0.0 && g_jobs_mv[9].gv[0] == 100.0);
    g_jobs_mv[9].gv_age = 0.3;
    CHECK(move_goal_job("fleet", 9, 4, ch, loc, nullptr, 0, 0, vx) == 1 && g_jobs_mv[9].gv_age == 0.0);
    CHECK(move_goal_job("fleet", 9, 4, ch, loc, nullptr, 0, 0, nullptr) == 1 && !g_jobs_mv[9].gv_on);
    g_jobs_mv.clear();

    // aim level: the mode-1 step extrapolates the goal and then runs pt_rloc_calc + aim_pt, so the aim equals the aim of the goal moved by 25 m
    ScFrame g;
    g.loc[0] = 1000; g.loc[1] = 2000; g.loc[2] = 3000;
    const int64_t c0[3] = {0, 0, 0};
    const double l0[3] = {0, 0, 0};
    const double tgt[3] = {655360.0, 0.0, 0.0};   // 10 m ahead, raw
    const double vy[3] = {0.0, 100.0, 0.0};
    double gl[3] = {0, 0, 0}, rl_a[3] = {0, 0, 0}, rl_b[3] = {0, 0, 0};
    sc_goal_extrap(tgt, vy, 0.25, R, gl);
    const double moved[3] = {655360.0, 25.0 * R, 0.0};
    CHECK(pt_rloc_calc(g, 5, c0, l0, 5, c0, gl, rl_a) && pt_rloc_calc(g, 5, c0, l0, 5, c0, moved, rl_b));
    ScAim aa, ab;
    CHECK(aim_pt(g, rl_a, aa) && aim_pt(g, rl_b, ab));
    CHECK(NEARB(aa.dist_m, ab.dist_m, 1e-9) && NEARB(aa.err_deg, ab.err_deg, 1e-9) && NEARB(aa.a_body[2], ab.a_body[2], 1e-12));
}

static void t_follow_smooth()
{
    // 1D follow sim: control tick 0.25 s, physics 0.05 s, speed follows thr*vmax/10 with a 0.8 s lag. Captain 40 m/s (0-30 s), stopped (30-60 s), 20 m/s (60-90 s). Starts 100 m behind.
    MatchState st;
    double cap_pos = 100.0, pos = 0.0, v = 0.0, vmax = 100.0, maxabs = 0.0, worst_dthr = 0.0, err_end = 0.0;
    int thr = 0, flips = 0, last_sign = 0, near_ticks = 0, mixed = 0, lo_ok = 0;
    double mint = 0.0;
    for (int i = 0; i < 360; ++i)
    {
        const double t = i * 0.25;
        const double vc = t < 30.0 ? 40.0 : (t < 60.0 ? 0.0 : 20.0);
        const double err = cap_pos - pos;
        const int prev = thr;
        thr = quick_match_step(st, 0.25, err, vc, v, vmax, 10, thr, 100, 10);   // v signed: reversing reads negative
        CHECK(thr >= -10 && thr <= 10);
        const bool settled = (t >= 15.0 && t < 30.0) || (t >= 45.0 && t < 60.0) || (t >= 75.0 && t < 90.0);   // away from the captain's speed changes
        if (std::fabs(err) < SQ_NEAR_M && settled)
        {
            ++near_ticks;
            worst_dthr = (std::max)(worst_dthr, static_cast<double>(std::abs(thr - prev)));
            const int sg = thr > 0 ? 1 : (thr < 0 ? -1 : 0);
            if (sg != 0) { if (last_sign != 0 && sg != last_sign) ++flips; last_sign = sg; }
            if (thr > 0 && thr < 10) ++mixed;
            if (thr == 0) ++lo_ok;
        }
        mint = (std::min)(mint, static_cast<double>(thr));
        if (t < 30.0 || t >= 60.0) maxabs = (std::min)(maxabs, err);   // the captain's hard stop overshoots by the ship's own stopping distance (physics), not judged
        for (int k = 0; k < 5; ++k)
        {
            v += (thr * vmax / 10.0 - v) * (0.05 / 0.8);
            pos += v * 0.05;
            cap_pos += vc * 0.05;
        }
        if (i == 119 || i == 239 || i == 359) { err_end = cap_pos - pos; CHECK(std::fabs(err_end) < 10.0); }   // converged in each captain phase
        #ifdef SC_SIM_DEBUG
        if (i % 8 == 0) std::printf("t=%.1f err=%.1f v=%.1f thr=%d\n", t, cap_pos - pos, v, thr);
        #endif
    }
    CHECK(near_ticks > 100);
    CHECK(flips < 2);                                                     // no sign chatter in the near zone
    CHECK(worst_dthr <= 1.0);                                             // at most one step per 0.25 s near the slot
    CHECK(maxabs > -2.0 * 10.0);                                          // never rams
    CHECK(mixed > 20 && lo_ok > 10);                                      // intermediate steps are used, not only 0 / 10
    // steering
    const double up[3] = {0.0, 0.0, 1.0};
    double yaw = 9.0, pit = 9.0;
    bool al = false;
    for (double e : {0.0, 1.5, 2.4})
    {
        view_offset_follow(e, up, 0.0, 0.0, 0.1, 30.0, yaw, pit, al);
        CHECK(yaw == 0.0 && pit == 0.0 && al);                            // deadband: exactly zero
    }
    view_offset_follow(10.0, up, 4.0, 0.0, 0.1, 30.0, yaw, pit, al);
    CHECK(!al && NEARB(yaw, 5.0, 1e-9) && NEARB(pit, 0.0, 1e-9));        // 0.5 deg per deg
    view_offset_follow(20.0, up, 0.0, 0.0, 0.1, 30.0, yaw, pit, al);
    CHECK(NEARB(yaw, 6.0, 1e-9));                                         // slew: 60 deg/s * 0.1 s
    view_offset_follow(20.0, up, 6.0, 0.0, 0.1, 30.0, yaw, pit, al);
    CHECK(NEARB(yaw, 10.0, 1e-9));                                        // proportional target 10 deg reached
    view_offset_follow(45.0, up, 0.0, 0.0, 0.1, 30.0, yaw, pit, al);
    CHECK(NEARB(yaw, 30.0, 1e-9));                                        // big error: hard turn, no slew
    // quick turn mapping
    view_offset_quick(1.9, up, 60.0, yaw, pit, al);
    CHECK(al && yaw == 0.0 && pit == 0.0);
    view_offset_quick(15.0, up, 60.0, yaw, pit, al);
    CHECK(!al && NEARB(yaw, 40.0, 1e-9) && NEARB(pit, 0.0, 1e-9));       // full cap 60 clamped to the 40 axis limit
    view_offset_quick(15.0, up, 30.0, yaw, pit, al);
    CHECK(NEARB(yaw, 30.0, 1e-9));
    view_offset_quick(6.0, up, 60.0, yaw, pit, al);
    CHECK(NEARB(yaw, 21.0, 1e-9));                                        // 6 * 3.5
    view_offset_quick(2.5, up, 60.0, yaw, pit, al);
    CHECK(NEARB(yaw, 10.0, 1e-9));                                        // min 10 (taper 15)
    view_offset_quick(1.99, up, 60.0, yaw, pit, al);
    CHECK(al);
    view_offset_quick(1.0, up, 60.0, yaw, pit, al, SM_TRACK_DEG); CHECK(!al && NEARB(yaw, 6.0, 1e-9));
    view_offset_quick(0.2, up, 60.0, yaw, pit, al, SM_TRACK_DEG); CHECK(al && yaw == 0.0 && pit == 0.0);
    view_offset_quick(1.0, up, 60.0, yaw, pit, al); CHECK(al);
    const double dg[3] = {0, 0, 0};
    view_offset_quick(47.0, dg, 60.0, yaw, pit, al);
    CHECK(!al && (yaw != 0.0 || pit != 0.0));                             // never an all-zero offset at a big error
    {   // vector scaled, direction kept: axis (0, -0.6, 0.8) -> yaw 0.8 mag, pit 0.6 mag
        const double ax[3] = {0.0, -0.6, 0.8};
        view_offset_quick(47.0, ax, 60.0, yaw, pit, al);
        CHECK(NEARB(yaw, 40.0, 1e-9) && NEARB(pit, 30.0, 1e-9));
        double y2 = 10.0, p2 = 80.0;
        view_clamp_axes(y2, p2);
        CHECK(NEARB(p2, 40.0, 1e-9) && NEARB(y2, 5.0, 1e-9));
    }
    for (double e = 0.0; e < 180.0; e += 0.5)                             // every axis always within the limit
        for (double ang = 0.0; ang < 6.3; ang += 0.3)
        {
            const double ax[3] = {0.0, -std::sin(ang), std::cos(ang)};
            view_offset_quick(e, ax, 60.0, yaw, pit, al);
            CHECK(std::fabs(yaw) <= SC_VIEW_AXIS_MAX_DEG + 1e-9 && std::fabs(pit) <= SC_VIEW_AXIS_MAX_DEG + 1e-9);
            if (e > SQ_TURN_NOZERO_ERR_DEG) CHECK(std::hypot(yaw, pit) >= SQ_TURN_NOZERO_MIN_DEG - 1e-9);
        }
    {   // 90 deg turn, first-order model rate = min(offset, 52), 0.1 s steps: new mapping beats old (min(err+7, 60)), overshoot < 3
        double tn = -1.0, to = -1.0, ovn = 0.0, ovo = 0.0;
        for (int mode = 0; mode < 2; ++mode)
        {
            double e = 90.0, t = 0.0, over = 0.0, t3 = -1.0;
            for (int i = 0; i < 600; ++i)
            {
                if (e < 3.0 && t3 < 0.0) t3 = t;
                double off = 0.0;
                if (e >= SM_FACE_DONE_DEG || e <= -SM_FACE_DONE_DEG)
                {
                    const double ae = std::fabs(e);
                    if (mode == 0) { view_offset_quick(ae, up, 60.0, yaw, pit, al); off = yaw; }
                    else off = (std::min)(ae + SM_VIEW_DEADZONE_DEG, 60.0);
                    off *= e > 0 ? 1.0 : -1.0;
                }
                const double rate = (std::min)(std::fabs(off), 52.0) * (off < 0 ? -1.0 : 1.0);
                e -= rate * 0.1;
                t += 0.1;
                if (e < 0.0) over = (std::max)(over, -e);
            }
            if (mode == 0) { tn = t3; ovn = over; } else { to = t3; ovo = over; }
        }
        CHECK(tn > 0.0 && to > 0.0 && tn < to);
        CHECK(ovn < 3.0);
        (void)ovo;
    }
}

static void t_approach_step()
{
    ApproachOut o = approach_step(5000.0, 0.0, 0.0, 10, false, 0, 0, 0.0, 0.25);
    CHECK(o.target == 10 && o.thr == 0 && NEARB(o.thr_t, 0.25, 1e-12));          // ramp not yet
    ApproachOut o2 = approach_step(5000.0, 0.0, 0.0, 10, false, 0, o.thr, o.thr_t, 0.25);
    CHECK(o2.thr == 1 && o2.thr_t == 0.0);                                       // 0.5 s: one step up
    o = approach_step(100.0, 0.0, 0.0, 10, false, 0, 5, 0.3, 0.25);
    CHECK(o.target == 2 && o.thr == 2 && o.thr_t == 0.0);                       // falls straight to target
    o = approach_step(0.0, 5.0, 0.0, 10, false, 0, 3, 0.2, 0.25);
    CHECK(o.target == -2 && o.thr == -2 && !o.done);                             // past stop, still moving: brake
    o = approach_step(-10.0, 5.0, 0.0, 10, false, 0, 3, 0.2, 0.25);
    CHECK(o.target == -2 && o.thr == -2 && !o.done);
    o = approach_step(0.0, 0.5, 0.0, 10, false, 0, -2, 0.0, 0.25);
    CHECK(o.done);                                                               // nearly stopped: done
    o = approach_step(5000.0, 0.0, 12.0, 10, false, 0, 5, 0.0, 0.25);
    CHECK(o.target == 0 && o.thr == 0);                                          // heading error > 10 deg
    o = approach_step(5000.0, 0.0, 0.0, 4, false, 0, 0, 0.5, 0.5);
    CHECK(o.target == 4 && o.thr == 1);                                          // steps cap
    int thr = 0;
    double tt = 0.0;
    for (int i = 0; i < 100; ++i)
    {
        o = approach_step(5000.0, 0.0, 0.0, 10, true, 3, thr, tt, 0.25);
        thr = o.thr;
        tt = o.thr_t;
        CHECK(o.target <= 3 && thr <= 3);                                        // low-power cap 3
    }
    CHECK(thr == 3);
    o = approach_step(5000.0, 0.0, 0.0, 10, false, 0, -2, 0.0, 0.25);
    CHECK(o.thr == 0);                                                           // pushed back out: restart from 0
}

static void t_view_and_ore()
{
    CHECK(!ore_refresh_due(150.0, false, 0.0));
    CHECK(ore_refresh_due(50.0, false, 0.0));
    CHECK(ore_refresh_due(50.0, true, 1.0));
    CHECK(!ore_refresh_due(50.0, true, 0.5));
    double yaw = -1.0, pit = -1.0;
    bool aligned = false;
    const double up[3] = {0, 0, 1}, dn[3] = {0, -1, 0}, zero[3] = {0, 0, 0};
    CHECK(view_offset(1.9, up, yaw, pit, aligned) && aligned && yaw == 0.0 && pit == 0.0);
    CHECK(view_offset(5.0, up, yaw, pit, aligned) && !aligned && NEARB(yaw, 12.0, 1e-9) && NEARB(pit, 0.0, 1e-9));
    CHECK(view_offset(5.0, dn, yaw, pit, aligned) && NEARB(yaw, 0.0, 1e-9) && NEARB(pit, 12.0, 1e-9));
    CHECK(view_offset(18.0, up, yaw, pit, aligned) && NEARB(yaw, 20.0, 1e-9));
    CHECK(view_offset(5.0, zero, yaw, pit, aligned) && yaw == 0.0 && pit == 0.0);
    const double ship[3] = {0, 0, 0}, tgt[3] = {300, 400, 0};
    NEAR(remain_dist_m(ship, tgt, 100.0), 400.0, 1e-9);
}

static void t_aim_and_rloc()
{
    ScFrame f;   // at the origin, nose +X, right +Y, up +Z
    ScAim r;
    const double ahead[3] = {1000, 0, 0}, behind[3] = {-1000, 0, 0}, right[3] = {0, 1000, 0}, above[3] = {0, 0, 1000}, here[3] = {0, 0, 0};
    CHECK(aim_pt(f, ahead, r) && NEARB(r.err_deg, 0.0, 1e-9) && NEARB(r.dist_m, 10.0, 1e-9));
    CHECK(aim_pt(f, behind, r) && NEARB(r.err_deg, 180.0, 1e-6));
    CHECK(aim_pt(f, right, r) && NEARB(r.err_deg, 90.0, 1e-6) && r.a_body[2] > 0.99);   // right: turn axis +U
    CHECK(aim_pt(f, above, r) && r.a_body[1] < -0.99);                                 // above: turn axis -R
    CHECK(!aim_pt(f, here, r));                                                        // point at the ship: degenerate

    ScFrame g;
    g.loc[0] = 1000; g.loc[1] = 2000; g.loc[2] = 3000;
    const int64_t c0[3] = {0, 0, 0};
    const double l0[3] = {0, 0, 0};
    const double tl[3] = {655360.0, 0, 0};   // 10 m raw
    double rl[3] = {0, 0, 0};
    CHECK(pt_rloc_calc(g, 5, c0, l0, 5, c0, tl, rl));
    NEAR(rl[0], 2000.0, 1e-9); NEAR(rl[1], 2000.0, 1e-9); NEAR(rl[2], 3000.0, 1e-9);   // same chunk: loc + 1000 uu
    const int64_t c1[3] = {1, 0, 0};
    const double z[3] = {0, 0, 0};
    CHECK(pt_rloc_calc(g, 5, c0, l0, 5, c1, z, rl));
    NEAR(rl[0], 1000.0 + 204800.0, 1e-6);                                                  // one chunk: + 2048 m
    CHECK(!pt_rloc_calc(g, 5, c0, l0, 6, c0, tl, rl));                                     // other system
}

static void t_qcal_and_frame()
{
    const double q[4] = {0.8660254037844386, 0.16666666666666666, 0.3333333333333333, 0.3333333333333333};   // unit, generic axis
    double fw[3], R[3], U[3];
    qcal_out(q, 0, 0, fw);
    qcal_out(q, 0, 2, R);
    qcal_out(q, 0, 4, U);
    int qm = -1, ax[3] = {-1, -1, -1};
    CHECK(qcal_pick(q, fw, R, U, qm, ax) && qm == 0 && ax[0] == 0 && ax[1] == 2 && ax[2] == 4);
    const double ident[4] = {1, 0, 0, 0}, X[3] = {1, 0, 0}, Y[3] = {0, 1, 0}, Z[3] = {0, 0, 1};
    CHECK(!qcal_pick(q, X, Y, Z, qm, ax));                                                  // axes not from this quat
    double a2[3], b2[3], c2[3];
    qcal_apply(q, 0, ax, a2, b2, c2);
    NEAR(dot3(a2, b2), 0.0, 1e-9); NEAR(norm3(a2), 1.0, 1e-9);
    NEAR(quat_angle_deg(ident, ident), 0.0, 1e-9);
    const double half = std::sqrt(0.5);
    const double quarter[4] = {half, 0, 0, half};   // 90 deg about Z
    NEAR(quat_angle_deg(ident, quarter), 90.0, 1e-6);

    MsFlight f;
    std::memset(&f, 0, sizeof(f));
    ScFrame out;
    std::wstring why;
    f.flags = FF_ACTOR_FRAME;
    f.act_loc[0] = 1; f.act_loc[1] = 2; f.act_loc[2] = 3;
    f.fw[0] = 0; f.fw[1] = 1; f.fw[2] = 0;
    f.R[0] = -1; f.R[1] = 0; f.R[2] = 0;
    f.U[2] = 1;
    CHECK(frame_from_flight(f, out, why) && out.loc[2] == 3.0 && out.fw[1] == 1.0 && out.R[0] == -1.0);   // actor frame copied
    std::memset(&f, 0, sizeof(f));
    f.flags = FF_OBJ_POSE;
    CHECK(!frame_from_flight(f, out, why) && why == L"ship frame unreadable");          // object pose, no calibration
    f.ch[0] = 1; f.ch[1] = 2; f.ch[2] = 3;
    f.loc_raw[0] = 65536.0 * 5; f.loc_raw[2] = 65536.0 * 7;
    f.quat[0] = 1.0;
    g_calib = ScCalib();
    g_calib.ok = true;
    CHECK(frame_from_flight(f, out, why) && NEARB(out.loc[0], 205300.0, 1e-6) && NEARB(out.loc[1], 409600.0, 1e-6) && NEARB(out.loc[2], 615100.0, 1e-6));
    g_calib = ScCalib();

    std::memset(&f, 0, sizeof(f));
    f.flags = FF_ACTOR_FRAME | FF_OBJ_POSE;
    f.quat[0] = q[0]; f.quat[1] = q[1]; f.quat[2] = q[2]; f.quat[3] = q[3];
    qcal_out(q, 0, 0, f.fw);
    qcal_out(q, 0, 2, f.R);
    qcal_out(q, 0, 4, f.U);
    CHECK(sc_calib_learn(f) && g_calib.ok && g_calib.qmode == 0 && g_calib.ax[0] == 0 && g_calib.ax[1] == 2 && g_calib.ax[2] == 4);
    CHECK(!sc_calib_learn(f));                                                              // learned once
    g_calib = ScCalib();
    f.flags = FF_OBJ_POSE;
    CHECK(!sc_calib_learn(f) && !g_calib.ok);                                               // no actor frame: cannot teach
}

// SC2b A2: move-job ABI sizes (also checked by static_asserts in sc_api.h) and the pure view raw conversion of sc_move.hpp.
static void t_move_abi()
{
    CHECK(SC_API_LEVEL == 8);
    CHECK(sizeof(ScHost) == 112 && SC_HOST_SIZE_L2 == 24 && SC_HOST_SIZE_L3 == 80);
    CHECK(sizeof(ScMoveReq) == 160 && SC_MOVEREQ_SIZE_L5 == 128 && SC_MOVEREQ_SIZE_L6 == 144 && sizeof(ScMoveStatus) == 64);
    CHECK(sc_view_raw(1.0) == 65536 && sc_view_raw(-0.5) == -32768 && sc_view_raw(0.0) == 0);   // llround of deg * 65536
    // SC3 A2 (API 4): level 4 structs
    CHECK(sizeof(ScBlockHit) == 56 && sizeof(ScBubbleProfile) == 72);
    CHECK(sizeof(ScBubbleEvent) == 88 && sizeof(ScBubbleStatus) == 56);
    CHECK(offsetof(ScHost, first_blocker) == 80 && offsetof(ScHost, near_rock) == 104);
}

// SC3 A2: ladder state helpers (sc_bubble.hpp) on a hand-built g_bub entry. No game needed.
static void t_bub_state()
{
    g_bub.clear();
    CHECK(!bub_braking(5));                                       // no state: not braking
    {
        ScBubbleStatus st{};
        st.size = static_cast<int32_t>(sizeof(ScBubbleStatus));
        bub_status_fill(5, st);
        CHECK(st.have == 0 && st.braking == 0 && st.blk == 0 && st.size == 56);
    }
    BubbleState& s = g_bub[5];
    s.braking = true; s.replans = 1; s.pending = BBA_REPLAN; s.wp = 2; s.resume = true;
    s.blk = 77; s.phase = BPH_SEEK; s.why = BW_ACTIVE; s.rock = 9; s.blk_d = 12.5;
    CHECK(bub_braking(5));
    CHECK(!bub_braking(6));                                       // another ship: not braking
    {
        ScBubbleStatus st{};
        st.size = static_cast<int32_t>(sizeof(ScBubbleStatus));
        bub_status_fill(5, st);
        CHECK(st.have == 1 && st.braking == 1 && st.replans == 1 && st.pending == BBA_REPLAN);
        CHECK(st.wp == 2 && st.resume == 1 && st.blk == 77 && st.phase == BPH_SEEK && st.why == BW_ACTIVE);
        CHECK(st.rock == 9 && NEARB(st.blk_d, 12.5, 1e-12) && st.size == 56);
    }
    g_bub[5].braking = false;
    CHECK(!bub_braking(5));
    bub_drop(5);
    CHECK(g_bub.count(5) == 0);
    g_bub.clear();
}

// SC3 A1: bubble ladder (sc_logic_bubble.hpp). Values from the legacy mb_logic.hpp / mb_bubble.hpp rules.
static void t_bubble()
{
    // ladder: clear -> hit brakes, pending REPLAN; stop -> REPLAN, count 1; third hit after 2 replans -> RETARGET, count reset at the stop
    {
        BubbleState s;
        CHECK(bubble_step(s, false, 10.0, 5) == BBA_NONE);                  // clear
        CHECK(s.rock == 5);
        CHECK(bubble_step(s, true, 10.0, 5) == BBA_BRAKE && s.braking && s.pending == BBA_REPLAN);
        CHECK(bubble_step(s, true, 5.0, 5) == BBA_NONE && s.braking);       // speed 5 while braking: nothing
        CHECK(bubble_step(s, true, 2.0, 5) == BBA_REPLAN && s.replans == 1 && !s.braking);
        CHECK(bubble_step(s, true, 10.0, 5) == BBA_BRAKE && s.pending == BBA_REPLAN);
        CHECK(bubble_step(s, true, 2.0, 5) == BBA_REPLAN && s.replans == 2);
        CHECK(bubble_step(s, true, 10.0, 5) == BBA_BRAKE && s.pending == BBA_RETARGET);
        CHECK(bubble_step(s, true, 2.0, 5) == BBA_RETARGET && s.replans == 0);
        // new rock id resets the state
        CHECK(bubble_step(s, true, 10.0, 5) == BBA_BRAKE);
        CHECK(bubble_step(s, false, 0.0, 9) == BBA_NONE && s.rock == 9 && s.replans == 0 && !s.braking);
    }
    // brake rule: ahead and closing only
    CHECK(!bubble_brake_ok(-0.1, 50.0, 0.0, false, 5.0));            // behind (cos <= 0)
    CHECK(!bubble_brake_ok(0.5, 50.0, 0.0, false, 0.0));             // first sight, not moving
    CHECK(bubble_brake_ok(0.5, 50.0, 0.0, false, 1.0));              // first sight, moving
    CHECK(bubble_brake_ok(0.5, 40.0, 50.0, true, 0.0));              // same blocker, closing
    CHECK(!bubble_brake_ok(0.5, 50.0, 50.0, true, 5.0));             // same blocker, same distance
    // activation: autopilot beats everything, then dock autopilot, travel, our steering or speed, else idle
    CHECK(bubble_why(true, true, true, true, 10.0) == BW_AUTOPILOT);
    CHECK(bubble_why(false, true, true, true, 10.0) == BW_DOCK_AUTOPILOT);
    CHECK(bubble_why(false, false, true, true, 10.0) == BW_TRAVEL);
    CHECK(bubble_why(false, false, false, true, 0.0) == BW_ACTIVE);
    CHECK(bubble_why(false, false, false, false, 5.0) == BW_ACTIVE);
    CHECK(bubble_why(false, false, false, false, 2.0) == BW_IDLE);
    // phase
    CHECK(bubble_phase(true, false, true, false) == BPH_ORE);
    CHECK(bubble_phase(true, true, false, false) == BPH_WAYPOINT);
    CHECK(bubble_phase(true, false, false, false) == BPH_SEEK);
    CHECK(bubble_phase(false, false, false, true) == BPH_MINE);
    CHECK(bubble_phase(false, false, false, false) == BPH_NONE);
    // fix table
    CHECK(bubble_fix(BPH_MINE, BBA_NONE, 0, true) == BF_NUDGE);
    CHECK(bubble_fix(BPH_MINE, BBA_NONE, 0, false) == BF_RESUME);
    CHECK(bubble_fix(BPH_SEEK, BBA_RETARGET, 0, false) == BF_SKIP);
    CHECK(bubble_fix(BPH_SEEK, BBA_REPLAN, 0, false) == BF_REPLAN);
    CHECK(bubble_fix(BPH_WAYPOINT, BBA_REPLAN, 0, false) == BF_WAYPOINT);
    CHECK(bubble_fix(BPH_WAYPOINT, BBA_REPLAN, 1, false) == BF_WAYPOINT);
    CHECK(bubble_fix(BPH_WAYPOINT, BBA_REPLAN, 2, false) == BF_SKIP);
    CHECK(bubble_fix(BPH_ORE, BBA_REPLAN, 0, false) == BF_WAYPOINT);
    CHECK(bubble_fix(BPH_ORE, BBA_REPLAN, 2, false) == BF_SKIP);
    // activation gate
    {
        BubbleState bs;
        CHECK(bubble_gate(bs, true, true, BW_ACTIVE, false) == BGT_SKIP && bs.why == 4);   // warp wait: why unchanged
        CHECK(bubble_gate(bs, false, false, BW_AUTOPILOT, false) == BGT_ERASE && bs.why == BW_AUTOPILOT);
        bs = BubbleState();
        bs.wp = 2;
        CHECK(bubble_gate(bs, false, false, BW_IDLE, false) == BGT_SKIP && bs.wp == 0);    // idle, not braking, no resume: wp reset
        bs.wp = 2; bs.resume = true;
        CHECK(bubble_gate(bs, false, false, BW_IDLE, false) == BGT_SKIP && bs.wp == 2);    // resuming: wp kept
        bs = BubbleState();
        bs.braking = true;
        CHECK(bubble_gate(bs, false, false, BW_IDLE, false) == BGT_RUN);                   // idle but braking: run
        bs = BubbleState();
        CHECK(bubble_gate(bs, false, true, BW_ACTIVE, true) == BGT_SKIP);                  // paused, not braking
        bs.braking = true;
        CHECK(bubble_gate(bs, false, true, BW_ACTIVE, true) == BGT_RUN);                   // braking always finishes
        bs = BubbleState();
        bs.wp = 2;
        CHECK(bubble_gate(bs, false, false, BW_ACTIVE, false) == BGT_RUN && bs.wp == 0);   // no job: wp reset
        bs.wp = 2;
        CHECK(bubble_gate(bs, false, true, BW_ACTIVE, false) == BGT_RUN && bs.wp == 2);    // job: wp kept
    }
    // resume step
    CHECK(bubble_resume_step(false, false, 10.0) == BRS_NONE);
    CHECK(bubble_resume_step(true, true, 10.0) == BRS_NONE);
    CHECK(bubble_resume_step(true, false, 30.0) == BRS_ARRIVED);
    CHECK(bubble_resume_step(true, false, 31.0) == BRS_FLY);
    // hit decision
    {
        BubbleState bs;
        const double p[3] = {0, 0, 0}, fw[3] = {1, 0, 0};
        double dn = -1.0;
        bs.prev_id = 7;
        CHECK(bubble_hit_decide(bs, 0, p, fw, p, 5.0, dn) == 0 && dn == 0.0 && bs.prev_id == 0);   // clear
        const double behind[3] = {-100, 0, 0}, ahead[3] = {100, 0, 0};
        CHECK(bubble_hit_decide(bs, 3, p, fw, behind, 5.0, dn) == 0 && bs.prev_id == 3 && bs.prev_d == dn);   // behind: no brake
        bs = BubbleState();
        CHECK(bubble_hit_decide(bs, 3, p, fw, ahead, 5.0, dn) == 3 && bs.prev_id == 3 && NEARB(bs.prev_d, 100.0, 1e-9) && NEARB(dn, 100.0, 1e-9));   // first sight, moving
        bs.prev_d = 120.0;
        CHECK(bubble_hit_decide(bs, 3, p, fw, ahead, 0.0, dn) == 3);                        // same blocker closing: keeps the id
        bs.prev_d = 100.0;
        CHECK(bubble_hit_decide(bs, 3, p, fw, ahead, 0.0, dn) == 0);                        // same distance: no brake
    }
    // pass: store the blocker while not braking, phase only then, wp reset on a new rock
    {
        BubbleState bs;
        const double c[3] = {1, 2, 3}, c2[3] = {9, 9, 9};
        CHECK(bubble_pass(bs, 4, BPH_SEEK, 12.5, c, 8.0, 77, 5.0) == BBA_BRAKE);
        CHECK(bs.blk == 4 && NEARB(bs.blk_d, 12.5, 1e-12) && bs.blk_c[0] == 1.0 && bs.blk_c[2] == 3.0 && NEARB(bs.blk_rr, 8.0, 1e-12) && bs.phase == BPH_SEEK);
        CHECK(bubble_pass(bs, 5, BPH_ORE, 1.0, c2, 2.0, 77, 5.0) == BBA_NONE);             // braking: blocker updated, phase kept
        CHECK(bs.blk == 5 && bs.phase == BPH_SEEK && bs.blk_c[0] == 9.0);
        bs.wp = 2;
        CHECK(bubble_pass(bs, 0, BPH_NONE, 0.0, c, 0.0, 88, 0.0) == BBA_NONE && bs.wp == 0 && bs.rock == 88);   // new rock
    }
    // plan: engine event per fix
    {
        BubblePlan p1 = bubble_plan(BPH_SEEK, BBA_REPLAN, 0, false, false, false);
        CHECK(p1.fix == BF_REPLAN && p1.ev == BEV_REPLAN);
        CHECK(bubble_plan(BPH_SEEK, BBA_RETARGET, 0, false, false, false).ev == BEV_SKIP);
        CHECK(bubble_plan(BPH_WAYPOINT, BBA_REPLAN, 0, false, false, true).ev == BEV_SKIP);   // no destination
        CHECK(bubble_plan(BPH_WAYPOINT, BBA_REPLAN, 0, false, true, false).ev == BEV_SKIP);   // no blocker geometry
        BubblePlan w = bubble_plan(BPH_WAYPOINT, BBA_REPLAN, 0, false, true, true);
        CHECK(w.fix == BF_WAYPOINT && w.ev == BEV_FLY && w.reason == BFR_WAYPOINT);
        BubblePlan n = bubble_plan(BPH_MINE, BBA_NONE, 0, true, false, false);
        CHECK(n.ev == BEV_FLY && n.reason == BFR_NUDGE);
        BubblePlan r = bubble_plan(BPH_MINE, BBA_NONE, 0, false, false, false);
        CHECK(r.fix == BF_RESUME && r.ev == BEV_NONE);
    }
    // nudge point: 100 m along p - nc; degenerate p == nc stays put
    {
        double wp[3];
        const double p[3] = {0, 0, 0}, nc[3] = {100, 0, 0};
        bubble_nudge_point(p, nc, wp);
        NEAR(wp[0], -100.0, 1e-9); NEAR(wp[1], 0.0, 1e-9); NEAR(wp[2], 0.0, 1e-9);
        const double nc2[3] = {0, 30, 40};
        bubble_nudge_point(p, nc2, wp);
        NEAR(wp[0], 0.0, 1e-9); NEAR(wp[1], -60.0, 1e-9); NEAR(wp[2], -80.0, 1e-9);
        bubble_nudge_point(p, p, wp);
        CHECK(std::isfinite(wp[0]) && std::isfinite(wp[1]) && std::isfinite(wp[2]));
        NEAR(wp[0], 0.0, 1e-9); NEAR(wp[1], 0.0, 1e-9); NEAR(wp[2], 0.0, 1e-9);
    }
    // fly point: chunk + raw loc of a flight target; border carry into the next chunk
    {
        const int64_t ch0[3] = {1, 2, 3};
        const double loc0[3] = {0, 0, 0}, ps[3] = {0, 0, 0}, wp[3] = {10, 0, 0};
        int64_t ch[3] = {0, 0, 0};
        double loc[3] = {0, 0, 0};
        bubble_fly_point(ps, wp, ch0, loc0, ch, loc);                       // 10 m = 655360 raw, same chunk
        CHECK(ch[0] == 1 && ch[1] == 2 && ch[2] == 3 && NEARB(loc[0], 655360.0, 1e-6));
        const double loc1[3] = {134217728.0 - 327680.0, 0, 0};             // 5 m short of the chunk border
        bubble_fly_point(ps, wp, ch0, loc1, ch, loc);                       // +10 m crosses it
        CHECK(ch[0] == 2 && ch[1] == 2 && NEARB(loc[0], 327680.0, 1e-6));
    }
    // ship radius from the game read
    CHECK(NEARB(ship_r_from_flight(true, 65536 * 8), 8.0, 1e-12));
    CHECK(NEARB(ship_r_from_flight(false, 65536 * 8), 12.0, 1e-12));
    CHECK(NEARB(ship_r_from_flight(true, 0), 12.0, 1e-12));
    CHECK(NEARB(ship_r_from_flight(true, -5), 12.0, 1e-12));
}

int main()
{
    std::string why;

    // free ship: first claim wins
    {
        ScOwners o;
        CHECK(o.owner_of(7).empty());
        CHECK(o.claim(7, "mining", why));
        CHECK(o.owner_of(7) == "mining");
    }
    // same owner again: ok, still held
    {
        ScOwners o;
        o.claim(7, "mining", why);
        CHECK(o.claim(7, "mining", why));
        CHECK(o.owner_of(7) == "mining");
    }
    // other owner: refused, reason names the holder, holder unchanged
    {
        ScOwners o;
        o.claim(7, "mining", why);
        why.clear();
        CHECK(!o.claim(7, "attack", why));
        CHECK(why == "owned by mining");
        CHECK(o.owner_of(7) == "mining");
    }
    // release by holder frees the ship; a second owner can then claim it
    {
        ScOwners o;
        o.claim(7, "mining", why);
        CHECK(o.release(7, "mining", why));
        CHECK(o.owner_of(7).empty());
        CHECK(o.claim(7, "attack", why));
    }
    // release by non-holder refused, ship keeps its owner
    {
        ScOwners o;
        o.claim(7, "mining", why);
        why.clear();
        CHECK(!o.release(7, "attack", why));
        CHECK(why == "owned by mining");
        CHECK(o.owner_of(7) == "mining");
    }
    // release of a free ship is a no-op success
    {
        ScOwners o;
        CHECK(o.release(9, "mining", why));
    }
    // release_all frees only that owner's ships and returns the count
    {
        ScOwners o;
        o.claim(1, "mining", why);
        o.claim(2, "mining", why);
        o.claim(3, "attack", why);
        CHECK(o.release_all("mining") == 2);
        CHECK(o.owner_of(1).empty());
        CHECK(o.owner_of(2).empty());
        CHECK(o.owner_of(3) == "attack");
        CHECK(o.release_all("mining") == 0);
    }

    t_warp_settle();
    t_hazard_dir();
    t_path_block_geom();
    t_path_waypoint_geom();
    t_path2_gates();
    t_tune_geom_path();

    // SC2a: speed judge (sc_speed.hpp, pure) and owner refusal for SC_zero / SC_watch (sc_owner.hpp may_act)
    {
        SpeedMon sp;
        double r = 0.0;
        CHECK(speed_judge(sp, r) == SJ_NONE);          // no valid speed yet
        sp.have = true; sp.v = 50.0; sp.lim = 100.0;
        CHECK(speed_judge(sp, r) == SJ_OK && r == 0.5);
        sp.v = 110.0;                                  // ratio 1.1 is not above the abort ratio
        CHECK(speed_judge(sp, r) == SJ_OK);
        sp.v = 120.0;                                  // first step above: report once
        CHECK(speed_judge(sp, r) == SJ_OVER_NEW && sp.aborted);
        CHECK(speed_judge(sp, r) == SJ_OVER);          // still over: no second report
        sp.v = 90.0;                                   // back under 1.0 re-arms the report
        CHECK(speed_judge(sp, r) == SJ_OK && !sp.aborted);
        sp.v = 130.0;
        CHECK(speed_judge(sp, r) == SJ_OVER_NEW);
        sp.v = 1100.0;                                 // ratio 11 > glitch ratio: sample dropped
        CHECK(speed_judge(sp, r) == SJ_GLITCH && !sp.have);
        sp.have = true; sp.lim = 0.0;                  // no limit: no decision
        CHECK(speed_judge(sp, r) == SJ_NONE);
    }
    {
        ScOwners o;
        CHECK(o.may_act(5, "mining", why));            // free ship: any owner may act
        o.claim(5, "attack", why);
        why.clear();
        CHECK(!o.may_act(5, "mining", why) && why == "owned by attack");
        CHECK(o.may_act(5, "attack", why));            // holder may act
    }

    {   // API 5: per-owner hosts, tune and tick filter
        ScHosts hs;
        ScHost a{}, b{};
        a.size = 111; b.size = 222;
        hs.set("mining", a);
        hs.set("fleet", b);
        CHECK(hs.of("mining").size == 111 && hs.of("fleet").size == 222);   // kept separate
        CHECK(hs.of("none").size == 0 && hs.of("none").log == nullptr);     // absent = zero host
        hs.set_speed_k("fleet", 7.0);
        CHECK(hs.speed_k("fleet", 60.0) == 7.0 && hs.speed_k("mining", 60.0) == 60.0);
        {   // of() returns a copy: a later set() does not change a host already taken
            const ScHost taken = hs.of("fleet");
            ScHost c{}; c.size = 333;
            hs.set("fleet", c);
            CHECK(taken.size == 222 && hs.of("fleet").size == 333);
        }
        // speed filter: watches of owner only
        g_spd.watch.clear();
        g_spd.watch[10] = "mining"; g_spd.watch[11] = "fleet";
        CHECK(speed_ids("fleet") == std::vector<int>({11}) && speed_ids("mining") == std::vector<int>({10}));
        // bubble filter: a watched-but-unclaimed ship is ticked by its watching owner and NOT by another
        {
            ScOwners ow;
            std::string why;
            ow.claim(12, "mining", why);                    // claimed, not watched, has ladder state
            g_bub.clear();
            g_bub[12] = BubbleState{};
            CHECK(bub_ids(ow, "fleet") == std::vector<int>({11}));          // 11 is unclaimed but watched by fleet
            CHECK(bub_ids(ow, "mining") == std::vector<int>({10, 12}));     // 10 watched by mining; 12 ladder state claimed by mining
            g_bub.clear();
        }
        g_spd.watch.clear();
        // key is the exact name (no case folding); dllmain owner_key (ASCII, non-empty) gates every _for export
        hs.set("mining", a); hs.set("fleet", b);
        CHECK(hs.of("Mining").size == 0);               // no case folding: the key is the exact name
        g_jobs_mv.clear();
        ScJob jm, jf;
        jm.owner = "mining"; jf.owner = "fleet";
        g_jobs_mv[1] = jm; g_jobs_mv[2] = jf;
        std::vector<Done> dn;
        finish_all(L"x", ScHost{}, dn, false, "fleet");   // finishes only fleet jobs
        CHECK(g_jobs_mv.count(1) == 1 && g_jobs_mv.count(2) == 0 && dn.size() == 1 && dn[0].sid == 2);
        g_jobs_mv.clear();
    }

    t_moved_steer();
    t_lp_state_from();
    t_power_dead_from();
    t_phase1_steer();
    t_approach_step();
    t_approach_step_quick();
    t_approach_step_fast();
    t_quick_match();
    t_move_goal();
    t_goal_extrap();
    t_follow_smooth();
    t_view_and_ore();
    t_aim_and_rloc();
    t_qcal_and_frame();
    t_move_abi();
    t_bubble();
    t_bub_state();

    std::printf("test_shipcontrol: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
