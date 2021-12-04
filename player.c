#include "player.h"

#include "portaudio.h"
#include "sndfile.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static struct player player_static;
struct player *player;

struct player *
player_thread()
{
	struct player *player;
	pid_t pid;

	player = mmap(&player_static, sizeof(struct player),
		PROT_READ | PROT_WRITE, MAP_SHARED, -1, 0);
	player->action = PLAYER_NONE;
	player->resp = PLAYER_NOTSET;
	player->alive = 1;

	pid = fork();
	if (!pid) {
		player_main();
		player->alive = 0;
		exit(0);
	}

	player->pid = pid;

	return player;
}

void
player_main(void)
{
	PaStream *stream;
	int status;

	if (Pa_Initialize() != paNoError)
		return;

	while (player->action != PLAYER_EXIT) {
		player->resp = PLAYER_NOTSET;
		switch (player->action) {
		case PLAYER_PLAY:
			player->resp = player_play();
			break;
		case PLAYER_PAUSE:
			player->resp = player_pause();
			break;
		case PLAYER_SKIP:
			player->resp = player_skip();
			break;
		case PLAYER_PREV:
			player->resp = player_prev();
			break;
		}
		Pa_Sleep(100);
	}

	Pa_Terminate();
}

int
player_alive(void)
{
	return player->alive && !kill(player->pid, 0);
}

void
player_loadfile(const char *file)
{
	ASSERT(player_alive());
	player_action(PLAYER_STOP);
}

void
player_action(int action)
{
	ASSERT(player_alive());
	player->action = action;
}

int
player_play(void)
{
	return PLAYER_OK;
}

int
player_pause(void)
{
	return PLAYER_OK;
}

int
player_skip(void)
{
	return PLAYER_OK;
}

int
player_prev(void)
{
	return PLAYER_OK;
}



