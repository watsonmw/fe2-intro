Frontier Elite II Intro Player
---

---
About
---

This is a RTG player for the Frontier intro, it's a full implementation of the Frontier Elite II renderer.
The 3d models and music are read from official Frontier exe.  You must already own Frontier or alternatively
send 5 pounds to Frontier Developments to purchase the shareware version.


---
Requirements
---

 - Amiga OS 3.x or AROS
 - RTG 640x360x8
 - 68040 or better (real 68040 is too slow)
 - 8MB RAM or more

Tested on FS-UAE / WinUAE and on Vampire V4, reported to work on V1200 by Dr Procton!

Frontier Elite II exe must exist in same directory as 'fintro'.  Use the exe from the CD32 version or the final
Elite Club shareware version.  This can be downloaded from here:

  https://www.sharoma.com/frontierverse/files/game/fe2_amiga_cd32.rar

A 640 wide RTG resolution must be available.  The closest RTG resolution of 640 width and 8-bit palette (CLUT)
will be used.  640x360 is 16:9 and looks good.


---
Controls
---

  `          - Display frame rate / other stats
  Space      - Pause/Play
  Arrow Keys - Back/Forward frames, press shift to speed up
  Escape     - Exit
  Left Mouse - Exit

Somewhat OS friendly, multitasking is still enabled.  But stop any music players before starting.


---
Command Line Options
---

No music:

   fintro -mod 0


Any track from main game can be played (0 - 10):

   fintro -mod 1


---
Bugs
---

 - Sound issues
 - A bit slow



---
FAQ
---

Q. Could this be patched into original exe?
A. Yes, it would be work, but the original exe had jump tables for the renderer
that this code re-implements.

Q. Why?
A. DooM is ported to pretty much every platform and widely acclaimed with little black
books written on it and everything.  I think Frontier is an equal, if not greater,
achievement in many ways and deserves the same!  I wanted to do a portable version of
at least the renderer, and when I heard of the Vampire accelerator it seemed an ideal
platform to target.


---
Feedback
---

Either to github.com/watsonmw/fintro or directly to watsonmw@gmail.com
