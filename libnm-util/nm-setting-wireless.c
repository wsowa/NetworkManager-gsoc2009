/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

/*
 * Dan Williams <dcbw@redhat.com>
 * Tambet Ingo <tambet@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2007 - 2008 Red Hat, Inc.
 * (C) Copyright 2007 - 2008 Novell, Inc.
 */

#include <string.h>
#include <netinet/ether.h>
#include <dbus/dbus-glib.h>

#include "wireless-helper.h"

#include "NetworkManager.h"
#include "nm-setting-wireless.h"
#include "nm-param-spec-specialized.h"
#include "nm-utils.h"
#include "nm-dbus-glib-types.h"
#include "nm-utils-private.h"

GQuark
nm_setting_wireless_error_quark (void)
{
	static GQuark quark;

	if (G_UNLIKELY (!quark))
		quark = g_quark_from_static_string ("nm-setting-wireless-error-quark");
	return quark;
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
nm_setting_wireless_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			/* Unknown error. */
			ENUM_ENTRY (NM_SETTING_WIRELESS_ERROR_UNKNOWN, "UnknownError"),
			/* The specified property was invalid. */
			ENUM_ENTRY (NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY, "InvalidProperty"),
			/* The specified property was missing and is required. */
			ENUM_ENTRY (NM_SETTING_WIRELESS_ERROR_MISSING_PROPERTY, "MissingProperty"),
			/* The required security setting is missing */
			ENUM_ENTRY (NM_SETTING_WIRELESS_ERROR_MISSING_SECURITY_SETTING, "MissingSecuritySetting"),
			/* The 'channel' property requires a valid 'band' */
			ENUM_ENTRY (NM_SETTING_WIRELESS_ERROR_CHANNEL_REQUIRES_BAND, "ChannelRequiresBand"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("NMSettingWirelessError", values);
	}
	return etype;
}


G_DEFINE_TYPE (NMSettingWireless, nm_setting_wireless, NM_TYPE_SETTING)

#define NM_SETTING_WIRELESS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_WIRELESS, NMSettingWirelessPrivate))

typedef struct {
	GByteArray *ssid;
	char *mode;
	char *band;
	guint32 channel;
	GByteArray *bssid;
	guint32 rate;
	guint32 tx_power;
	GByteArray *mac_address;
	guint32 mtu;
	GSList *seen_bssids;
	char *security;
} NMSettingWirelessPrivate;

enum {
	PROP_0,
	PROP_SSID,
	PROP_MODE,
	PROP_BAND,
	PROP_CHANNEL,
	PROP_BSSID,
	PROP_RATE,
	PROP_TX_POWER,
	PROP_MAC_ADDRESS,
	PROP_MTU,
	PROP_SEEN_BSSIDS,
	PROP_SEC,

	LAST_PROP
};

static gboolean
match_cipher (const char *cipher,
              const char *expected,
              guint32 wpa_flags,
              guint32 rsn_flags,
              guint32 flag)
{
	if (strcmp (cipher, expected) != 0)
		return FALSE;

	if (!(wpa_flags & flag) && !(rsn_flags & flag))
		return FALSE;

	return TRUE;
}

gboolean
nm_setting_wireless_ap_security_compatible (NMSettingWireless *s_wireless,
								    NMSettingWirelessSecurity *s_wireless_sec,
								    guint32 ap_flags,
								    guint32 ap_wpa,
								    guint32 ap_rsn,
								    guint32 ap_mode)
{
	NMSettingWirelessPrivate *priv;
	const char *key_mgmt = NULL, *cipher;
	guint32 num, i;
	gboolean found = FALSE;

	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (s_wireless), FALSE);

	priv = NM_SETTING_WIRELESS_GET_PRIVATE (s_wireless);

	if (!priv->security) {
		if (   (ap_flags & NM_802_11_AP_FLAGS_PRIVACY)
		    || (ap_wpa != NM_802_11_AP_SEC_NONE)
		    || (ap_rsn != NM_802_11_AP_SEC_NONE))
			return FALSE;
		return TRUE;
	}

	if (strcmp (priv->security, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME) != 0)
		return FALSE;

	if (s_wireless_sec)
		key_mgmt = nm_setting_wireless_security_get_key_mgmt (s_wireless_sec);

	if (s_wireless_sec == NULL || !key_mgmt)
		return FALSE;

	/* Static WEP */
	if (!strcmp (key_mgmt, "none")) {
		if (   !(ap_flags & NM_802_11_AP_FLAGS_PRIVACY)
		    || (ap_wpa != NM_802_11_AP_SEC_NONE)
		    || (ap_rsn != NM_802_11_AP_SEC_NONE))
			return FALSE;
		return TRUE;
	}

	/* Adhoc WPA */
	if (!strcmp (key_mgmt, "wpa-none")) {
		if (ap_mode != NM_802_11_MODE_ADHOC)
			return FALSE;
		// FIXME: validate ciphers if the BSSID actually puts WPA/RSN IE in
		// it's beacon
		return TRUE;
	}

	/* Stuff after this point requires infrastructure */
	if (ap_mode != NM_802_11_MODE_INFRA)
		return FALSE;

	/* Dynamic WEP or LEAP */
	if (!strcmp (key_mgmt, "ieee8021x")) {
		if (!(ap_flags & NM_802_11_AP_FLAGS_PRIVACY))
			return FALSE;

		/* If the AP is advertising a WPA IE, make sure it supports WEP ciphers */
		if (ap_wpa != NM_802_11_AP_SEC_NONE) {
			if (!(ap_wpa & NM_802_11_AP_SEC_KEY_MGMT_802_1X))
				return FALSE;

			/* quick check; can't use AP if it doesn't support at least one
			 * WEP cipher in both pairwise and group suites.
			 */
			if (   !(ap_wpa & (NM_802_11_AP_SEC_PAIR_WEP40 | NM_802_11_AP_SEC_PAIR_WEP104))
			    || !(ap_wpa & (NM_802_11_AP_SEC_GROUP_WEP40 | NM_802_11_AP_SEC_GROUP_WEP104)))
				return FALSE;

			/* Match at least one pairwise cipher with AP's capability if the
			 * wireless-security setting explicitly lists pairwise ciphers
			 */
			num = nm_setting_wireless_security_get_num_pairwise (s_wireless_sec);
			for (i = 0, found = FALSE; i < num; i++) {
				cipher = nm_setting_wireless_security_get_pairwise (s_wireless_sec, i);
				if ((found = match_cipher (cipher, "wep40", ap_wpa, ap_wpa, NM_802_11_AP_SEC_PAIR_WEP40)))
					break;
				if ((found = match_cipher (cipher, "wep104", ap_wpa, ap_wpa, NM_802_11_AP_SEC_PAIR_WEP104)))
					break;
			}
			if (!found && num)
				return FALSE;

			/* Match at least one group cipher with AP's capability if the
			 * wireless-security setting explicitly lists group ciphers
			 */
			num = nm_setting_wireless_security_get_num_groups (s_wireless_sec);
			for (i = 0, found = FALSE; i < num; i++) {
				cipher = nm_setting_wireless_security_get_group (s_wireless_sec, i);
				if ((found = match_cipher (cipher, "wep40", ap_wpa, ap_wpa, NM_802_11_AP_SEC_GROUP_WEP40)))
					break;
				if ((found = match_cipher (cipher, "wep104", ap_wpa, ap_wpa, NM_802_11_AP_SEC_GROUP_WEP104)))
					break;
			}
			if (!found && num)
				return FALSE;
		}
		return TRUE;
	}

	/* WPA[2]-PSK and WPA[2] Enterprise */
	if (   !strcmp (key_mgmt, "wpa-psk")
	    || !strcmp (key_mgmt, "wpa-eap")) {

		if (!strcmp (key_mgmt, "wpa-psk")) {
			if (   !(ap_wpa & NM_802_11_AP_SEC_KEY_MGMT_PSK)
			    && !(ap_rsn & NM_802_11_AP_SEC_KEY_MGMT_PSK))
				return FALSE;
		} else if (!strcmp (key_mgmt, "wpa-eap")) {
			if (   !(ap_wpa & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
			    && !(ap_rsn & NM_802_11_AP_SEC_KEY_MGMT_802_1X))
				return FALSE;
		}

		// FIXME: should handle WPA and RSN separately here to ensure that
		// if the Connection only uses WPA we don't match a cipher against
		// the AP's RSN IE instead

		/* Match at least one pairwise cipher with AP's capability if the
		 * wireless-security setting explicitly lists pairwise ciphers
		 */
		num = nm_setting_wireless_security_get_num_pairwise (s_wireless_sec);
		for (i = 0, found = FALSE; i < num; i++) {
			cipher = nm_setting_wireless_security_get_pairwise (s_wireless_sec, i);
			if ((found = match_cipher (cipher, "tkip", ap_wpa, ap_rsn, NM_802_11_AP_SEC_PAIR_TKIP)))
				break;
			if ((found = match_cipher (cipher, "ccmp", ap_wpa, ap_rsn, NM_802_11_AP_SEC_PAIR_CCMP)))
				break;
		}
		if (!found && num)
			return FALSE;

		/* Match at least one group cipher with AP's capability if the
		 * wireless-security setting explicitly lists group ciphers
		 */
		num = nm_setting_wireless_security_get_num_groups (s_wireless_sec);
		for (i = 0, found = FALSE; i < num; i++) {
			cipher = nm_setting_wireless_security_get_group (s_wireless_sec, i);

			if ((found = match_cipher (cipher, "wep40", ap_wpa, ap_rsn, NM_802_11_AP_SEC_GROUP_WEP40)))
				break;
			if ((found = match_cipher (cipher, "wep104", ap_wpa, ap_rsn, NM_802_11_AP_SEC_GROUP_WEP104)))
				break;
			if ((found = match_cipher (cipher, "tkip", ap_wpa, ap_rsn, NM_802_11_AP_SEC_GROUP_TKIP)))
				break;
			if ((found = match_cipher (cipher, "ccmp", ap_wpa, ap_rsn, NM_802_11_AP_SEC_GROUP_CCMP)))
				break;
		}
		if (!found && num)
			return FALSE;

		return TRUE;
	}

	return FALSE;
}

NMSetting *
nm_setting_wireless_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_WIRELESS, NULL);
}

const GByteArray *
nm_setting_wireless_get_ssid (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), NULL);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->ssid;
}

const char *
nm_setting_wireless_get_mode (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), NULL);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->mode;
}

const char *
nm_setting_wireless_get_band (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), NULL);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->band;
}

guint32
nm_setting_wireless_get_channel (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), 0);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->channel;
}

const GByteArray *
nm_setting_wireless_get_bssid (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), NULL);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->bssid;
}

guint32
nm_setting_wireless_get_rate (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), 0);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->rate;
}

guint32
nm_setting_wireless_get_tx_power (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), 0);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->tx_power;
}

const GByteArray *
nm_setting_wireless_get_mac_address (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), NULL);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->mac_address;
}

guint32
nm_setting_wireless_get_mtu (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), 0);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->mtu;
}

const char *
nm_setting_wireless_get_security (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), NULL);

	return NM_SETTING_WIRELESS_GET_PRIVATE (setting)->security;
}

gboolean
nm_setting_wireless_add_seen_bssid (NMSettingWireless *setting,
									const char *bssid)
{
	NMSettingWirelessPrivate *priv;
	char *lower_bssid;
	GSList *iter;
	gboolean found = FALSE;

	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), FALSE);
	g_return_val_if_fail (bssid != NULL, FALSE);

	lower_bssid = g_ascii_strdown (bssid, -1);
	if (!lower_bssid)
		return FALSE;

	priv = NM_SETTING_WIRELESS_GET_PRIVATE (setting);

	for (iter = priv->seen_bssids; iter; iter = iter->next) {
		if (!strcmp ((char *) iter->data, lower_bssid)) {
			found = TRUE;
			break;
		}
	}

	if (!found)
		priv->seen_bssids = g_slist_prepend (priv->seen_bssids, lower_bssid);
	else
		g_free (lower_bssid);

	return !found;
}

guint32
nm_setting_wireless_get_num_seen_bssids (NMSettingWireless *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), 0);

	return g_slist_length (NM_SETTING_WIRELESS_GET_PRIVATE (setting)->seen_bssids);
}

const char *
nm_setting_wireless_get_seen_bssid (NMSettingWireless *setting,
									guint32 i)
{
	g_return_val_if_fail (NM_IS_SETTING_WIRELESS (setting), NULL);

	return (const char *) g_slist_nth_data (NM_SETTING_WIRELESS_GET_PRIVATE (setting)->seen_bssids, i);
}

static gint
find_setting_by_name (gconstpointer a, gconstpointer b)
{
	NMSetting *setting = NM_SETTING (a);
	const char *str = (const char *) b;

	return strcmp (nm_setting_get_name (setting), str);
}

static gboolean
verify (NMSetting *setting, GSList *all_settings, GError **error)
{
	NMSettingWirelessPrivate *priv = NM_SETTING_WIRELESS_GET_PRIVATE (setting);
	const char *valid_modes[] = { "infrastructure", "adhoc", NULL };
	const char *valid_bands[] = { "a", "bg", NULL };
	GSList *iter;

	if (!priv->ssid) {
		g_set_error (error,
		             NM_SETTING_WIRELESS_ERROR,
		             NM_SETTING_WIRELESS_ERROR_MISSING_PROPERTY,
		             NM_SETTING_WIRELESS_SSID);
		return FALSE;
	}

	if (!priv->ssid->len || priv->ssid->len > 32) {
		g_set_error (error,
		             NM_SETTING_WIRELESS_ERROR,
		             NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY,
		             NM_SETTING_WIRELESS_SSID);
		return FALSE;
	}

	if (priv->mode && !_nm_utils_string_in_list (priv->mode, valid_modes)) {
		g_set_error (error,
		             NM_SETTING_WIRELESS_ERROR,
		             NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY,
		             NM_SETTING_WIRELESS_MODE);
		return FALSE;
	}

	if (priv->band && !_nm_utils_string_in_list (priv->band, valid_bands)) {
		g_set_error (error,
		             NM_SETTING_WIRELESS_ERROR,
		             NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY,
		             NM_SETTING_WIRELESS_BAND);
		return FALSE;
	}

	if (priv->channel && !priv->band) {
		g_set_error (error,
		             NM_SETTING_WIRELESS_ERROR,
		             NM_SETTING_WIRELESS_ERROR_CHANNEL_REQUIRES_BAND,
		             NM_SETTING_WIRELESS_BAND);
		return FALSE;
	}

	if (priv->channel) {
		if (!strcmp (priv->band, "a")) {
			int i;
			int valid_channels[] = { 7, 8, 9, 11, 12, 16, 34, 36, 40, 44, 48,
			                         52, 56, 60, 64, 100, 104, 108, 112, 116,
			                         120, 124, 128, 132, 136, 140, 149, 153,
			                         157, 161, 165, 183, 184, 185, 187, 188,
			                         192, 196, 0 };

			for (i = 0; valid_channels[i]; i++) {
				if (priv->channel == valid_channels[i])
					break;
			}

			if (valid_channels[i] == 0) {
				g_set_error (error,
				             NM_SETTING_WIRELESS_ERROR,
				             NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY,
				             NM_SETTING_WIRELESS_CHANNEL);
				return FALSE;
			}
		} else if (!strcmp (priv->band, "bg") && priv->channel > 14) {
				g_set_error (error,
				             NM_SETTING_WIRELESS_ERROR,
				             NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY,
				             NM_SETTING_WIRELESS_CHANNEL);
			return FALSE;
		}
	}

	if (priv->bssid && priv->bssid->len != ETH_ALEN) {
		g_set_error (error,
		             NM_SETTING_WIRELESS_ERROR,
		             NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY,
		             NM_SETTING_WIRELESS_BSSID);
		return FALSE;
	}

	if (priv->mac_address && priv->mac_address->len != ETH_ALEN) {
		g_set_error (error,
		             NM_SETTING_WIRELESS_ERROR,
		             NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY,
		             NM_SETTING_WIRELESS_MAC_ADDRESS);
		return FALSE;
	}

	for (iter = priv->seen_bssids; iter; iter = iter->next) {
		struct ether_addr addr;

		if (!ether_aton_r (iter->data, &addr)) {
			g_set_error (error,
			             NM_SETTING_WIRELESS_ERROR,
			             NM_SETTING_WIRELESS_ERROR_INVALID_PROPERTY,
			             NM_SETTING_WIRELESS_SEEN_BSSIDS);
			return FALSE;
		}
	}

	if (   priv->security
	    && !g_slist_find_custom (all_settings, priv->security, find_setting_by_name)) {
		g_set_error (error,
		             NM_SETTING_WIRELESS_ERROR,
		             NM_SETTING_WIRELESS_ERROR_MISSING_SECURITY_SETTING,
		             NULL);
		return FALSE;
	}

	return TRUE;
}

static void
nm_setting_wireless_init (NMSettingWireless *setting)
{
	g_object_set (setting, NM_SETTING_NAME, NM_SETTING_WIRELESS_SETTING_NAME, NULL);
}

static void
finalize (GObject *object)
{
	NMSettingWirelessPrivate *priv = NM_SETTING_WIRELESS_GET_PRIVATE (object);

	g_free (priv->mode);
	g_free (priv->band);
	g_free (priv->security);

	if (priv->ssid)
		g_byte_array_free (priv->ssid, TRUE);
	if (priv->bssid)
		g_byte_array_free (priv->bssid, TRUE);
	if (priv->mac_address)
		g_byte_array_free (priv->mac_address, TRUE);

	nm_utils_slist_free (priv->seen_bssids, g_free);

	G_OBJECT_CLASS (nm_setting_wireless_parent_class)->finalize (object);
}

static void
set_property (GObject *object, guint prop_id,
		    const GValue *value, GParamSpec *pspec)
{
	NMSettingWirelessPrivate *priv = NM_SETTING_WIRELESS_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_SSID:
		if (priv->ssid)
			g_byte_array_free (priv->ssid, TRUE);
		priv->ssid = g_value_dup_boxed (value);
		break;
	case PROP_MODE:
		g_free (priv->mode);
		priv->mode = g_value_dup_string (value);
		break;
	case PROP_BAND:
		g_free (priv->band);
		priv->band = g_value_dup_string (value);
		break;
	case PROP_CHANNEL:
		priv->channel = g_value_get_uint (value);
		break;
	case PROP_BSSID:
		if (priv->bssid)
			g_byte_array_free (priv->bssid, TRUE);
		priv->bssid = g_value_dup_boxed (value);
		break;
	case PROP_RATE:
		priv->rate = g_value_get_uint (value);
		break;
	case PROP_TX_POWER:
		priv->tx_power = g_value_get_uint (value);
		break;
	case PROP_MAC_ADDRESS:
		if (priv->mac_address)
			g_byte_array_free (priv->mac_address, TRUE);
		priv->mac_address = g_value_dup_boxed (value);
		break;
	case PROP_MTU:
		priv->mtu = g_value_get_uint (value);
		break;
	case PROP_SEEN_BSSIDS:
		nm_utils_slist_free (priv->seen_bssids, g_free);
		priv->seen_bssids = g_value_dup_boxed (value);
		break;
	case PROP_SEC:
		g_free (priv->security);
		priv->security = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
		    GValue *value, GParamSpec *pspec)
{
	NMSettingWireless *setting = NM_SETTING_WIRELESS (object);

	switch (prop_id) {
	case PROP_SSID:
		g_value_set_boxed (value, nm_setting_wireless_get_ssid (setting));
		break;
	case PROP_MODE:
		g_value_set_string (value, nm_setting_wireless_get_mode (setting));
		break;
	case PROP_BAND:
		g_value_set_string (value, nm_setting_wireless_get_band (setting));
		break;
	case PROP_CHANNEL:
		g_value_set_uint (value, nm_setting_wireless_get_channel (setting));
		break;
	case PROP_BSSID:
		g_value_set_boxed (value, nm_setting_wireless_get_bssid (setting));
		break;
	case PROP_RATE:
		g_value_set_uint (value, nm_setting_wireless_get_rate (setting));
		break;
	case PROP_TX_POWER:
		g_value_set_uint (value, nm_setting_wireless_get_tx_power (setting));
		break;
	case PROP_MAC_ADDRESS:
		g_value_set_boxed (value, nm_setting_wireless_get_mac_address (setting));
		break;
	case PROP_MTU:
		g_value_set_uint (value, nm_setting_wireless_get_mtu (setting));
		break;
	case PROP_SEEN_BSSIDS:
		g_value_set_boxed (value, NM_SETTING_WIRELESS_GET_PRIVATE (setting)->seen_bssids);
		break;
	case PROP_SEC:
		g_value_set_string (value, nm_setting_wireless_get_security (setting));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_setting_wireless_class_init (NMSettingWirelessClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	g_type_class_add_private (setting_class, sizeof (NMSettingWirelessPrivate));

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize     = finalize;
	parent_class->verify       = verify;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_SSID,
		 _nm_param_spec_specialized (NM_SETTING_WIRELESS_SSID,
							   "SSID",
							   "SSID",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_MODE,
		 g_param_spec_string (NM_SETTING_WIRELESS_MODE,
						  "Mode",
						  "Mode",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_BAND,
		 g_param_spec_string (NM_SETTING_WIRELESS_BAND,
						  "Band",
						  "Band",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_CHANNEL,
		 g_param_spec_uint (NM_SETTING_WIRELESS_CHANNEL,
						"Channel",
						"Channel",
						0, G_MAXUINT32, 0,
						G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_BSSID,
		 _nm_param_spec_specialized (NM_SETTING_WIRELESS_BSSID,
							   "BSSID",
							   "BSSID",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_RATE,
		 g_param_spec_uint (NM_SETTING_WIRELESS_RATE,
						"Rate",
						"Rate",
						0, G_MAXUINT32, 0,
						G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE | NM_SETTING_PARAM_FUZZY_IGNORE));

	g_object_class_install_property
		(object_class, PROP_TX_POWER,
		 g_param_spec_uint (NM_SETTING_WIRELESS_TX_POWER,
						"TX Power",
						"TX Power",
						0, G_MAXUINT32, 0,
						G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE | NM_SETTING_PARAM_FUZZY_IGNORE));

	g_object_class_install_property
		(object_class, PROP_MAC_ADDRESS,
		 _nm_param_spec_specialized (NM_SETTING_WIRELESS_MAC_ADDRESS,
							   "MAC Address",
							   "Harware address",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_SEEN_BSSIDS,
		 _nm_param_spec_specialized (NM_SETTING_WIRELESS_SEEN_BSSIDS,
							   "Seen BSSIDS",
							   "Seen BSSIDs",
							   DBUS_TYPE_G_LIST_OF_STRING,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE | NM_SETTING_PARAM_FUZZY_IGNORE));

	g_object_class_install_property
		(object_class, PROP_MTU,
		 g_param_spec_uint (NM_SETTING_WIRELESS_MTU,
						"MTU",
						"MTU",
						0, G_MAXUINT32, 0,
						G_PARAM_READWRITE | G_PARAM_CONSTRUCT | NM_SETTING_PARAM_SERIALIZE | NM_SETTING_PARAM_FUZZY_IGNORE));

	g_object_class_install_property
		(object_class, PROP_SEC,
		 g_param_spec_string (NM_SETTING_WIRELESS_SEC,
						  "Security",
						  "Security",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));
}
