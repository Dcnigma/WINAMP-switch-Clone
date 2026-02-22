#pragma once

#ifdef __cplusplus
extern "C" {
#endif


void playerInit();
void playerPlay(int index);
void playerStop();
//void playerNext();
//void playerPrev();
bool playerIsPlaying();
void playerUpdate();

//bool playerIsPlaying();
bool playerIsPaused();

bool playerIsShuffleEnabled();
bool playerIsRepeatEnabled();

void playerToggleShuffle();
void playerToggleRepeat();

float playerGetPosition();   // seconds
void  playerSeek(float sec);


#define PREV_RESTART_THRESHOLD 3.0f

#define FFT_SIZE 1024
extern float g_fftInput[FFT_SIZE];

void playerSetVolume(float v);
float playerGetVolume();
void playerAdjustVolume(float delta);


void playerSetPan(float pan);
float playerGetPan();
// Playback control
void playerTogglePause();
void playerNext();
void playerPrev();

// Repeat / Shuffle
void playerToggleShuffle();
void playerCycleRepeat();

//int playerGetCurrentIndex();
int playerGetCurrentTrackIndex();
int playerGetElapsedSeconds();
int playerGetTrackLength();
int playlistGetCurrentIndex();

//int  playerGetElapsedSeconds();
//int  playerGetTrackLength();





#ifdef __cplusplus
}
#endif
