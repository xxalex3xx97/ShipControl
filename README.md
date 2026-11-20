# ShipControl

A shared ship control mod for *Main Sequence*. It moves ships, watches their speed and brakes them before rocks, and keeps track of which mod controls which ship so two mods never steer the same ship.

You do not use it directly. **Mining Unit (Mining Bench Mod) 0.3.0 and later needs it.** MSCore Manager installs it for you when you add or update Mining Unit.

## Install

1. Install or update **MSCore 0.8.13 or newer** with MSCore Manager.
2. In MSCore Manager click **Add mod...** and add `xxalex3xx97/MiningBenchMod`. ShipControl is installed automatically as a required mod.
3. Already have Mining Unit? Open MSCore Manager, click **Check**, then **Update selected**. A missing ShipControl is installed with the update.

If ShipControl is missing or too old, Mining Unit refuses to start with the message "ShipControl not installed or too old; update MSCore Manager".

## For mod authors

The interface is a plain C header, `sc_api.h`, append only. The API levels, exports and layouts are documented in the source repository: [mods/ShipControl/README.md](https://github.com/xxalex3xx97/MSMiningUnit/blob/main/mods/ShipControl/README.md).

## Releases

Each version is a GitHub release with a zip and a `manifest.txt` for MSCore Manager. See the [CHANGELOG](CHANGELOG.md).
