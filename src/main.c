#include "data.h"
#include "log.h"
#include "mpris.h"
#include "player.h"
#include "tui.h"

#include <locale.h>

static void init(void);
static void cleanup(int code, void *arg);

void
init(void)
{
	setlocale(LC_ALL, "");
	srand(time(NULL));

	log_init();

	data_load();

	player_init();

	tui_init();

	dbus_init();

	on_exit(cleanup, NULL);
	signal(SIGINT, exit);
}

void
cleanup(int exitcode, void* arg)
{
	tui_restore();

	player_deinit();

	data_save();
	data_free();

	dbus_deinit();

	tui_deinit();

	log_deinit();
}

int
main(int argc, const char **argv)
{
	init();

	do {
		dbus_update();
		player_update();
	} while (tui_update());
}

