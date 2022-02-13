#include "log.h"
#include "mpris.h"
#include "player.h"
#include "util.h"

#include <stdbool.h>

static void dbus_handle_getall(DBusMessage *msg);

int dbus_active;
DBusConnection *dbus_conn;

static const char *const dbus_mpris_caps[] = {
	"CanPlay",
	"CanPause",
	"CanGoPrevious",
	"CanGoNext",
	"CanControl"
};

void
dbus_handle_getall(DBusMessage *msg)
{
	DBusMessage *reply;
	DBusError err;
	DBusMessageIter iter, aiter, eiter, viter;
	const char *interface;
	dbus_bool_t can;
	int i, ok;

	dbus_error_init(&err);

	ok = dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &interface);
	if (ok && strcmp(interface, "org.mpris.MediaPlayer2.Player")) {
		dbus_error_free(&err);
		return;
	}

	reply = dbus_message_new_method_return(msg);

	/* TODO: change to more sane api like gio and gdbus (?) */

	/* connect argument iter to message */
	dbus_message_iter_init_append(reply, &iter);

	/* add array of dict entries { string : variant } */
	ASSERT(dbus_message_iter_open_container(&iter, 'a', "{sv}", &aiter));

	for (i = 0; i < ARRLEN(dbus_mpris_caps); i++) {
		/* add dict entry */
		ASSERT(dbus_message_iter_open_container(&aiter,
			'e', NULL, &eiter));

		/* string key */
		ASSERT(dbus_message_iter_append_basic(&eiter,
			's', &dbus_mpris_caps[i]));

		/* variant container */
		ASSERT(dbus_message_iter_open_container(&eiter,
			'v', "b", &viter));

		can = true; /* bool value */
		ASSERT(dbus_message_iter_append_basic(&viter, 'b', &can));

		dbus_message_iter_close_container(&eiter, &viter);
		dbus_message_iter_close_container(&aiter, &eiter);
	}

	dbus_message_iter_close_container(&iter, &aiter);

	dbus_connection_send(dbus_conn, reply, NULL);

	dbus_message_unref(reply);

	dbus_error_free(&err);
}

void
dbus_init(void)
{
	DBusError err;
	int ret;

	dbus_active = 0;

	dbus_error_init(&err);

	/* dont fail if dbus not available, not everyone has
	 * it or needs it to play music */
	dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (dbus_error_is_set(&err) || !dbus_conn) {
		dbus_error_free(&err);
		return;
	}

	/* register as MPRIS compliant player for events */
	ret = dbus_bus_request_name(dbus_conn, "org.mpris.MediaPlayer2.tmus",
		DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
	if (dbus_error_is_set(&err))
		PANIC("Failed to register as MPRIS service\n");

	log_info("DBus active!\n");

	dbus_active = 1;

	dbus_error_free(&err);
}

void
dbus_deinit(void)
{
	if (!dbus_active) return;

	dbus_connection_unref(dbus_conn);
}

void
dbus_update(void)
{
	DBusMessage *msg;
	const char *interface;
	const char *method;

	if (!dbus_active) return;

	dbus_connection_read_write(dbus_conn, 0);
	msg = dbus_connection_pop_message(dbus_conn);
	if (msg == NULL) return;

	method = dbus_message_get_member(msg);
	interface = dbus_message_get_interface(msg);
	if (!strcmp(interface, "org.freedesktop.DBus.Properties")) {
		log_info("DBus: Properties requested\n");
		if (!strcmp(method, "GetAll"))
			dbus_handle_getall(msg);
	} else if (!strcmp(interface, "org.mpris.MediaPlayer2.Player")) {
		log_info("MPRIS: Method %s\n", method);
		if (!strcmp(method, "PlayPause")) {
			player_toggle_pause();
		} else if (!strcmp(method, "Next")) {
			player_next();
		} else if (!strcmp(method, "Previous")) {
			player_prev();
		}
	}

	dbus_message_unref(msg);
}

