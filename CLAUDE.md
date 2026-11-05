# quake2-miyoomini

Yamagi Quake II for the Miyoo Mini Plus (OnionOS). Repo ksielick/quake2-miyoomini.
Upstream: github.com/yquake2/yquake2 (remote `upstream`); our work is on branch `miyoo`.
Commit as 153522713+ksielick@users.noreply.github.com (already set in the repo config).

Read `miyoo/NOTES.md` first (device facts, why the MiyooCFW port doesn't work, plan); keep it
updated after each step.

- The owner's rules: Onion's Ports list only (never `App/`); no game data in the repo or
  releases (the player copies their own `baseq2/`); a PC backup before changing the card.
- Shared tools: `G:\Emulacja\tools-miyoo` (private repo ksielick/tools-miyoo):
  - `device/` holds `mssh`, `mscp` and `mshot.sh` (SMB share `\\<miyoo-ip>\Roms`).
  - `analysis/` holds `shots_compare.py`, `frametimes.py` and `prof_map.py`.
  - The game's own scripts (bench.sh, bench_ab.sh, q2step_*.sh) stay in `miyoo/tools/`.
- Toolchain:
  `G:\Emulacja\redriver2-miyoo\deps\arm-gnu-toolchain-13.3.rel1-*`.
- Repo files in English; chat with the owner in Polish.
- Shared Miyoo knowledge for all projects: the `miyoo` skill (`~/.claude/skills/miyoo/SKILL.md`: owner rules, device facts, shared tools) and `G:\Emulacja\miyoo.md` (the full measured notes). Add cross-project findings there.
