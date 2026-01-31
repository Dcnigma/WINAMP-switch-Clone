#pragma once

void playerInit();
//void playerPlay(int playlistIndex);
void playerPlay(int index);
void playerStop();
void playerNext();
void playerPrev();
bool playerIsPlaying();
int  playerGetCurrentIndex();
void playerUpdate();
int playlistGetCurrentIndex();
