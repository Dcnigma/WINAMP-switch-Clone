#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void playerInit();
void playerPlay(int index);
void playerStop();
void playerNext();
void playerPrev();
bool playerIsPlaying();
void playerUpdate();

bool playerIsPlaying();

#define FFT_SIZE 1024
extern float g_fftInput[FFT_SIZE];

void  playerSetVolume(float v);
float playerGetVolume();
void playerAdjustVolume(float delta);

void playerShutdown();

void  playerSetPan(float pan);
float playerGetPan();


int playerGetCurrentIndex();
int playerGetCurrentTrackIndex();
int playerGetElapsedSeconds();
int playerGetTrackLength();
int playlistGetCurrentIndex();

#ifdef __cplusplus
}
#endif
