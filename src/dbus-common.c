#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event.h>
#include <signal.h>
#include "dbus-common.h"

static void dispatch_data(DBusConnection *con)
{
	while (dbus_connection_get_dispatch_status(con) == 
			DBUS_DISPATCH_DATA_REMAINS)
		dbus_connection_dispatch(con);
}

static void dispatch_initial_dbus_messages(int fd, short type, void *arg)
{
	DBusConnection *con = arg;
	dispatch_data(con);
}

static void process_watch(int fd, short event, void *arg)
{
	DBusWatch *watch = arg;
	struct service_dbus_watch_priv *watch_priv = dbus_watch_get_data(watch);
	struct service_dbus_priv *priv = watch_priv->service_priv;

	dbus_connection_ref(priv->con);

	if (event & EV_READ)
		dbus_watch_handle(watch, DBUS_WATCH_READABLE);
	if (event & EV_WRITE)
		dbus_watch_handle(watch, DBUS_WATCH_WRITABLE);

	if (priv->should_dispatch) {
		dispatch_data(priv->con);
		priv->should_dispatch = 0;
	}

	dbus_connection_unref(priv->con);
}

static dbus_bool_t add_watch(DBusWatch *watch, void *data)
{
	struct service_dbus_priv *priv = data;
	unsigned int flags;
	int fd;

	if (!dbus_watch_get_enabled(watch))
		return TRUE;

	flags = dbus_watch_get_flags(watch);
	fd = dbus_watch_get_unix_fd(watch);

	if (flags & DBUS_WATCH_READABLE) {
		struct service_dbus_watch_priv *watch_priv = 
			dbus_malloc0(sizeof(struct service_dbus_watch_priv));
		watch_priv->service_priv = priv;
		event_set(&watch_priv->ev_watch, 
				fd, EV_READ|EV_PERSIST, process_watch, watch);
		event_add(&watch_priv->ev_watch, NULL);
		dbus_watch_set_data(watch, watch_priv, dbus_free);
	}
	if (flags & DBUS_WATCH_READABLE) {
		struct service_dbus_watch_priv *watch_priv = 
			dbus_malloc0(sizeof(struct service_dbus_watch_priv));
		watch_priv->service_priv = priv;
		event_set(&watch_priv->ev_watch, 
				fd, EV_WRITE|EV_PERSIST, process_watch, watch);
		event_add(&watch_priv->ev_watch, NULL);
		dbus_watch_set_data(watch, watch_priv, dbus_free);
	}

	return TRUE;
}

static void remove_watch(DBusWatch *watch, void *data)
{
	unsigned int flags;
	int fd;
	struct service_dbus_watch_priv *watch_priv;
	struct event *ev_watch;

	flags = dbus_watch_get_flags(watch);
	fd = dbus_watch_get_unix_fd(watch);
	watch_priv = dbus_watch_get_data(watch);
	ev_watch = &watch_priv->ev_watch;

	if (flags & DBUS_WATCH_READABLE) {
		if (event_initialized(ev_watch) && 
				event_pending(ev_watch, EV_READ, NULL))
			event_del(ev_watch);
	}
	if (flags & DBUS_WATCH_WRITABLE) {
		if (event_initialized(ev_watch) && 
				event_pending(ev_watch, EV_WRITE, NULL))
			event_del(ev_watch);
	}
}

static void watch_toggled(DBusWatch *watch, void *data)
{
	struct service_dbus_watch_priv *watch_priv;
	struct event *ev_watch;

	watch_priv = dbus_watch_get_data(watch);
	ev_watch = &watch_priv->ev_watch;

	if (dbus_watch_get_enabled(watch))
		event_add(ev_watch, NULL);
	else 
		event_del(ev_watch);
}

static void process_timeout(int fd, short event, void *arg)
{
	DBusTimeout *timeout = arg;
	dbus_timeout_handle(timeout);
}

static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data)
{
	struct service_dbus_priv *priv = data;
	struct service_dbus_timeout_priv *timeout_priv;
	struct timeval interval;

	if (!dbus_timeout_get_enabled(timeout))
		return TRUE;

	interval.tv_sec = dbus_timeout_get_interval(timeout)/1000;
	interval.tv_usec = (dbus_timeout_get_interval(timeout)%1000)*1000;

	timeout_priv = dbus_malloc0(sizeof(struct service_dbus_timeout_priv));
	timeout_priv->service_priv = priv;
	evtimer_set(&timeout_priv->ev_timeout, process_timeout, timeout);
	evtimer_add(&timeout_priv->ev_timeout, &interval);
	dbus_timeout_set_data(timeout, timeout_priv, dbus_free);

	return TRUE;
}

static void remove_timeout(DBusTimeout *timeout, void *data)
{
	struct service_dbus_timeout_priv *timeout_priv;
	struct event *ev_timeout;

	timeout_priv = dbus_timeout_get_data(timeout);
	ev_timeout = &timeout_priv->ev_timeout;

	if (evtimer_initialized(ev_timeout) && evtimer_pending(ev_timeout, NULL))
		evtimer_del(ev_timeout);
}

static void timeout_toggled(DBusTimeout *timeout, void *data)
{
	struct service_dbus_timeout_priv *timeout_priv;
	struct event *ev_timeout;
	struct timeval interval;

	timeout_priv = dbus_timeout_get_data(timeout);
	ev_timeout = &timeout_priv->ev_timeout;

	interval.tv_sec = dbus_timeout_get_interval(timeout)/1000;
	interval.tv_usec = (dbus_timeout_get_interval(timeout)%1000)*1000;

	if (dbus_timeout_get_enabled(timeout))
		evtimer_add(ev_timeout, &interval);
	else
		evtimer_del(ev_timeout);
}

static void process_wakeup_main(int sig, short event, void *arg)
{
	struct service_dbus_priv *priv = arg;

	if (sig != SIGPOLL || !priv->con)
		return;

	if (dbus_connection_get_dispatch_status(priv->con) !=
			DBUS_DISPATCH_DATA_REMAINS)
		return;

	/* Only dispatch once - we do not want to starve other events */
	dbus_connection_ref(priv->con);
	dbus_connection_dispatch(priv->con);
	dbus_connection_unref(priv->con);
}

static void wakeup_main(void *data)
{
	struct service_dbus_priv *priv = data;

	/* Use SIGPOLL to break out of the event loop */
	raise(SIGPOLL);
	priv->should_dispatch = 1;
}

static int integrate_with_event(struct service_dbus_priv *priv)
{
	if (!dbus_connection_set_watch_functions(priv->con, add_watch,
				remove_watch, watch_toggled, priv, NULL) ||
			!dbus_connection_set_timeout_functions(priv->con,
				add_timeout, remove_timeout, timeout_toggled,
				priv, NULL)) {
		fprintf(stderr, "dubs: Failed to set callback functions\n");
		return -1;
	}

	signal_set(&priv->ev_sig, SIGPOLL, process_wakeup_main, priv);
	signal_add(&priv->ev_sig, NULL);

	dbus_connection_set_wakeup_main_function(priv->con, wakeup_main,
			priv, NULL);

	return 0;
}

static void free_service_object_desc_cb(DBusConnection *con, void *user_data)
{
	struct service_dbus_object_desc *obj_desc = user_data;
	if (!obj_desc)
		return;
	free(obj_desc->path);
}

static DBusHandlerResult message_handler(DBusConnection *con, DBusMessage *msg,
		void *user_data)
{
	struct service_dbus_object_desc *obj_desc = user_data;
	const char *path;
	const char *msg_interface;
	const char *method;

	path = dbus_message_get_path(msg);
	msg_interface = dbus_message_get_interface(msg);
	method = dbus_message_get_member(msg);
	if (!path || !msg_interface || !method)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	/* TODO: handle message call */

	return DBUS_HANDLER_RESULT_HANDLED;
}

int init_dbus_with_event_loop(struct service_dbus_priv *priv)
{
	DBusError error;
	int ret = 0;
	struct timeval delay;
	struct service_dbus_object_desc *obj_desc;
	DBusObjectPathVTable service_vtable = {
		&free_service_object_desc_cb, &message_handler,
		NULL, NULL, NULL, NULL
	};

	/* Get Bus */
	dbus_error_init(&error);
	priv->con = dbus_bus_get(DBUS_BUS_SESSION, &error);
	if (!priv->con) {
		fprintf(stderr, "dbus: Could not acquire the session bus "
				"bus: %s - %s\n", error.name, error.message);
		ret = -1;
		goto fail;
	}
	dbus_error_free(&error);

	/* Reigster handler for object path */
	obj_desc = dbus_malloc0(sizeof(struct service_dbus_object_desc));
	obj_desc->connection = priv->con;
	obj_desc->path = strdup(MY_DBUS_OBJECT_PATH);
	if (!dbus_connection_register_object_path(priv->con, MY_DBUS_OBJECT_PATH,
				&service_vtable, obj_desc)) {
		fprintf(stderr, "dbus: Could not set up message handler.\n");
		goto fail;
	}

	/* Request Bus name */
	dbus_error_init(&error);
	if (dbus_bus_request_name(priv->con, MY_DBUS_SERVICE_NAME, 0, &error) != 
			DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		fprintf(stderr, "dbus: Could not request service name.\n");
		goto fail;
	}
	dbus_error_free(&error);

	/* Integrate with libevent */
	integrate_with_event(priv);

	/*
	 * Dispatch initial DBus messages that may have come in since the bus
	 * name was claimed above. Happens when clients are quick to notice the
	 * service.
	 *
	 * FIXME: is there a better solution to this problem?
	 */
	evtimer_set(&priv->ev_early_dispatch, dispatch_initial_dbus_messages,
			priv->con);
	delay.tv_sec = 0;
	delay.tv_usec = 50;
	evtimer_add(&priv->ev_early_dispatch, &delay);

fail:
	return ret;
}
