# Changelog

## 0.6.0

- API 7: `ScMoveReq` grows to 160 bytes: `ff_cms` (off 144, captain speed along the line ship -> goal, cm/s, signed), `follow` (off 148), `ff_valid` (off 152), `pad` (off 156). `SC_move_start` accepts 128, 144 and 160 byte structs (missing fields are zero: old behaviour).
- Follow mode (quick profile, `follow` = 1 and `ff_valid` = 1): the job never finishes by itself. It matches the captain speed in the slot with normal throttle only (feed-forward + P on the slot error + speed-mismatch damping, hard brake on overshoot, brake hysteresis). Fleet cancels / restarts the job.
- Per-ship max speed estimate (starts from the game limit, learned from steady throttle / speed, kept per ship id).
- Log every 0.25 s: `ShipControl match ship=.. v_cap=.. v_own=.. thr=.. err=.. vmax_est=..`.
- New export `SC_move_goal(owner, sid, sys, ch, loc, pt_uu, ff_cms, ff_valid)`: rewrites the goal and the follow feed-forward of the running job in place (no phase / throttle / aim / speed reset; one closing-speed sample is skipped). Returns 0 when there is no job (the caller then uses `SC_move_start`). Replaces cancel + restart, which stopped the turn every time.
- Logs: `ShipControl turn ship=.. want=.. yaw=.. pit=.. got=.. ph=.. t=..` every 0.5 s; the quick / match lines now also print in phase 1.
- Smooth follow near the slot (`quick_match_step`): above 80 m the plain quick controller; below it an EMA-smoothed proportional throttle (any step -10..10); below 40 m the throttle moves 1 step per 0.25 s (0.5 s in the 3 m dead zone, pure feed-forward there) and a thr sign change waits 0.5 s; reverse only past the slot (hard only to kill speed, else at most -2). Follow steering (`view_offset_follow`): 2.5 deg dead zone, 0.5 deg offset per deg of error, 60 deg/s slew limit; errors above 30 deg keep the hard turn.
- All ShipControl trace lines (`match`, `quick`, `turn`) start with `PROBE ` so the Fleet log filter keeps them. The match line also prints `sign_flips_per_s` (thr sign changes in the last 4 s / 4).
- Quick drive (non-follow) is back to miner-style discrete speed steps: a decision every 0.1 s (`approach_step_fast`), far = full, brakes early (2 s lead), near = low steps, no ramp-up wait. `brake_pct` is ignored. The follow-mode matcher (`quick_match_*`) is kept but unused (Fleet sends `follow` = 0). Careful profile unchanged.
- Not done: heading match in the slot (no aim-at-direction mode exists yet).

## 0.5.0

- API 6: `ScMoveReq` grows from 128 to 144 bytes: `profile` (0 careful = unchanged default, 1 quick), `brake_pct`, `turn_pct`, `overshoot_m`. `SC_move_start` still accepts the 128-byte level 5 struct (careful profile).
- Quick profile for escorts / defenders / attackers: the throttle jumps straight between steps (no ramp), hard brake (-10) when the closing speed is above what the remaining distance allows, full throttle to catch up, wider aim offset (`turn_pct`, default 150% of 20 deg), approach starts at a heading error below 30 deg. Closing speed comes from the distance change (EMA).
- Log line every 0.25 s while quick: `ShipControl quick ship=.. thr=.. target=.. v_close=.. remaining=..`.
- Constant `SQ_TAU_S` (assumed seconds to stop from max speed) is an estimate: calibrate in game.

## 0.4.0

- API 5: owner-scoped host/tune/ticks (two mods can use ShipControl together); API 4 exports unchanged (owner mining).
- New exports: `SC_set_host_for`, `SC_set_tune_for`, `SC_tick_speed_for`, `SC_tick_frame_for`, `SC_tick_bubble_for`. `SC_api()` returns 5.
- **Breaking for legacy clients:** the API 4 exports (`SC_set_host`, `SC_set_tune`, `SC_tick_speed`, `SC_tick_frame`, `SC_tick_bubble`) now apply to owner `mining` ONLY. A legacy client that uses another owner name must move to the `_for` exports. Fleet 0.5.0 and newer requires API 5.
- `SC_set_tune_for` refuses a speed_k that is NaN, infinite or <= 0 (the previous value is kept). The bubble tick selects ships by the owner that watches them (claimed or not).
- Each owner has its own host and speed_k; a `_for` tick only steps that owner's jobs and ships.

## 0.3.1

- Same behaviour as 0.3.0. No code or API change.
- Source now lives in this repository, with its history (imported from MSMiningUnit `mods/ShipControl`).
- Version bump. Header comments in `sc_logic_path.hpp` are neutral: MiningBenchMod and Fleet both include it.
- Tests moved here (`tests/`).

## 0.3.0

- First release.
- **Owner registry:** one mod controls a ship at a time; a second mod's control call is refused.
- **Speed monitor:** watches ship speed and stops a ship that runs far above its limit.
- **Move jobs and steering:** flies a ship to a rock or a point (aim, thrust, brake), including the wait after a far warp.
- **Bubble brake:** brakes a ship before a rock or another ship's no-go zone and asks the calling mod to replan.
- Needs MSCore 0.8.13 or newer. Run in game with Mining Unit 0.3.0 (miners approach, mine, reroute and brake as before). Not yet run in game: a far warp with its landing wait.
