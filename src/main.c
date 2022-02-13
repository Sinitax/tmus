#include "data.h"
#include "history.h"
#include "list.h"
#include "listnav.h"
#include "log.h"
#include "mpris.h"
#include "pane.h"
#include "player.h"
#include "ref.h"
#include "style.h"
#include "tag.h"
#include "tui.h"
#include "util.h"

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

	data_save();
	data_free();

	player_deinit();

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

