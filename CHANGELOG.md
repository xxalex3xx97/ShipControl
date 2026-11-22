# Changelog

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
