# 🎵 Winamp-Style Spectrum Visualizer (Nintendo Switch Homebrew)

A Winamp-inspired music player UI for Nintendo Switch homebrew, featuring a classic-style spectrum analyzer, playlist, and MP3 playback.

## ✨ Features

- 🎚 Winamp-style vertical spectrum visualizer (20 bars)
- 🔊 Real-time FFT audio analysis
- 📉 Smooth peak falloff like classic Winamp
- 🎵 MP3 playback with metadata
- 📂 Playlist UI
- 🖥 SDL2-based rendering

---

## 🔊 Audio Spectrum Analysis

This project performs real-time audio spectrum analysis using **Fast Fourier Transform (FFT)**.

FFT functionality is powered by:

### **KissFFT (Keep It Simple, Stupid FFT)**
A lightweight, portable FFT library.

- Repository: https://github.com/mborgerding/kissfft  
- License: BSD-style license

KissFFT is used to:
- Convert PCM audio samples into frequency data
- Split frequencies into 20 visual spectrum bands
- Drive the Winamp-style visualizer

---

## 🛠 Dependencies

This project uses the following open-source libraries:

| Library | Purpose | License |
|--------|---------|---------|
| SDL2 | Graphics, input, rendering | zlib License |
| SDL2_ttf | Font rendering | zlib License |
| SDL2_mixer | Audio playback | zlib License |
| KissFFT | Fast Fourier Transform | BSD-style License |

---

## 📜 Third-Party Licenses

### KissFFT License

Copyright (c) 2003-2010, Mark Borgerding
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


---

### SDL License (applies to SDL2, SDL2_ttf, SDL2_mixer)

SDL is licensed under the zlib license:
This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

The origin of this software must not be misrepresented; you must not claim
that you wrote the original software. If you use this software in a product,
an acknowledgment in the product documentation would be appreciated but is
not required.

Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

This notice may not be removed or altered from any source distribution.


---

## ❤️ Credits

- Winamp visualizer style inspired by the classic Winamp player  
- FFT powered by **KissFFT**  
- Built for the Nintendo Switch homebrew community  

---

## ⚠️ Disclaimer

This project is a fan-made UI recreation for educational and homebrew development purposes.  
It is not affiliated with, endorsed by, or connected to Winamp or its owners.



