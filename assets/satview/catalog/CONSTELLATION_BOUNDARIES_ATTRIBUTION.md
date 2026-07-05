# Constellation Boundary And Name Data

`constellation_boundaries.dxbnd` contains a modified, render-ready form of the
constellation boundary and name data in D3-Celestial.

- Project: https://github.com/ofrohn/d3-celestial
- Pinned commit: `7e720a3de062059d4c5400a379146a601d9010e0`
- Source files: `data/constellations.borders.json` and
  `data/constellations.json`
- Original boundary catalog: Davenhall and Leggett, VizieR VI/49
- License: BSD 3-Clause

Draxul converts the J2000 GeoJSON longitude/declination coordinates into its
render-space J2000 unit directions, subdivides long spherical segments to at
most one degree, stores each shared boundary once, and packs the 89 named area
anchors (including separate Serpens Caput and Serpens Cauda areas) into a
versioned binary file. Draxul's renderer code is not derived from D3-Celestial.

## BSD 3-Clause License

Copyright (c) 2015, Olaf Frohn
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
