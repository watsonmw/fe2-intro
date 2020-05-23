Frontier Elite II Intro Player
---

![Alt text](/docs/frontier.png?raw=true "Frontier Intro")

This is a renderer for Frontier: Elite II, it can play the full intro with the
bombastic music.  

The 3d models and music are read from official Frontier exe.  You must already
own Frontier or alternatively send 5 pounds to Frontier Developments to
purchase the shareware version.

Frontier Elite II exe must exist in same directory as 'fintro'.  Use the exe
from the CD32 version or the final Elite Club shareware version.  This can be
downloaded from here:

  https://www.sharoma.com/frontierverse/files/game/fe2_amiga_cd32.rar


Running
---

To run on an Amiga (with RTG), an LHA is available from the github pages:

    https://github.com/watsonmw/fe2-intro/releases

It runs great on a Vampire and UAE.  If you run on real 68060 let me know!


Why?
---

DooM is ported to pretty much every platform and widely acclaimed with little
black books written on it and everything.  I think Frontier is an even greater
achievement and deserves its own ports and little dark blue books!  I wanted
to do a portable version of the renderer - so here it is.

The whole game is not ported because, well, that would require a ton more time,
and besides Frontier may want to re-release it at some point.  If you are
checking this out you probably already know about Elite: Dangerous, but in case
you don't, it's great and you can check it out at:

    https://www.elitedangerous.com/

Might be a while before it is ported to the Amiga though :P


Implementation
---

Frontier renders the 3d world using its own byte code to specify primitives to
display, level of detail calculations, animations, and more.  Much of this is
described in entertaining detail here:

    http://www.jongware.com/galaxy7.html 

This project implements much more than is described there though, I'd say 90%
of the original Frontier's graphics code is implemented here, mostly just 2d
and navigation / galaxy rendering is missing.  Every 3d model including planets
can be rendered.

A music player is also included in 'audio.c'.  It shows how to emulate amiga
sounds in SDL, and how the original mod format is laid out.  The original
samples are cleaned up a bit to reduce clicking / popping.  If anyone wants to
contribute 16-bit samples for each of the 12 or so instruments, and can keep
the same overall sound let me know and I'll add them as overrides.  I don't
have an ear for it, but there's a lot of reuse - by changing pitch and cutting
sounds off early.

