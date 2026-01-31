#pragma once

void playerInit();
void playerPlay(int index);
void playerStop();
void playerNext();
void playerPrev();
bool playerIsPlaying();
void playerUpdate();


int playerGetCurrentIndex();
int playerGetCurrentTrackIndex();
int playerGetElapsedSeconds();
int playerGetTrackLength();
int playlistGetCurrentIndex();
