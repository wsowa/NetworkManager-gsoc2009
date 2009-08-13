/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2006 - 2008 Red Hat, Inc.
 * Copyright (C) 2006 - 2008 Novell, Inc.
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "nm-supplicant-interface.h"
#include "nm-supplicant-manager.h"
#include "nm-utils.h"
#include "nm-marshal.h"
#include "nm-supplicant-config.h"
#include "nm-dbus-manager.h"
#include "nm-call-store.h"
#include "nm-dbus-glib-types.h"
#include "nm-glib-compat.h"

#define WPAS_DBUS_IFACE_INTERFACE   WPAS_DBUS_INTERFACE ".Interface"
#define WPAS_DBUS_IFACE_BSS         WPAS_DBUS_IFACE_INTERFACE ".BSS"
#define WPAS_DBUS_IFACE_NETWORK	  WPAS_DBUS_IFACE_INTERFACE ".Network"
#define WPAS_ERROR_IFACE_UNKNOWN    WPAS_DBUS_INTERFACE ".InterfaceUnknown"
#define WPAS_ERROR_INTERFACE_EXISTS     WPAS_DBUS_INTERFACE ".InterfaceExists"
#define WPAS_GET_PROPERTY_TIMEOUT   1000

G_DEFINE_TYPE (NMSupplicantInterface, nm_supplicant_interface, G_TYPE_OBJECT)


#define NM_SUPPLICANT_INTERFACE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                                 NM_TYPE_SUPPLICANT_INTERFACE, \
                                                 NMSupplicantInterfacePrivate))

static void nm_supplicant_interface_set_property (GObject *      object,
                                                  guint          prop_id,
                                                  const GValue * value,
                                                  GParamSpec *   pspec);

static void nm_supplicant_interface_get_property (GObject *      object,
                                                  guint          prop_id,
                                                  GValue *       value,
                                                  GParamSpec *   pspec);

static void nm_supplicant_interface_start (NMSupplicantInterface *self);

static void nm_supplicant_interface_add_to_supplicant (NMSupplicantInterface *self,
                                                       gboolean get_only);

static void nm_supplicant_interface_smgr_state_changed (NMSupplicantManager *smgr,
                                                        guint32 new_state,
                                                        guint32 old_state,
                                                        gpointer user_data);

static void nm_supplicant_interface_set_state (NMSupplicantInterface *self,
                                               guint32 new_state);


/* Signals */
enum {
	STATE,             /* change in the interface's state */
	REMOVED,           /* interface was removed by the supplicant */
	SCANNED_AP,        /* interface saw a new access point from a scan */
	SCAN_REQ_RESULT,   /* result of a wireless scan request */
	SCAN_RESULTS,      /* scan results returned from supplicant */
	CONNECTION_STATE,  /* link state of the device's connection */
	CONNECTION_ERROR,  /* an error occurred during a connection request */
	LAST_SIGNAL
};
static guint nm_supplicant_interface_signals[LAST_SIGNAL] = { 0 };


/* Properties */
enum {
	PROP_0 = 0,
	PROP_SUPPLICANT_MANAGER,
	PROP_DEVICE,
	PROP_STATE,
	PROP_CONNECTION_STATE,
	PROP_SCANNING,
	LAST_PROP
};


typedef struct
{
	NMSupplicantManager * smgr;
	gulong                smgr_state_sig_handler;
	NMDBusManager *       dbus_mgr;
	char *                dev;
	gboolean              is_wireless;

	char *                object_path;
	guint32               state;
	NMCallStore *         assoc_pcalls;
	NMCallStore *         other_pcalls;

	guint32               con_state;
	gboolean              scanning;
	guint32               capabilities;

	DBusGProxy *          iface_proxy;
	DBusGProxy *          net_proxy;
	guint                 pending_blobs;

	guint                 scan_results_timeout;
	guint32               last_scan;

	NMSupplicantConfig *  cfg;

	gboolean              dispose_has_run;
} NMSupplicantInterfacePrivate;

static gboolean
cancel_all_cb (GObject *object, gpointer call_id, gpointer user_data)
{
	dbus_g_proxy_cancel_call (DBUS_G_PROXY (object), (DBusGProxyCall *) call_id);

	return TRUE;
}

static void
cancel_all_callbacks (NMCallStore *store)
{
	nm_call_store_foreach (store, NULL, cancel_all_cb, NULL);
	nm_call_store_clear (store);
}

typedef struct {
	NMSupplicantInterface *interface;
	DBusGProxy *proxy;
	NMCallStore *store;
	DBusGProxyCall *call;
} NMSupplicantInfo;

static gboolean
_wpas_get_property (DBusGProxy *proxy,
						const char *interface,
						const char *prop_name,
						GValue *value)
{
	DBusGProxy *properties_proxy;
	GError *err = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (interface != NULL, FALSE);
	g_return_val_if_fail (prop_name != NULL, FALSE);

	properties_proxy = dbus_g_proxy_new_from_proxy (proxy,
													"org.freedesktop.DBus.Properties",
													dbus_g_proxy_get_path (proxy));

	if (!dbus_g_proxy_call_with_timeout(properties_proxy,
							"Get", WPAS_GET_PROPERTY_TIMEOUT, &err,
							G_TYPE_STRING, interface,
							G_TYPE_STRING, prop_name,
							G_TYPE_INVALID,
							G_TYPE_VALUE, value,
							G_TYPE_INVALID)) {
		g_warning ("Error in device_get_property: %s\n", err->message);
		g_error_free (err);
		ret = FALSE;
	}

	g_object_unref (properties_proxy);

	return ret;
}

static void
_wpas_set_property (DBusGProxy *proxy,
					  const char *interface,
					  const char *prop_name,
					  GValue *value)
{
	DBusGProxy *properties_proxy;

	g_return_if_fail (proxy != NULL);
	g_return_if_fail (interface != NULL);
	g_return_if_fail (prop_name != NULL);

	properties_proxy = dbus_g_proxy_new_from_proxy (proxy,
													"org.freedesktop.DBus.Properties",
													dbus_g_proxy_get_path (proxy));

	dbus_g_proxy_call_no_reply (properties_proxy, "Set",
								G_TYPE_STRING, interface,
								G_TYPE_STRING, prop_name,
								G_TYPE_VALUE, value,
								G_TYPE_INVALID);

	g_object_unref (properties_proxy);
}

static NMSupplicantInfo *
nm_supplicant_info_new (NMSupplicantInterface *interface,
                        DBusGProxy *proxy,
                        NMCallStore *store)
{
	NMSupplicantInfo *info;

	info = g_slice_new0 (NMSupplicantInfo);
	info->interface = g_object_ref (interface);
	info->proxy = g_object_ref (proxy);
	info->store = store;

	return info;
}

static void
nm_supplicant_info_set_call (NMSupplicantInfo *info, DBusGProxyCall *call)
{
	if (call) {
		nm_call_store_add (info->store, G_OBJECT (info->proxy), (gpointer) call);
		info->call = call;
	}
}

static void
nm_supplicant_info_destroy (gpointer user_data)
{
	NMSupplicantInfo *info = (NMSupplicantInfo *) user_data;

	if (info->call)
		nm_call_store_remove (info->store, G_OBJECT (info->proxy), info->call);

	g_object_unref (info->proxy);
	g_object_unref (info->interface);

	g_slice_free (NMSupplicantInfo, info);
}


NMSupplicantInterface *
nm_supplicant_interface_new (NMSupplicantManager * smgr, const char *ifname, gboolean is_wireless)
{
	NMSupplicantInterface * iface;

	g_return_val_if_fail (NM_IS_SUPPLICANT_MANAGER (smgr), NULL);
	g_return_val_if_fail (ifname != NULL, NULL);

	iface = g_object_new (NM_TYPE_SUPPLICANT_INTERFACE,
	                      "supplicant-manager", smgr,
	                      "device", ifname,
	                      NULL);
	if (iface) {
		NM_SUPPLICANT_INTERFACE_GET_PRIVATE (iface)->is_wireless = is_wireless;
		nm_supplicant_interface_start (iface);
	}

	return iface;
}

static void
nm_supplicant_interface_init (NMSupplicantInterface * self)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);

	priv->state = NM_SUPPLICANT_INTERFACE_STATE_INIT;
	priv->con_state = NM_SUPPLICANT_INTERFACE_CON_STATE_DISCONNECTED;
	priv->assoc_pcalls = nm_call_store_new ();
	priv->other_pcalls = nm_call_store_new ();

	priv->dispose_has_run = FALSE;

	priv->dbus_mgr = nm_dbus_manager_get ();
}


static void
nm_supplicant_interface_set_property (GObject *      object,
                                      guint          prop_id,
                                      const GValue * value,
                                      GParamSpec *   pspec)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (object);
	gulong id;

	switch (prop_id) {
		case PROP_SUPPLICANT_MANAGER:
			priv->smgr = NM_SUPPLICANT_MANAGER (g_value_get_object (value));
			g_object_ref (G_OBJECT (priv->smgr));

			id = g_signal_connect (priv->smgr,
			                       "state",
			                       G_CALLBACK (nm_supplicant_interface_smgr_state_changed),
			                       object);
			priv->smgr_state_sig_handler = id;
			break;
		case PROP_DEVICE:
			/* Construct-only */
			priv->dev = g_strdup (g_value_get_string (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
nm_supplicant_interface_get_property (GObject *     object,
                                      guint         prop_id,
                                      GValue *      value,
                                      GParamSpec *  pspec)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (object);

	switch (prop_id) {
		case PROP_SUPPLICANT_MANAGER:
			g_value_set_object (value, G_OBJECT (priv->smgr));
			break;
		case PROP_DEVICE:
			g_value_set_string (value, priv->dev);
			break;
		case PROP_STATE:
			g_value_set_uint (value, priv->state);
			break;
		case PROP_CONNECTION_STATE:
			g_value_set_uint (value, priv->con_state);
			break;
		case PROP_SCANNING:
			g_value_set_boolean (value, priv->scanning);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
try_remove_iface (DBusGConnection *g_connection,
                  const char *path)
{
	DBusGProxy *proxy;

	g_return_if_fail (g_connection != NULL);
	g_return_if_fail (path != NULL);

	proxy = dbus_g_proxy_new_for_name (g_connection,
	                                   WPAS_DBUS_SERVICE,
	                                   WPAS_DBUS_PATH,
	                                   WPAS_DBUS_INTERFACE);
	if (!proxy)
		return;

	dbus_g_proxy_call_no_reply (proxy, "RemoveInterface",
	                            DBUS_TYPE_G_OBJECT_PATH, path,
	                            G_TYPE_INVALID);
	g_object_unref (proxy);
}

static void
nm_supplicant_interface_dispose (GObject *object)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (object);
	guint32 sm_state;

	if (priv->dispose_has_run) {
		G_OBJECT_CLASS (nm_supplicant_interface_parent_class)->dispose (object);
		return;
	}

	priv->dispose_has_run = TRUE;

	/* Ask wpa_supplicant to remove this interface */
	sm_state = nm_supplicant_manager_get_state (priv->smgr);
	if (sm_state == NM_SUPPLICANT_MANAGER_STATE_IDLE) {
		if (priv->object_path) {
			try_remove_iface (nm_dbus_manager_get_connection (priv->dbus_mgr),
			                  priv->object_path);
		}
	}

	if (priv->iface_proxy)
		g_object_unref (priv->iface_proxy);

	if (priv->net_proxy)
		g_object_unref (priv->net_proxy);

	if (priv->scan_results_timeout)
		g_source_remove (priv->scan_results_timeout);

	if (priv->smgr) {
		g_signal_handler_disconnect (priv->smgr,
		                             priv->smgr_state_sig_handler);
		g_object_unref (priv->smgr);
	}

	g_free (priv->dev);

	/* Cancel pending calls before unrefing the dbus manager */
	cancel_all_callbacks (priv->other_pcalls);
	nm_call_store_destroy (priv->other_pcalls);

	cancel_all_callbacks (priv->assoc_pcalls);
	nm_call_store_destroy (priv->assoc_pcalls);

	if (priv->dbus_mgr)
		g_object_unref (priv->dbus_mgr);

	if (priv->cfg)
		g_object_unref (priv->cfg);

	g_free (priv->object_path);

	/* Chain up to the parent class */
	G_OBJECT_CLASS (nm_supplicant_interface_parent_class)->dispose (object);
}

static void
nm_supplicant_interface_class_init (NMSupplicantInterfaceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (object_class, sizeof (NMSupplicantInterfacePrivate));

	object_class->dispose = nm_supplicant_interface_dispose;
	object_class->set_property = nm_supplicant_interface_set_property;
	object_class->get_property = nm_supplicant_interface_get_property;

	/* Properties */
	g_object_class_install_property (object_class,
	                                 PROP_SUPPLICANT_MANAGER,
	                                 g_param_spec_object ("supplicant-manager",
	                                                      "Supplicant Manager",
	                                                      "Supplicant manager to which this interface belongs",
	                                                      NM_TYPE_SUPPLICANT_MANAGER,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_DEVICE,
	                                 g_param_spec_string ("device",
	                                                      "Device",
	                                                      "Device which this interface represents to the supplicant",
	                                                      NULL,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_STATE,
	                                 g_param_spec_uint ("state",
	                                                    "State",
	                                                    "State of the supplicant interface; INIT, READY, or DOWN",
	                                                    NM_SUPPLICANT_INTERFACE_STATE_INIT,
	                                                    NM_SUPPLICANT_INTERFACE_STATE_LAST - 1,
	                                                    NM_SUPPLICANT_INTERFACE_STATE_INIT,
	                                                    G_PARAM_READABLE));

	g_object_class_install_property (object_class,
	                                 PROP_SCANNING,
	                                 g_param_spec_boolean ("scanning",
	                                                    "Scanning",
	                                                    "Scanning",
	                                                    FALSE,
	                                                    G_PARAM_READABLE));

	/* Signals */
	nm_supplicant_interface_signals[STATE] =
		g_signal_new ("state",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NMSupplicantInterfaceClass, state),
		              NULL, NULL,
		              _nm_marshal_VOID__UINT_UINT,
		              G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	nm_supplicant_interface_signals[REMOVED] =
		g_signal_new ("removed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NMSupplicantInterfaceClass, removed),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	nm_supplicant_interface_signals[SCANNED_AP] =
		g_signal_new ("scanned-ap",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NMSupplicantInterfaceClass, scanned_ap),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__POINTER,
		              G_TYPE_NONE, 1, G_TYPE_POINTER);

	nm_supplicant_interface_signals[SCAN_REQ_RESULT] =
		g_signal_new ("scan-req-result",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NMSupplicantInterfaceClass, scan_req_result),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__BOOLEAN,
		              G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	nm_supplicant_interface_signals[SCAN_RESULTS] =
		g_signal_new ("scan-results",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NMSupplicantInterfaceClass, scan_results),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__UINT,
		              G_TYPE_NONE, 1, G_TYPE_UINT);

	nm_supplicant_interface_signals[CONNECTION_STATE] =
		g_signal_new ("connection-state",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NMSupplicantInterfaceClass, connection_state),
		              NULL, NULL,
		              _nm_marshal_VOID__UINT_UINT,
		              G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	nm_supplicant_interface_signals[CONNECTION_ERROR] =
		g_signal_new ("connection-error",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (NMSupplicantInterfaceClass, connection_error),
		              NULL, NULL,
		              _nm_marshal_VOID__STRING_STRING,
		              G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
emit_error_helper (NMSupplicantInterface *self,
				   GError *err)
{
	const char *name = NULL;

	if (err->domain == DBUS_GERROR && err->code == DBUS_GERROR_REMOTE_EXCEPTION)
		name = dbus_g_error_get_name (err);

	g_signal_emit (self,
	               nm_supplicant_interface_signals[CONNECTION_ERROR],
	               0,
	               name,
	               err->message);
}

static void
request_bssid_properties (NMSupplicantInterface * self,
                          const char * op)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	DBusGProxy *proxy;
	GValue value = {0,};
	gboolean result;
	GHashTable *hash = NULL;

	proxy = dbus_g_proxy_new_for_name (nm_dbus_manager_get_connection (priv->dbus_mgr),
	                                   WPAS_DBUS_SERVICE,
	                                   op,
	                                   WPAS_DBUS_IFACE_BSS);

	result = _wpas_get_property(proxy, WPAS_DBUS_IFACE_BSS, "Properties", &value);

	if (result) {
		hash = g_value_get_boxed (&value);
		g_signal_emit (self,
		               nm_supplicant_interface_signals[SCANNED_AP],
		               0,
		               hash);

		g_value_unset (&value);
	} else {
		nm_warning ("could not get BSS properties: %s", op);
	}

	g_object_unref (proxy);
}

static gboolean
request_scan_results (gpointer user_data)
{
	NMSupplicantInterface *self = NM_SUPPLICANT_INTERFACE (user_data);
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	GTimeVal cur_time;
	GPtrArray *array = NULL;
	GValue value = {0,};
	gboolean result;

	priv->scan_results_timeout = 0;

	g_return_val_if_fail (priv->iface_proxy != NULL, FALSE);

	result = _wpas_get_property(priv->iface_proxy, WPAS_DBUS_IFACE_INTERFACE, "BSSs", &value);

	if (result) {
		int i;

		array = g_value_get_boxed(&value);

		/* Notify listeners of the result of the scan */
		g_signal_emit (self,
					   nm_supplicant_interface_signals[SCAN_RESULTS],
					   0,
					   array->len);

		/* Get a "Properties" property for each returned BSSID */
		for (i = 0; i < array->len; i++) {
			char *op = g_ptr_array_index (array, i);

			request_bssid_properties (self, op);
		}

		g_value_unset (&value);
	} else {
		nm_warning ("could not get scan results");
	}

	g_get_current_time (&cur_time);
	priv->last_scan = cur_time.tv_sec;
	return FALSE;
}

static void
wpas_iface_query_scan_results (DBusGProxy *proxy, gboolean success, gpointer user_data)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (user_data);
	GTimeVal cur_time;

	/* Do not query scan results if scan failed */
	if(!success)
		return;

	/* Only query scan results if a query is not queued */
	if (priv->scan_results_timeout)
		return;

	g_get_current_time (&cur_time);

	/* Only fetch scan results every 4s max, but initially do it right away */
	if (priv->last_scan + 4 < cur_time.tv_sec) {
		priv->scan_results_timeout = g_idle_add (request_scan_results,
		                                         user_data);
	} else {
		priv->scan_results_timeout =
			g_timeout_add_seconds ((4 - (cur_time.tv_sec - priv->last_scan)),
			               request_scan_results, user_data);
	}
}

static guint32
wpas_state_string_to_enum (const char * str_state)
{
	guint32 enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_DISCONNECTED;

	if (!strcmp (str_state, "DISCONNECTED"))
		enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_DISCONNECTED;
	else if (!strcmp (str_state, "INACTIVE"))
		enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_INACTIVE;
	else if (!strcmp (str_state, "SCANNING"))
		enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_SCANNING;
	else if (!strcmp (str_state, "ASSOCIATING"))
		enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_ASSOCIATING;
	else if (!strcmp (str_state, "ASSOCIATED"))
		enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_ASSOCIATED;
	else if (!strcmp (str_state, "4WAY_HANDSHAKE"))
		enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_4WAY_HANDSHAKE;
	else if (!strcmp (str_state, "GROUP_HANDSHAKE"))
		enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_GROUP_HANDSHAKE;
	else if (!strcmp (str_state, "COMPLETED"))
		enum_state = NM_SUPPLICANT_INTERFACE_CON_STATE_COMPLETED;

	return enum_state;
}

static void
wpas_iface_handle_state_change (DBusGProxy *proxy,
                                const char *str_new_state,
                                const char *str_old_state,
                                gpointer user_data)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (user_data);
	guint32 old_state, enum_new_state;

	enum_new_state = wpas_state_string_to_enum (str_new_state);
	old_state = priv->con_state;
	priv->con_state = enum_new_state;
	if (priv->con_state != old_state) {
		g_signal_emit (user_data,
		               nm_supplicant_interface_signals[CONNECTION_STATE],
		               0,
		               priv->con_state,
		               old_state);
	}
}

static void
wpas_iface_get_state (NMSupplicantInterface *self)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	GValue value = {0,};
	gboolean result;

	result = _wpas_get_property(priv->iface_proxy, WPAS_DBUS_IFACE_INTERFACE, "State", &value);

	if (result) {
		const char *state_str = g_value_get_string (&value);
		priv->con_state = wpas_state_string_to_enum (state_str);
		nm_supplicant_interface_set_state (self, NM_SUPPLICANT_INTERFACE_STATE_READY);
		g_value_unset (&value);
	} else {
		nm_warning ("could not get interface state");
	}

}

static void
foreach_capability_cb (gpointer key, gpointer value, gpointer user_data)
{
	GValue *variant = (GValue *) value;
	guint32 *capas = (guint32 *) user_data;

	if (!strcmp (key, "Group")) {
		char** array = g_value_get_boxed (variant);
		int i=0;
		while (array[i]) {
			if (!strcmp (array[i], "ccmp"))
				*capas |= NM_WIFI_DEVICE_CAP_CIPHER_CCMP;
			else if (!strcmp (array[i], "tkip"))
				*capas |= NM_WIFI_DEVICE_CAP_CIPHER_TKIP;
			else if (!strcmp (array[i], "wep104"))
				*capas |= NM_WIFI_DEVICE_CAP_CIPHER_WEP40;
			else if (!strcmp (array[i], "wep40"))
				*capas |= NM_WIFI_DEVICE_CAP_CIPHER_WEP104;
			i++;
		}
	} else if (!strcmp (key, "Protocol")) {
		char** array = g_value_get_boxed (variant);
		int i=0;
		while (array[i]) {
			if (!strcmp (array[i], "wpa"))
				*capas |= NM_WIFI_DEVICE_CAP_WPA;
			else if (!strcmp (array[i], "rsn"))
				*capas |= NM_WIFI_DEVICE_CAP_RSN;
			i++;
		}
	} else if (!strcmp (key, "Modes")) {
		char** array = g_value_get_boxed (variant);
		int i=0;
		while (array[i]) {
			if (!strcmp (array[i], "ap"))
				*capas |= NM_WIFI_DEVICE_CAP_MODE_AP;
			i++;
		}
	}
}

#define WPA_CAPS (NM_WIFI_DEVICE_CAP_CIPHER_TKIP | \
                  NM_WIFI_DEVICE_CAP_CIPHER_CCMP | \
                  NM_WIFI_DEVICE_CAP_WPA | \
                  NM_WIFI_DEVICE_CAP_RSN)

static void
wpas_iface_get_capabilities (NMSupplicantInterface *self)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	GValue value = {0,};
	GHashTable *hash;
	gboolean result;
	guint32 caps = NM_WIFI_DEVICE_CAP_NONE;

	result = _wpas_get_property(priv->iface_proxy, WPAS_DBUS_IFACE_INTERFACE, "Capabilities", &value);

	if (result) {
		hash = g_value_get_boxed (&value);
		g_hash_table_foreach (hash, foreach_capability_cb, &caps);

		/* Check for cipher support but not WPA support */
		if (    (caps & (NM_WIFI_DEVICE_CAP_CIPHER_TKIP | NM_WIFI_DEVICE_CAP_CIPHER_CCMP))
		    && !(caps & (NM_WIFI_DEVICE_CAP_WPA | NM_WIFI_DEVICE_CAP_RSN))) {
			nm_warning ("%s: device supports WPA ciphers but not WPA protocol; "
			            "WPA unavailable.", priv->dev);
			caps &= ~WPA_CAPS;
		}

		/* Check for WPA support but not cipher support */
		if (    (caps & (NM_WIFI_DEVICE_CAP_WPA | NM_WIFI_DEVICE_CAP_RSN))
		    && !(caps & (NM_WIFI_DEVICE_CAP_CIPHER_TKIP | NM_WIFI_DEVICE_CAP_CIPHER_CCMP))) {
			nm_warning ("%s: device supports WPA protocol but not WPA ciphers; "
			            "WPA unavailable.", priv->dev);
			caps &= ~WPA_CAPS;
		}

		priv->capabilities = caps;
		g_value_unset (&value);
	} else {
		nm_warning ("could not get Capabilities property");
	}
}

static void
wpas_iface_get_scanning (NMSupplicantInterface *self)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	GValue value = {0,};
	gboolean result;
	gboolean scanning;

	result = _wpas_get_property(priv->iface_proxy, WPAS_DBUS_IFACE_INTERFACE, "Scanning", &value);

	if (result) {
		scanning = g_value_get_boolean (&value);
		if (scanning != priv->scanning) {
			priv->scanning = scanning;
			g_object_notify (G_OBJECT (self), "scanning");
		}
		g_value_unset (&value);
	} else {
		nm_warning ("could not get scanning property");
	}
}

static void
wpas_iface_handle_scanning (DBusGProxy *proxy,
                            gboolean scanning,
                            gpointer user_data)
{
	NMSupplicantInterface *self = NM_SUPPLICANT_INTERFACE (user_data);
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);

	if (scanning != priv->scanning) {
		priv->scanning = scanning;
		g_object_notify (G_OBJECT (self), "scanning");
	}
}

gboolean
nm_supplicant_interface_get_scanning (NMSupplicantInterface *self)
{
	NMSupplicantInterfacePrivate *priv;

	g_return_val_if_fail (self != NULL, FALSE);

	priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	if (priv->scanning)
		return TRUE;
	if (priv->con_state == NM_SUPPLICANT_INTERFACE_CON_STATE_SCANNING)
		return TRUE;
	return FALSE;
}

static void
handle_property_changed (gpointer key, gpointer data, gpointer user_data)
{
	NMSupplicantInterface *self = NM_SUPPLICANT_INTERFACE (user_data);
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	char *prop_name = key;

	if (!strcmp(prop_name, "Scanning")) {
		gboolean scanning = g_value_get_boolean ((GValue *) data);
		wpas_iface_handle_scanning (priv->iface_proxy, scanning, self);
	}

	else {
#if DEBUG
		g_warning ("Property '%s' unhandled.", prop_name);
#endif
	}
}

static void
properties_changed_proxy (DBusGProxy *proxy,
                          GHashTable *properties,
                          gpointer user_data)
{
	g_hash_table_foreach (properties, handle_property_changed, user_data);
}

static void
nm_supplicant_interface_add_cb (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	NMSupplicantInfo *info = (NMSupplicantInfo *) user_data;
	GError *err = NULL;
	char *path = NULL;

	if (!dbus_g_proxy_end_call (proxy, call_id, &err,
	                            DBUS_TYPE_G_OBJECT_PATH, &path,
	                            G_TYPE_INVALID)) {

		if (dbus_g_error_has_name (err, WPAS_ERROR_IFACE_UNKNOWN)) {
			/* Interface not added, try to add it */
			nm_supplicant_interface_add_to_supplicant (info->interface, FALSE);
		} else if (dbus_g_error_has_name (err, WPAS_ERROR_INTERFACE_EXISTS)) {
			/* Interface already added, just try to get the interface */
			nm_supplicant_interface_add_to_supplicant (info->interface, TRUE);
		} else {
			nm_warning ("Unexpected supplicant error getting interface: %s", err->message);
		}

		g_error_free (err);
	} else {
		NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (info->interface);

		priv->object_path = path;

		priv->iface_proxy = dbus_g_proxy_new_for_name (nm_dbus_manager_get_connection (priv->dbus_mgr),
		                                               WPAS_DBUS_SERVICE,
		                                               path,
		                                               WPAS_DBUS_IFACE_INTERFACE);

		dbus_g_proxy_add_signal (priv->iface_proxy, "ScanDone", G_TYPE_BOOLEAN, G_TYPE_INVALID);

		dbus_g_object_register_marshaller (_nm_marshal_VOID__STRING_STRING,
		                                   G_TYPE_NONE,
		                                   G_TYPE_STRING, G_TYPE_STRING,
		                                   G_TYPE_INVALID);

		dbus_g_proxy_add_signal (priv->iface_proxy, "StateChanged", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

		dbus_g_proxy_connect_signal (priv->iface_proxy, "ScanDone",
		                             G_CALLBACK (wpas_iface_query_scan_results),
		                             info->interface,
		                             NULL);

		dbus_g_proxy_connect_signal (priv->iface_proxy, "StateChanged",
		                             G_CALLBACK (wpas_iface_handle_state_change),
		                             info->interface,
		                             NULL);

		dbus_g_proxy_add_signal (priv->iface_proxy, "PropertiesChanged", DBUS_TYPE_G_MAP_OF_VARIANT, G_TYPE_INVALID);

		dbus_g_proxy_connect_signal (priv->iface_proxy, "PropertiesChanged",
								G_CALLBACK (properties_changed_proxy),
								info->interface,
								NULL);

		/* Interface added to the supplicant; get its initial state. */
		wpas_iface_get_state (info->interface);
		wpas_iface_get_scanning (info->interface);
		wpas_iface_get_capabilities (info->interface);
	}
}

static void
nm_supplicant_interface_add_to_supplicant (NMSupplicantInterface * self,
                                           gboolean get_only)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	NMSupplicantInfo *info;
	DBusGProxy *proxy;
	DBusGProxyCall *call;

	proxy = dbus_g_proxy_new_for_name (nm_dbus_manager_get_connection (priv->dbus_mgr),
	                                   WPAS_DBUS_SERVICE,
	                                   WPAS_DBUS_PATH,
	                                   WPAS_DBUS_INTERFACE);
	info = nm_supplicant_info_new (self, proxy, priv->other_pcalls);

	if (get_only) {
		call = dbus_g_proxy_begin_call (proxy, "GetInterface",
		                                nm_supplicant_interface_add_cb,
		                                info,
		                                nm_supplicant_info_destroy,
		                                G_TYPE_STRING, priv->dev,
		                                G_TYPE_INVALID);
	} else {
		GHashTable *hash = g_hash_table_new (g_str_hash, g_str_equal);
		GValue *driver;
		GValue *ifname;

		driver = g_new0 (GValue, 1);
		g_value_init (driver, G_TYPE_STRING);
		g_value_set_string (driver, priv->is_wireless ? "wext" : "wired");
		g_hash_table_insert (hash, "Driver", driver);

		ifname = g_new0 (GValue, 1);
		g_value_init (ifname, G_TYPE_STRING);
		g_value_set_string (ifname, priv->dev);
		g_hash_table_insert (hash, "Ifname", ifname);

		call = dbus_g_proxy_begin_call (proxy, "CreateInterface",
		                                nm_supplicant_interface_add_cb,
		                                info,
		                                nm_supplicant_info_destroy,
		                                DBUS_TYPE_G_MAP_OF_VARIANT, hash,
		                                G_TYPE_INVALID);

		g_value_unset (driver);
		g_free (driver);
		g_value_unset (ifname);
		g_free (ifname);
		g_hash_table_destroy (hash);
	}

	g_object_unref (proxy);

	nm_supplicant_info_set_call (info, call);
}

static void
nm_supplicant_interface_start (NMSupplicantInterface * self)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	guint32          state;

	/* Can only start the interface from INIT state */
	g_return_if_fail (priv->state == NM_SUPPLICANT_INTERFACE_STATE_INIT);

	state = nm_supplicant_manager_get_state (priv->smgr);
	if (state == NM_SUPPLICANT_MANAGER_STATE_IDLE) {
		nm_supplicant_interface_set_state (self, NM_SUPPLICANT_INTERFACE_STATE_STARTING);
		nm_supplicant_interface_add_to_supplicant (self, FALSE);
	} else if (state == NM_SUPPLICANT_MANAGER_STATE_DOWN) {
		/* Don't do anything; wait for signal from supplicant manager
		 * that its state has changed.
		 */
	} else
		nm_warning ("Unknown supplicant manager state!");
}

static void
nm_supplicant_interface_handle_supplicant_manager_idle_state (NMSupplicantInterface * self)
{
	switch (NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self)->state) {
		case NM_SUPPLICANT_INTERFACE_STATE_INIT:
			/* Move to STARTING state when supplicant is ready */
			nm_supplicant_interface_start (self);
			break;
		case NM_SUPPLICANT_INTERFACE_STATE_STARTING:
			/* Don't do anything here, though we should never hit this */
			break;
		case NM_SUPPLICANT_INTERFACE_STATE_READY:
			/* Don't do anything here, though we should never hit this */
			break;
		case NM_SUPPLICANT_INTERFACE_STATE_DOWN:
			/* Don't do anything here; interface can't get out of DOWN state */
			break;
		default:
			nm_warning ("Unknown supplicant interface state!");
			break;
	}
}


static void
nm_supplicant_interface_set_state (NMSupplicantInterface * self,
                                   guint32 new_state)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	guint32 old_state;

	g_return_if_fail (new_state < NM_SUPPLICANT_INTERFACE_STATE_LAST);

	if (new_state == priv->state)
		return;

	old_state = priv->state;
	if (new_state == NM_SUPPLICANT_INTERFACE_STATE_DOWN) {
		/* If the interface is transitioning to DOWN and there's are
		 * in-progress pending calls, cancel them.
		 */
		cancel_all_callbacks (priv->other_pcalls);
		cancel_all_callbacks (priv->assoc_pcalls);
	}

	priv->state = new_state;
	g_signal_emit (self,
	               nm_supplicant_interface_signals[STATE],
	               0,
	               priv->state,
	               old_state);
}

static void
nm_supplicant_interface_smgr_state_changed (NMSupplicantManager * smgr,
                                            guint32 new_state,
                                            guint32 old_state,
                                            gpointer user_data)
{
	NMSupplicantInterface * self = NM_SUPPLICANT_INTERFACE (user_data);

	switch (new_state) {
		case NM_SUPPLICANT_MANAGER_STATE_DOWN:
			/* The supplicant went away, likely the connection to it is also
			 * gone.  Therefore, this interface must move to the DOWN state
			 * and be disposed of.
			 */
			nm_supplicant_interface_set_state (self, NM_SUPPLICANT_INTERFACE_STATE_DOWN);
			break;
		case NM_SUPPLICANT_MANAGER_STATE_IDLE:
			/* Handle the supplicant now being available. */
			nm_supplicant_interface_handle_supplicant_manager_idle_state (self);
			break;
		default:
			nm_warning ("Unknown supplicant manager state!");
			break;
	}
}


static void
remove_network_cb (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	GError *err = NULL;

	if (!dbus_g_proxy_end_call (proxy, call_id, &err, G_TYPE_INVALID)) {
		nm_warning ("Couldn't remove network from supplicant interface: %s.", err->message);
		g_error_free (err);
	}
}

static void
disconnect_cb  (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	GError *err = NULL;

	if (!dbus_g_proxy_end_call (proxy, call_id, &err, G_TYPE_INVALID)) {
		nm_warning ("Couldn't disconnect supplicant interface: %s.", err->message);
		g_error_free (err);
	}
}

void
nm_supplicant_interface_disconnect (NMSupplicantInterface * self)
{
	NMSupplicantInterfacePrivate *priv;

	g_return_if_fail (NM_IS_SUPPLICANT_INTERFACE (self));

	priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);

	/* Clear and cancel all pending calls related to a prior
	 * connection attempt.
	 */
	cancel_all_callbacks (priv->assoc_pcalls);

	/* Don't do anything if there is no connection to the supplicant yet. */
	if (!priv->iface_proxy)
		return;

	/* Don't try to disconnect if the supplicant interface is already
	 * disconnected.
	 */
	if (priv->con_state == NM_SUPPLICANT_INTERFACE_CON_STATE_DISCONNECTED
	    || priv->con_state == NM_SUPPLICANT_INTERFACE_CON_STATE_INACTIVE) {
		if (priv->net_proxy) {
			g_object_unref (priv->net_proxy);
			priv->net_proxy = NULL;
		}

		return;
	}

	/* Remove any network that was added by NetworkManager */
	if (priv->net_proxy) {
		dbus_g_proxy_begin_call (priv->iface_proxy, "RemoveNetwork",
		                         remove_network_cb,
		                         NULL, NULL,
		                         DBUS_TYPE_G_OBJECT_PATH, dbus_g_proxy_get_path (priv->net_proxy),
		                         G_TYPE_INVALID);

		g_object_unref (priv->net_proxy);
		priv->net_proxy = NULL;
	}

	dbus_g_proxy_begin_call (priv->iface_proxy, "Disconnect",
	                         disconnect_cb,
	                         NULL, NULL,
	                         G_TYPE_INVALID);
}

static void
select_network_cb (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	NMSupplicantInfo *info = (NMSupplicantInfo *) user_data;
	GError *err = NULL;

	if (!dbus_g_proxy_end_call (proxy, call_id, &err, G_TYPE_INVALID)) {
		nm_warning ("Couldn't select network config: %s.", err->message);
		emit_error_helper (info->interface, err);
		g_error_free (err);
	}
}

static void
call_select_network (NMSupplicantInfo *info)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (info->interface);
	DBusGProxyCall *call;

	info = nm_supplicant_info_new (info->interface,
	                               priv->iface_proxy,
	                               priv->assoc_pcalls);

	call = dbus_g_proxy_begin_call (priv->iface_proxy, "SelectNetwork",
	                                select_network_cb,
	                                info,
	                                nm_supplicant_info_destroy,
	                                DBUS_TYPE_G_OBJECT_PATH, dbus_g_proxy_get_path (priv->net_proxy),
	                                G_TYPE_INVALID);
	nm_supplicant_info_set_call (info, call);
}

static void
add_blob_cb (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	NMSupplicantInfo *info = (NMSupplicantInfo *) user_data;
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (info->interface);
	GError *err = NULL;

	if (!dbus_g_proxy_end_call (proxy, call_id, &err, G_TYPE_INVALID)) {
		nm_warning ("Couldn't add network blob: %s.", err->message);
		emit_error_helper (info->interface, err);
		g_error_free (err);
	}

	priv->pending_blobs--;
	if(priv->pending_blobs == 0) {
		call_select_network (info);
	}
}

static void
call_add_blob (const char *key, const GByteArray *value, NMSupplicantInterface * self)
{
	NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);
	DBusGProxyCall *call;
	NMSupplicantInfo *info;

	priv->pending_blobs++;

	info = nm_supplicant_info_new (self, priv->iface_proxy, priv->other_pcalls);
	call = dbus_g_proxy_begin_call (priv->iface_proxy, "AddBlob",
	                                add_blob_cb,
	                                info,
	                                nm_supplicant_info_destroy,
	                                G_TYPE_STRING, key,
	                                DBUS_TYPE_G_UCHAR_ARRAY, value,
	                                G_TYPE_INVALID);
	nm_supplicant_info_set_call (info, call);
}

static void
wpas_set_blobs (NMSupplicantInfo *info, GHashTable *orig_blobs)
{
	g_hash_table_foreach (orig_blobs, (GHFunc) call_add_blob, info->interface);
}

static void
add_network_cb (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	NMSupplicantInfo *info = (NMSupplicantInfo *) user_data;
	GError *err = NULL;
	char *path = NULL;

	if (!dbus_g_proxy_end_call (proxy, call_id, &err,
	                            DBUS_TYPE_G_OBJECT_PATH, &path,
	                            G_TYPE_INVALID)) {
		nm_warning ("Couldn't add a network to the supplicant interface: %s.", err->message);
		emit_error_helper (info->interface, err);
		g_error_free (err);
	} else {
		NMSupplicantInterfacePrivate *priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (info->interface);
		GHashTable *blobs;

		priv->net_proxy = dbus_g_proxy_new_for_name (nm_dbus_manager_get_connection (priv->dbus_mgr),
		                                             WPAS_DBUS_SERVICE,
		                                             path,
		                                             WPAS_DBUS_IFACE_NETWORK);
		g_free (path);

		/* Send any blobs first; if there aren't any jump to selecting the
		 * network.
		 */
		blobs = nm_supplicant_config_get_blobs (priv->cfg);
		if (g_hash_table_size (blobs) > 0)
			wpas_set_blobs (info, blobs);
		else
			call_select_network (info);
	}
}

gboolean
nm_supplicant_interface_set_config (NMSupplicantInterface * self,
                                    NMSupplicantConfig * cfg)
{
	NMSupplicantInterfacePrivate *priv;
	guint32 ap_scan;
	GHashTable * config_hash;
	GValue value = {0,};
	NMSupplicantInfo *info;
	DBusGProxyCall *call;

	g_return_val_if_fail (NM_IS_SUPPLICANT_INTERFACE (self), FALSE);

	priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);

	nm_supplicant_interface_disconnect (self);

	if (priv->cfg)
		g_object_unref (priv->cfg);
	priv->cfg = cfg;

	if (cfg == NULL)
		return TRUE;

	g_object_ref (priv->cfg);

	ap_scan = nm_supplicant_config_get_ap_scan (priv->cfg);

	g_value_init (&value, G_TYPE_UINT);
	g_value_set_uint (&value, ap_scan);

	_wpas_set_property (priv->iface_proxy, WPAS_DBUS_IFACE_INTERFACE, "ApScan", &value);

	g_value_unset (&value);

	nm_info ("Config: set interface ap_scan to %d", ap_scan);

	config_hash = nm_supplicant_config_get_hash (priv->cfg);
	info = nm_supplicant_info_new (self, priv->iface_proxy, priv->other_pcalls);
	call = dbus_g_proxy_begin_call (priv->iface_proxy, "AddNetwork",
									add_network_cb,
									info,
									nm_supplicant_info_destroy,
									DBUS_TYPE_G_MAP_OF_VARIANT, config_hash,
									G_TYPE_INVALID);
	nm_supplicant_info_set_call (info, call);

	g_hash_table_destroy (config_hash);

	return call != NULL;

}

const char *
nm_supplicant_interface_get_device (NMSupplicantInterface * self)
{
	g_return_val_if_fail (NM_IS_SUPPLICANT_INTERFACE (self), NULL);

	return NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self)->dev;
}

static void
scan_request_cb (DBusGProxy *proxy, DBusGProxyCall *call_id, gpointer user_data)
{
	NMSupplicantInfo *info = (NMSupplicantInfo *) user_data;
	GError *err = NULL;
	guint32 success = 1;

	if (!dbus_g_proxy_end_call (proxy, call_id, &err,
	                            G_TYPE_INVALID)) {
		nm_warning  ("Could not get scan request result: %s", err->message);
		g_error_free (err);
	}

	/* Notify listeners of the result of the scan */
	g_signal_emit (info->interface,
	               nm_supplicant_interface_signals[SCAN_REQ_RESULT],
	               0,
	               success ? TRUE : FALSE);
}

gboolean
nm_supplicant_interface_request_scan (NMSupplicantInterface * self)
{
	NMSupplicantInterfacePrivate *priv;
	NMSupplicantInfo *info;
	DBusGProxyCall *call;
	GHashTable *hash = g_hash_table_new (g_str_hash, g_str_equal);
	GValue *scan_type;

	g_return_val_if_fail (NM_IS_SUPPLICANT_INTERFACE (self), FALSE);

	priv = NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self);

	scan_type = g_new0 (GValue, 1);
	g_value_init (scan_type, G_TYPE_STRING);
	g_value_set_string (scan_type, "passive");
	g_hash_table_insert (hash, "Type", scan_type);

	info = nm_supplicant_info_new (self, priv->iface_proxy, priv->other_pcalls);
	call = dbus_g_proxy_begin_call (priv->iface_proxy, "Scan",
	                                scan_request_cb,
	                                info,
	                                nm_supplicant_info_destroy,
	                                DBUS_TYPE_G_MAP_OF_VARIANT, hash,
	                                G_TYPE_INVALID);
	nm_supplicant_info_set_call (info, call);

	g_value_unset (scan_type);
	g_free (scan_type);
	g_hash_table_destroy (hash);

	return call != NULL;
}

guint32
nm_supplicant_interface_get_state (NMSupplicantInterface * self)
{
	g_return_val_if_fail (NM_IS_SUPPLICANT_INTERFACE (self), NM_SUPPLICANT_INTERFACE_STATE_DOWN);

	return NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self)->state;
}

guint32
nm_supplicant_interface_get_capabilities (NMSupplicantInterface * self)
{
	g_return_val_if_fail (NM_IS_SUPPLICANT_INTERFACE (self), NM_DEVICE_CAP_NONE);

	return NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self)->capabilities;
}

guint32
nm_supplicant_interface_get_connection_state (NMSupplicantInterface * self)
{
	g_return_val_if_fail (NM_IS_SUPPLICANT_INTERFACE (self), NM_SUPPLICANT_INTERFACE_CON_STATE_DISCONNECTED);

	return NM_SUPPLICANT_INTERFACE_GET_PRIVATE (self)->con_state;
}

const char *
nm_supplicant_interface_state_to_string (guint32 state)
{
	switch (state) {
	case NM_SUPPLICANT_INTERFACE_STATE_INIT:
		return "init";
	case NM_SUPPLICANT_INTERFACE_STATE_STARTING:
		return "starting";
	case NM_SUPPLICANT_INTERFACE_STATE_READY:
		return "ready";
	case NM_SUPPLICANT_INTERFACE_STATE_DOWN:
		return "down";
	default:
		break;
	}
	return "unknown";
}

const char *
nm_supplicant_interface_connection_state_to_string (guint32 state)
{
	switch (state) {
	case NM_SUPPLICANT_INTERFACE_CON_STATE_DISCONNECTED:
		return "disconnected";
	case NM_SUPPLICANT_INTERFACE_CON_STATE_INACTIVE:
		return "inactive";
	case NM_SUPPLICANT_INTERFACE_CON_STATE_SCANNING:
		return "scanning";
	case NM_SUPPLICANT_INTERFACE_CON_STATE_ASSOCIATING:
		return "associating";
	case NM_SUPPLICANT_INTERFACE_CON_STATE_ASSOCIATED:
		return "associated";
	case NM_SUPPLICANT_INTERFACE_CON_STATE_4WAY_HANDSHAKE:
		return "4-way handshake";
	case NM_SUPPLICANT_INTERFACE_CON_STATE_GROUP_HANDSHAKE:
		return "group handshake";
	case NM_SUPPLICANT_INTERFACE_CON_STATE_COMPLETED:
		return "completed";
	default:
		break;
	}
	return "unknown";
}

