================================================================
  AUTOQ 1.12 for Windows 10/11 (64-bit) — guide
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
   Mouse 4/5, or any key; "hold" ms for each) and the speed
   (ms, CPS or CPM). One action: Repeat or Hold. More: In turn,
   Together or Hold.
3. Press the game's start/stop key (F6 by default; each game has
   its own, a mouse button works too; "On / off" or "While held")
   right in the game: AutoQ picks that window and clicks only there.
   The circle next to a game in the list makes it "active": its
   key works even while another game is open — several games can
   run at once. Right-click a game for start/stop, keys, edit.
4. CPS overlay: the gauge button → Off / Only while running /
   Always. Its look is in Settings → CPS overlay.
5. Profiles keep sets of settings per game: New / Save / Reset;
   a profile key switches to it right in the game.
6. First launch asks for an interface style (Studio / Glass /
   Terminal) and animations — redo it in Settings → General.
   Settings → Appearance also has the interface editor and size.

Autostart is the "AutoQ" entry in Task Manager → Startup apps (you
can turn it off there too); a Task Scheduler task starts AutoQ with
administrator rights without a UAC prompt.

The clicker pauses by itself when you switch to another window
and, for mouse clicks, when the cursor leaves the game window
(this can be turned off in Settings → General).
