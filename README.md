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

The Web version can be run directly from here:

    https://watsonmw.com/fintro/fe2-intro.html

Amiga & PC releases are available here:

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
68060 accelerated Amigas.  There's even a fork with raster optimizations for
the ZZ9000 accelerator card.

Will run on any platform that supports SDL as well.


Building
---

Amiga:

Built for Amiga using Bebbo's GCC compiler, once installed the bash script
'scripts/build-amiga-gcc.sh' can be used to cross compile from Linux.


SDL:

An SDL version is available to build on Linux, Windows (MinGW), and OSX:

    cmake .  -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
    make -C cmake-build-release fintro


Dear ImGui + SDL:

This version allows you to debug the intro and other Frontier models, useful
for reverse engineering efforts.

    cmake .  -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
    make -C cmake-build-release fintro-imgui


WASM:

To build WASM install a version of clang with WASM support, then run the
following script:

    scripts/build-wasm.sh

The Clang path may need to be changed to match yours.

    CLANG=clang-15

A Python3 script is provided for serving up the WASM code locally:

    python3 scripts/wasmserver.py

You can then open in your favourite WASM supporting browser (basically any
Chromium or WebKit based browser since 2018):

    http://localhost:8000/build-wasm/fe2-intro.html


Why?
---

DooM is ported to pretty much every platform due to be having a reference
implementation in C.  I wanted something similar for FE2 and have always been
curious about how the renderer worked.

The whole game is not ported because, well... that would require a ton more work,
and maybe Frontier will want to re-release it at some point.

BTW: If you are checking this out you probably already know about
Elite: Dangerous, but in case you don't, it's great and you can check it out at:

    https://www.elitedangerous.com/

Might be a while before that is ported to the Amiga though :P


Implementation
---

Frontier renders the 3d world using its own byte code (well 16-bit code, because
it was written with 16 bit buses in mind). This code specifies the primitives to
display, level of detail calculations, animations, and more.  Much of this is
described in entertaining detail here (for FFE):

    https://web.archive.org/web/20220117065108/http://www.jongware.com/galaxy7.html

This project implements much more than is described there though, I'd say 95%
of the original Frontier's graphics code is implemented here, mostly just some
2d and navigation / galaxy rendering is missing.  Every 3d model including all
ships and planets can be rendered.

A music and sound fx player is included in 'audio.c'.  It shows how to emulate
Amiga sounds in SDL, and how the original mod format is laid out.  The original
samples are cleaned up a bit to reduce clicking / popping.  (If anyone wants to
contribute 16-bit samples for each of the 12 or so instruments, and can keep
the same overall sound, let me know, and I'll add them as overrides.)

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
only did slow 16-bit divides, but that's not an issue for us.

For details about the original renderer see my: 'Frontier: Elite 2 engine study':

    https://watsonmw.com/fintro
