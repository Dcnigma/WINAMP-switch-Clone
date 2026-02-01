#pragma once

void playerInit();
void playerPlay(int index);
void playerStop();
void playerNext();
void playerPrev();
bool playerIsPlaying();
void playerUpdate();

#define FFT_SIZE 1024
extern float g_fftInput[FFT_SIZE];

void  playerSetVolume(float v);
float playerGetVolume();
void playerAdjustVolume(float delta);

int playerGetCurrentIndex();
int playerGetCurrentTrackIndex();
int playerGetElapsedSeconds();
int playerGetTrackLength();
int playlistGetCurrentIndex();
