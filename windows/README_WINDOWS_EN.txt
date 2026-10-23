================================================================
  AUTOQ 1.16 for Windows 10/11 (64-bit) — guide
  created by Qisit
================================================================

Nothing else to install: everything is built into a single
AutoQ.exe (no DLLs). The interface language follows Windows;
change it in Settings → General → Language.


----------------------------------------------------------------
OPTION 1. Just the exe (portable, no installation)
----------------------------------------------------------------
1. Unzip AutoQ-Windows-Portable.zip into any folder
   (for example C:\Games\AutoQ). Don't run it from inside the zip.
2. Double-click AutoQ.exe.
3. When Windows asks "Allow this app to make changes?" — Yes.
The AutoQ-data folder next to AutoQ.exe holds all settings and
profiles (portable mode: carry the folder on a USB stick). Delete
AutoQ-data to keep settings in %APPDATA%\AutoQ instead.

To remove: turn off "Autostart" (Settings → General), quit from
the tray icon (right click → Quit), delete the folder.


----------------------------------------------------------------
"WINDOWS PROTECTED YOUR PC"
----------------------------------------------------------------
Windows shows this for any new program without "reputation".
It's not a virus: "More info" → "Run anyway".
If an antivirus removed AutoQ.exe, it's a false positive
(autoclickers sometimes get flagged). Restore the file from
quarantine and report it to Microsoft:
microsoft.com/wdsi/filesubmission


----------------------------------------------------------------
OPTION 2. Installer AutoQ-Setup.exe
----------------------------------------------------------------
1. Run it, pick a language, Next → folder → desktop shortcut →
   Install. Leave "Run AutoQ" checked at the last step.
2. Remove: Settings → Apps → AutoQ → Uninstall.


----------------------------------------------------------------
HOW TO USE
----------------------------------------------------------------
1. Add a game: pick a ready-made one (Genshin, Minecraft…) or
   your own — "Pick" next to "process" lists the open windows.
2. Choose actions — as many as you like (mouse button incl.
   Mouse 4/5, or any key) and the speed (ms, CPS or CPM). One
   action: Repeat or Hold. More: In turn, Together, Hold or Step by
   step. Above each key is its mode (click it for the choices):
   Click or Hold. The "hold" field next to the key is always there:
   0 ms is a click, more holds that long. A step is Press ↓, Release ↑
   or Click ↕. Pauses between keys can be 0 ms; a whole cycle is
   still no faster than the game's minimum. The mouse wheel changes
   a number, list or slider only after you click it — otherwise it
   scrolls the page.
3. Press the game's start/stop key (F6 by default; each game has
   its own, a mouse button works too; "On / off" or "While held")
   right in the game: AutoQ picks that window and clicks only there.
   A letter or mouse button also reaches the game, unless you turn
   on "Don't pass the press to the game" under the key.
   The circle next to a game: filled — the game is in use, its key
   works even while another game is open (several games can run
   at once); empty — not in use, it can't be started until you
   click the circle. Right-click a game for start/stop, keys, edit.
   "In sync with the monitor" puts each click into its own frame; the "?"
   badge explains why CPS then can't exceed the refresh rate.
4. CPS overlay: the gauge button → Off / Only while running /
   Always. Several running games share one panel, a row per game.
   Its look is in Settings → CPS overlay.
5. Profiles keep sets of settings per game: New / Save / Reset;
   a profile key switches to it right in the game. ⋯ → Export /
   Import a profile file. Start/stop sounds can differ per game and
   profile; "Add your sound…" takes any WAV ("My sounds").
6. Cards: the arrow at the top right collapses a card into a small
   tile. Edit mode (right-click a card → "Arrange cards…", or
   Settings → Appearance → Cards): drag cards by the title, blocks
   by any spot, even into another card; "New card" makes an empty
   named card for your blocks. Each game keeps its own layout.
   In edit mode, click a small element (step, key, pause chip, mode
   button, profile buttons, Start, hints…) to change its look,
   color, corner radius, size or hide it — for all such elements
   at once ("Elements" in the editor bar does the same).
   My styles (Settings → Appearance, or "Styles" in the editor):
   save the whole look under a name, optionally with the card
   layout; click a style to apply it; ⋯ → "Save to file…" makes an
   .aqstyle file to share, "From file…" adds someone else's.
7. First launch asks for an interface style (Studio / Glass /
   Terminal) and animations — redo it in Settings → General.
   Settings → Appearance also has the interface editor and size.

Settings, games, profiles and styles are plain files in %APPDATA%\AutoQ
(portable: AutoQ-data next to the exe) — change the folder in
Settings → General → Data; copy it to move AutoQ to another PC.
Settings from older versions (registry) are moved over by itself.

Autostart is the "AutoQ" entry in Task Manager → Startup apps (you
can turn it off there too); a Task Scheduler task starts AutoQ with
administrator rights without a UAC prompt.

The clicker pauses by itself when you switch to another window
and, for mouse clicks, when the cursor leaves the game window
(this can be turned off in Settings → General).
