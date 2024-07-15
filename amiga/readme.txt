Frontier: Elite II Intro Player
---

---
About
---

This is a RTG player for the Frontier intro, it's a full implementation of the
Frontier: Elite II renderer.  The 3d models and music are read from the official
Frontier exe.  You must already own Frontier or alternatively download the
shareware version and send 5 pounds to Frontier Developments.


---
Requirements
---

 - Amiga OS 3.x or AROS
 - RTG 640x360x8
 - 68040 or better (real 68040 is too slow)
 - 8MB RAM or more

Tested on FS-UAE / WinUAE and on Vampire V4, reported to work well on a V1200
by Dr Procton!

'Frontier: Elite II' exe must exist in same directory as 'fintro'.  Use the exe
from the CD32 version or the final Elite Club shareware version.  This can be
downloaded from here:

  https://www.sharoma.com/frontierverse/files/game/fe2_amiga_cd32.rar

A 640 pixel wide RTG resolution must be available.  The closest RTG resolution
of 640 width and 8-bit palette (CLUT) will be used.  640x360 is 16:9 and looks
good.


---
Controls
---

  `          - Display frame rate / other stats
  Space      - Pause/Play
  Arrow Keys - Back/Forward frames, press shift to speed up
  Escape     - Exit
  Left Mouse - Exit

Somewhat OS friendly, multi-tasking is still enabled.  But stop any music players
before starting as it trounces audio registers.


---
Command Line Options
---

No music:

   fintro -mod 0


Any track from main game can be played (1 - 10):

   fintro -mod 1


---
FAQ
---

Q. Could this be patched into original Amiga exe so we can play FE2 in RTG?
A. Yes it could be, it would be work, but the original exe had jump tables for
   the renderer that this code mostly re-implements.

Q. It crashes on ApolloOS / AROS!
A. For me too.  There's at least two issues:
     - The software interrupt that generates the sound can get corrupted.  This
       only happens if you run the startup sequence.
     - PC or status register gets corrupted during rendering, likely also an
       interrupt issue.
   Of course, it could be a bug on my side - but it's not for lack of looking -
   let me just say that!
   A solution would be to turn off the OS while running, and use HW interrupts
   for the sound.


---
Feedback
---

Either to github.com/watsonmw/fe2-intro or directly to watsonmw@gmail.com
