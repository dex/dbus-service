#include <stdio.h>
#include <event.h>
#include <dbus/dbus.h>
#include "dbus-common.h"

int main(int argc, const char *argv[])
{
	struct service_dbus_priv priv;

	event_init();

	init_dbus_common(&priv);

	event_dispatch();
	return 0;
}
