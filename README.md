Frontier Elite II Intro Player
---

![Alt text](/docs/frontier.png?raw=true "Frontier Intro")

This is a reverse engineered player for the 'Frontier: Elite II' game intro.

It implements the full 3d renderer and music playback code from the original
in C.

Also includes:

- Compiler & decompiler for the game's 3d model format
- Updated 3d models (e.g. added planet sky to intro)
- Engine / sound effects playback
- SDL support (for non-Amiga platforms)
- Support for 2x and 4x times the original display resolution


Running
---

Releases are available here:

    https://github.com/watsonmw/fe2-intro/releases

The 3d models and music are read directly from official Frontier exe.  You
must already own Frontier or alternatively send 5 pounds to Frontier
Developments to purchase the shareware version.

Frontier Elite II exe must exist in same directory as 'fintro'.  Use the exe
from the CD32 version or the final Elite Club shareware version.  This can be
downloaded from here:

    https://www.sharoma.com/frontierverse/files/game/fe2_amiga_cd32.rar

To run on an Amiga you'll need RTG and a (very) fast Amiga.

It runs great on a Vampire and UAE.  Have had reports of it running well on
68060 accerated Amigas.  There's even a fork with raster optimizations for
the ZZ9000 accelerator card.

Will run on any platform that supports SDL as well.


Building
---

Amiga:

Built for Amiga using bebbo's GCC compiler, once installed the bash script
'amiga/build-ami-gcc.sh' can used to cross compile from Linux.


SDL:

Other platforms use SDL for platform support and CMake for compiling.  A CMake
file is included that works on Mac with xcode and Windows with Mingw.


Why?
---

DooM is ported to pretty much every platform due to be having a reference
implementation in C.  I wanted something simalr for FE2 and have always been
curious about how the renderer worked.

The whole game is not ported because, well.. that would require a ton more work,
and maybe Frontier will want to re-release it at some point.

BTW: If you are checking this out you probably already know about
Elite: Dangerous, but in case you don't, it's great and you can check it out at:

    https://www.elitedangerous.com/

Might be a while before that is ported to the Amiga though :P


Implementation
---

Frontier renders the 3d world using its own byte code (well 16bit code, because
it was written with 16 bit buses in mind). This code specifies the primitives to
display, level of detail calculations, animations, and more.  Much of this is
described in entertaining detail here:

    http://www.jongware.com/galaxy7.html 

This project implements much more than is described there though, I'd say 95%
of the original Frontier's graphics code is implemented here, mostly just 2d
and navigation / galaxy rendering is missing.  Every 3d model including ships
and planets can be rendered.

A music and sound fx player is included in 'audio.c'.  It shows how to emulate
amiga sounds in SDL, and how the original mod format is laid out.  The original
samples are cleaned up a bit to reduce clicking / popping.  (If anyone wants to
contribute 16-bit samples for each of the 12 or so instruments, and can keep
the same overall sound let me know and I'll add them as overrides.)

    assets.[ch]      - Helpers for loading the Amiga assets
    audio.[ch]       - Music and sfx playback (music only used in intro)
    amigahw.h        - Helpers for writing directly to Amiga hardware registers
    builddata.[ch]   - Helpers for generating compiled in assets (e.g. larger
                       highlight images)
    fintro.[ch]      - Setup and control for each scene in the intro
    fmath.h          - Frontier math functions both fixed point and soft
                       floating point
    main-amiga.c     - The Amiga entry point & platform specific code
    main-sdl.c       - The SDL entry point & platform specific code
    mlib.[ch]        - My own C array and memory management helpers
    modelcode.[ch]   - Compiler + decompiler for Frontier 3d objects (& vector
                       fonts)
    render.[ch]      - Contains the renderer and raster
    renderinternal.h - Code shared between compiler & renderer

The renderer is pretty close to the original, but allows for higher resolutions
and uses divides a bit more frequently.  The original had to run on 68000 which
only did slow 16 bit divides, but that's not an issue for us.
