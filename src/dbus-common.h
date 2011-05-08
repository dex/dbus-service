#ifndef _DBUS_COMMON_H_

#define _DBUS_COMMON_H_

#include <dbus/dbus.h>

#define evtimer_once(cb, arg, tv) \
    event_once(-1, EV_TIMEOUT, (cb), (arg), (tv))

struct service_dbus_priv {
	DBusConnection *con;
	int should_dispatch;
	struct event ev_sig;
	struct event ev_early_dispatch;
};

struct service_dbus_watch_priv {
	struct service_dbus_priv *service_priv;
	struct event ev_watch;
};

struct service_dbus_timeout_priv {
	struct service_dbus_priv *service_priv;
	struct event ev_timeout;
};

int init_dbus_with_event_loop(struct service_dbus_priv *priv);

#endif /* end of include guard: _DBUS_COMMON_H_ */
