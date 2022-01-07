#include "util.h"

#include "dbus-1.0/dbus/dbus-glib.h"
#include "dbus-1.0/dbus/dbus.h"

void dbus_init(void);
void dbus_update(void);
void dbus_end(void);

extern int dbus_active;
extern DBusConnection *dbus_conn;

