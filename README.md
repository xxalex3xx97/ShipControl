# ShipControl

Releases (zip + manifest.txt) are published on this repo's Releases page.

Version **0.4.0** (`sc_version.hpp`), API level **5** (`sc_api.h`, `SC_API_LEVEL`). A UE4SS mod that owns ship control for other mods (MiningBenchMod is the first user). It exports a plain C ABI; other mods load `main.dll` and resolve the names with `GetProcAddress`. Needs MSCore 0.8.13 at runtime (flight reads and writes go through MSCore).

The header `sc_api.h` is the contract. Append only: an export or a field is never changed or reordered.

## How to use

### For players

ShipControl is a dependency, not a feature. It has no window, no hotkey and no settings. Install it because another mod (MiningBenchMod, Fleet) needs it.

1. Open MSCore Manager.
2. Install **ShipControl** (and **MSCore** 0.8.13 or newer, which it needs).
3. Restart the game.

The manager lists it in the Installed column. Update it from the manager too.

| Message you see in the mod that needs it | Meaning | Fix |
|---|---|---|
| `error: ShipControl not installed or too old (needs API 4); update MSCore Manager` | ShipControl is missing, or its API level or capability bits are too low | Install or update ShipControl in MSCore Manager, restart |
| `owned by <name>` | another mod already controls that ship (one owner per ship; the host mod may show it with its own prefix) | Stop the other mod's job on that ship, or use another ship |
| `error: ShipControl killed` / `ShipControl killed` | the kill switch is on; every control call is refused | A mod or tool called `SC_kill(1)`. Restart the game, or the mod that set it must call `SC_kill(0)` |
| `error: MSCore flight unavailable` | MSCore is missing or older than 0.8.13 | Update MSCore in MSCore Manager |
| `error: move job already running` | that ship already has a move job | Wait for it or cancel it |

### For mod authors

Plain C ABI, header `sc_api.h` (copy it into your mod). The DLL is `Mods/ShipControl/dlls/main.dll`. Flow: load, check level and caps, install your host, claim, start a job, tick every frame, handle the callback, release.

```cpp
#include "sc_api.h"
#include <windows.h>

// 1. load the DLL and resolve exports (every UE4SS mod DLL is called main.dll: use the full path)
//    <game>\Binaries\Win64\ue4ss\Mods\ShipControl\dlls\main.dll
HMODULE h = LoadLibraryW(shipControlDllPath.c_str());   // already loaded by UE4SS: returns the same module
auto SC_api_f   = (int(*)())                                  GetProcAddress(h, "SC_api");
auto SC_caps_f  = (int(*)())                                  GetProcAddress(h, "SC_caps");
auto SC_set_host_for_f = (int(*)(const char*, const ScHost*)) GetProcAddress(h, "SC_set_host_for");
auto SC_claim_f = (int(*)(const wchar_t*, int, wchar_t*, int)) GetProcAddress(h, "SC_claim");
auto SC_move_start_f = (int(*)(const char*, int, ScMoveReq*, wchar_t*, int)) GetProcAddress(h, "SC_move_start");
auto SC_tick_frame_for_f = (int(*)(const char*, double))      GetProcAddress(h, "SC_tick_frame_for");
auto SC_release_f = (int(*)(const wchar_t*, int, wchar_t*, int)) GetProcAddress(h, "SC_release");

// 2. check level and capability bits; never use a feature whose bit is clear
if (!SC_api_f || SC_api_f() < 5) return;                 // owner-scoped exports need API 5
if (!(SC_caps_f() & 4)) return;                          // bit 2 (value 4) = move jobs

// 3. install your callbacks (size = sizeof(ScHost) = 112)
static void on_done(int sid, int seq, const wchar_t* result) { /* result "ok ..." or "error: ..." */ }
ScHost host{};
host.size = sizeof(ScHost);
host.on_move_done = on_done;      // null move callbacks fail closed
SC_set_host_for_f("mymod", &host);

// 4. claim, move, tick, release
wchar_t err[256] = {};
if (!SC_claim_f(L"mymod", shipId, err, 256)) { /* err = "owned by <name>" */ return; }

ScMoveReq req{};
req.size = sizeof(ScMoveReq);     // 128
req.kind = 0;                     // 0 face, 1 approach
req.seq  = 1;                     // returned in on_move_done
req.mode = 0;                     // fixed world point pt_uu (uu)
req.pt_uu[0] = x; req.pt_uu[1] = y; req.pt_uu[2] = z;
if (!SC_move_start_f("mymod", shipId, &req, err, 256)) { /* err = "error: ..." */ }

// every game tick: step your own jobs; -1 = killed or MSCore unusable (jobs already ended, on_move_done called)
SC_tick_frame_for_f("mymod", dt);

// when finished, or on your unload (SC_release_all(owner) frees all of them)
SC_release_f(L"mymod", shipId, err, 256);
```

Rules:

| Rule | Detail |
|---|---|
| Owner name | wide string in `SC_claim`, `SC_release`, `SC_release_all`; narrow string in every other export (`SC_move_start`, `SC_set_host_for`, `SC_*_for`, `SC_watch`, `SC_zero`) |
| Claim first | `SC_move_start`, `SC_watch`, `SC_zero` and the bubble ladder act only on a ship you own |
| Sizes | `ScHost` 24, 80 or 112; `ScMoveReq` 128; `ScMoveStatus` 64; `ScTune` `sizeof(ScTune)`; `ScSpeed` `sizeof(ScSpeed)`; `ScBubbleStatus` 56. Set `size` first; a wrong size is refused |
| Callbacks | run on the game thread, may call `SC_*`, must never throw |
| Legacy exports | `SC_set_host`, `SC_set_tune`, `SC_tick_*` without `_for` act for owner `mining` only. Use the `_for` exports with your own owner name |
| Poll | `SC_move_active(sid)`, `SC_move_status(sid, &st)` (set `st.size = sizeof(ScMoveStatus)`), `SC_move_cancel(owner, sid)` |

Kill switch: `SC_kill(1, err, n)` refuses every control export and ticks return -1 until `SC_kill(0, err, n)`. Check the return of every tick and stop your own loop on -1.

## Changes

- 0.4.0: API 5, owner-scoped host/tune/ticks (two mods can use ShipControl together). API 4 exports unchanged (they act for owner `mining` ONLY: a legacy client that uses another owner name must move to the `_for` exports; Fleet 0.5.0+ requires API 5).
- 0.3.1: no code or API change. The version is bumped because the ShipControl source moves to its own repository (history kept). The header comments in `sc_logic_path.hpp` no longer name a single mod, since MiningBenchMod and Fleet both include it. A mod that requires ShipControl@0.3.0 is still satisfied.

## API levels

| Level | Adds | Needs |
|---|---|---|
| 1 | owner registry: `SC_api`, `SC_caps`, `SC_kill`, `SC_claim`, `SC_owner`, `SC_release`, `SC_release_all` | nothing |
| 2 | speed monitor: `SC_set_host`, `SC_set_tune`, `SC_watch`, `SC_tick_speed`, `SC_speed`, `SC_zero`. ScHost 24 bytes | MSCore API >= 10 (0.8.13) |
| 3 | move jobs: `SC_move_start`, `SC_move_cancel`, `SC_move_active`, `SC_move_status`, `SC_tick_frame`. ScHost 80 bytes | MSCore flight |
| 4 | bubble ladder: `SC_tick_bubble`, `SC_bubble_status`. ScHost 112 bytes (4 callbacks: `first_blocker`, `bubble_profile`, `on_bubble`, `near_rock`) | MSCore flight |
| 5 | owner-scoped: `SC_set_host_for`, `SC_set_tune_for`, `SC_tick_speed_for`, `SC_tick_frame_for`, `SC_tick_bubble_for` (owner = `mining`, `fleet`, ...). Legacy `SC_set_host`, `SC_set_tune`, `SC_tick_*` = the `_for` call with owner `mining`. A `_for` tick steps only that owner's jobs and ships and uses that owner's host and speed_k | same as 4 |

Every export returns `int`, takes `wchar_t* err, int errlen` where it can refuse, and catches its own exceptions (a failure is a return code, never a throw).

## Capability bits (`SC_caps`)

| Bit | Value | Meaning |
|---|---|---|
| 0 | 1 | owner registry |
| 1 | 2 | speed monitor (MSCore usable) |
| 2 | 4 | move jobs (API 3) |
| 3 | 8 | bubble ladder (API 4) |

Callers check the bit they need. A caller that sees a bit clear must not use that feature.

## ScHost layout (`SC_set_host`)

The host is the mod's table of callbacks, filled by the caller. Offsets on x64:

- 0 `size` (int32)
- 8 `log`, 16 `on_speed_abort`, 24 `tlog`, 32 `on_move_done`, 40 `warp_begin`, 48 `remain_m`, 56 `target_ok`, 64 `ore_loc`, 72 `braking` (level 3 ends at 80)
- 80 `first_blocker`, 88 `bubble_profile`, 96 `on_bubble`, 104 `near_rock` (level 4, size 112)

`SC_set_host` accepts size 24, 80 or `sizeof(ScHost)` (112) and refuses others. It zeroes its copy and copies `min(size, sizeof)`, so a level 2 or 3 caller gets the older set. Callbacks run on the game thread inside the registry lock; they may call `SC_*`, never throw. `ScTune` has an exact size check (`size == sizeof(ScTune)`).

Structs of level 4: `ScBlockHit` (56), `ScBubbleProfile` (72), `ScBubbleEvent` (88), `ScBubbleStatus` (56). Their offsets are checked by `static_assert` in `sc_api.h`.

## Owner model

- One owner per ship. `SC_claim(owner, ship)`: 1 = owned now (free, or already this owner), 0 = refused (err names the owner).
- `SC_owner(ship)` reads the owner. `SC_release(owner, ship)` frees a ship only for its owner. `SC_release_all(owner)` frees every ship of that owner and returns the count.
- Speed watch, zero, move jobs and the bubble ladder are per ship and need the claim first (the owner name is checked against the ship's owner).
- `SC_kill(1)` refuses every control export until `SC_kill(0)`. A killed ShipControl makes every tick return -1, and its callers end their jobs.

## How mining uses it

MiningBenchMod 0.3.0 (requires ShipControl 0.3.0, MSCore 0.8.13):

- Start and Collect (sweep) claim the ship for owner `mining` (`mb_shipctl.hpp`, `sc_claim_ship`). No claim, no loop: refused with `error: ShipControl not installed or too old (needs API 4); update MSCore Manager` when ShipControl or the caps bits are missing, or `error: ship <err>` when another owner holds the ship.
- Every move job (approach a rock, a point, floating ore) is started with `SC_move_start`. ShipControl builds the job, steps the throttle and aim, and calls back `on_move_done`.
- The speed guard runs in `SC_tick_speed`; `SC_zero` stops a ship for mining.
- The bubble ladder (a rock on the nose: brake, replan, waypoint, nudge, resume) runs in `SC_tick_bubble`. Mining answers the ladder through `first_blocker` (registry query, ship spheres), `bubble_profile` (job state), `on_bubble` (replan, skip, fly) and `near_rock`.
- Stop or a finished loop releases the claim (`SC_release`). Mod unload calls `SC_release_all`.

Mining keeps the game data (rock registry, jobs, docks, cargo). ShipControl keeps the ship control.
