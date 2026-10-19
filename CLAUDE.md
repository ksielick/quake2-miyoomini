# quake2-miyoomini

Yamagi Quake II for the Miyoo Mini Plus (OnionOS). Private repo ksielick/quake2-miyoomini.
Upstream: github.com/yquake2/yquake2 (remote `upstream`); our work is on branch `miyoo`.
Commit as 153522713+ksielick@users.noreply.github.com (already set in the repo config).

Read `miyoo/NOTES.md` first (device facts, why the MiyooCFW port doesn't work, plan); keep it
updated after each step.

- The owner's rules: Onion's Ports list only (never `App/`); no game data in the repo or
  releases (the player copies their own `baseq2/`); a PC backup before changing the card.
- Device tools (`mssh`/`mscp`/`mshot.sh`, SMB share `\\192.168.0.246\Roms`):
  `G:\Emulacja\redriver2-miyoo\repos\REDRIVER2\miyoo\tools\`. Toolchain:
  `G:\Emulacja\redriver2-miyoo\deps\arm-gnu-toolchain-13.3.rel1-*`.
- Repo files in English; chat with the owner in Polish.
