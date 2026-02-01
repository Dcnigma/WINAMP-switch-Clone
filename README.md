# 🎵 Winamp-Style Spectrum Visualizer (Nintendo Switch Homebrew)

This project is a Winamp-inspired music player UI for Nintendo Switch homebrew, featuring a classic-style spectrum analyzer, playlist, and MP3 playback.

## ✨ Features

- Winamp-style vertical spectrum visualizer (20 bars)
- Real-time FFT audio analysis
- Smooth peak falloff like the original Winamp
- MP3 playback with metadata
- Playlist UI
- SDL2-based rendering

---

## 🔊 Audio Spectrum Analysis

This project uses **Fast Fourier Transform (FFT)** to analyze real-time audio and render the spectrum visualizer.

FFT functionality is powered by:

### **KissFFT (Keep It Simple, Stupid FFT)**  
A lightweight, highly portable FFT library.

**Repository:** https://github.com/mborgerding/kissfft  
**License:** BSD-style license

KissFFT is used to:
- Convert PCM audio samples into frequency data
- Split frequencies into 20 visual spectrum bands
- Drive the Winamp-style visualizer

---

## 📜 Third-Party Licenses

This project includes third-party open-source software:

### KissFFT License

KissFFT is licensed under a BSD-style license.
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

## 🛠 Dependencies

- SDL2
- SDL2_ttf
- SDL2_mixer
- KissFFT

---

## ❤️ Credits

- Winamp visualizer style inspired by the classic Winamp player  
- FFT powered by **KissFFT**  
- Built for the Nintendo Switch homebrew community

---

## ⚠️ Disclaimer

This project is a fan-made UI recreation for educational and homebrew development purposes and is not affiliated with or endorsed by Winamp.






## Work in Progress
todo:

- make a more accurate VBR duration estimator using the Xing header to avoids the approximate “CBR-based” calculation and is much closer to real duration for VBR MP3s.

- Album tag (TALB)

- Track number (TRCK)

- Proper UTF-16 decoding (for non-ASCII tags)

- Touchscreen controls

🎚 Seek bar

🔊 Volume control

⏸ Pause / Resume

🔁 Repeat / Shuffle
