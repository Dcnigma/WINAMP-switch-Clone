#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "player_state.h"
struct Mp3MetadataEntry;
void playerInit();
void playerPlay(int index);
void playerStop();
void playerStartCrossfade();
void playerShutdown();
//void playerNext();
void playerPrev();
bool playerIsPlaying();
void playerUpdate();
void applyReplayGainFromMetadata(const Mp3MetadataEntry& meta);

const char* playerGetCurrentTrackPath();
void playerUpdateTrackIndex(int newIndex);

bool playerIsPaused();

int  playerGetCurrentTrackIndex();
int  playerGetElapsedSeconds();
int  playerGetTrackLength();
const char* playerGetCurrentTrackPath();  // <-- Add this


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
RepeatMode playerGetRepeatMode();

//int playerGetCurrentIndex();
int playerGetCurrentTrackIndex();
int playerGetElapsedSeconds();
int playerGetTrackLength();
int playlistGetCurrentIndex();
void playerUpdateTrackIndex(int newIndex);
// Get the file path of the currently playing track
// Returns nullptr if nothing is playing



//int  playerGetElapsedSeconds();
//int  playerGetTrackLength();





#ifdef __cplusplus
}
#endif
