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
- Shared Miyoo knowledge for all projects: the `handheld-ports` skill (`~/.claude/skills/handheld-ports/SKILL.md`: owner rules, device facts, shared tools) and its `notes/` folder (the full measured notes, the former `G:\Emulacja\miyoo.md`). Add cross-project findings there.

## Keeping the rule files small
- Rule files (`AGENTS.md`, `.agents/rules/*.md`, the skill's `SKILL.md`) stay under 24,000 bytes (aim for 23,500: CRLF adds bytes); Antigravity truncates anything larger. Each project has one rule file, `AGENTS.md`, read by every agent; `CLAUDE.md` holds only the line `@AGENTS.md`, and there is no `GEMINI.md`. New detailed rules and lessons go into the right file of the `handheld-ports` skill (`notes/` or `ports/`), not into a rule file. Edit the skill only in `~/.claude/skills/handheld-ports`, then `git commit` in that folder: the commit refreshes Gemini's read-only copies (`~/.gemini/skills`, `~/.gemini/config/skills`).
