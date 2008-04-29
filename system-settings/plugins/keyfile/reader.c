/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <dbus/dbus-glib.h>
#include <nm-setting.h>

#include "reader.h"

#define DBUS_TYPE_G_ARRAY_OF_UINT          (dbus_g_type_get_collection ("GArray", G_TYPE_UINT))
#define DBUS_TYPE_G_ARRAY_OF_ARRAY_OF_UINT (dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_ARRAY_OF_UINT))

static void
read_one_setting_value (NMSetting *setting,
				    const char *key,
				    const GValue *value,
				    gboolean secret,
				    gpointer user_data)
{
	GKeyFile *file = (GKeyFile *) user_data;
	GType type;
	GError *err = NULL;

	if (!g_key_file_has_key (file, setting->name, key, &err)) {
		if (err) {
			g_warning ("Error loading setting '%s' value: %s", setting->name, err->message);
			g_error_free (err);
		}

		return;
	}

	type = G_VALUE_TYPE (value);

	if (type == G_TYPE_STRING) {
		char *str_val;

		str_val = g_key_file_get_string (file, setting->name, key, NULL);
		g_object_set (setting, key, str_val, NULL);
		g_free (str_val);
	} else if (type == G_TYPE_UINT) {
		int int_val;

		int_val = g_key_file_get_integer (file, setting->name, key, NULL);
		if (int_val < 0)
			g_warning ("Casting negative value (%i) to uint", int_val);
		g_object_set (setting, key, int_val, NULL);
	} else if (type == G_TYPE_INT) {
		int int_val;

		int_val = g_key_file_get_integer (file, setting->name, key, NULL);
		g_object_set (setting, key, int_val, NULL);
	} else if (type == G_TYPE_BOOLEAN) {
		gboolean bool_val;

		bool_val = g_key_file_get_boolean (file, setting->name, key, NULL);
		g_object_set (setting, key, bool_val, NULL);
	} else if (type == G_TYPE_CHAR) {
		int int_val;

		int_val = g_key_file_get_integer (file, setting->name, key, NULL);
		if (int_val < G_MININT8 || int_val > G_MAXINT8)
			g_warning ("Casting value (%i) to char", int_val);

		g_object_set (setting, key, int_val, NULL);
	} else if (type == G_TYPE_UINT64) {
		char *tmp_str;
		guint64 uint_val;

		tmp_str = g_key_file_get_value (file, setting->name, key, NULL);
		uint_val = g_ascii_strtoull (tmp_str, NULL, 10);
		g_object_set (setting, key, uint_val, NULL);
 	} else if (type == DBUS_TYPE_G_UCHAR_ARRAY) {
		gint *tmp;
		GByteArray *array;
		gsize length;
		int i;

		tmp = g_key_file_get_integer_list (file, setting->name, key, &length, NULL);

		array = g_byte_array_sized_new (length);
		for (i = 0; i < length; i++) {
			int val = tmp[i];
			unsigned char v = (unsigned char) (val & 0xFF);

			if (val < 0 || val > 255)
				g_warning ("Value out of range for a byte value");
			else
				g_byte_array_append (array, (const unsigned char *) &v, sizeof (v));
		}

		g_object_set (setting, key, array, NULL);
		g_byte_array_free (array, TRUE);
 	} else if (type == dbus_g_type_get_collection ("GSList", G_TYPE_STRING)) {
		gchar **sa;
		gsize length;
		int i;
		GSList *list = NULL;

		sa = g_key_file_get_string_list (file, setting->name, key, &length, NULL);
		for (i = 0; i < length; i++)
			list = g_slist_prepend (list, sa[i]);

		list = g_slist_reverse (list);
		g_object_set (setting, key, list, NULL);

		g_slist_free (list);
		g_strfreev (sa);
	} else if (type == dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)) {
		/* FIXME */
		g_warning ("Implement me");
	} else if (type == DBUS_TYPE_G_UINT_ARRAY) {
		gint *tmp;
		GArray *array;
		gsize length;
		int i;

		tmp = g_key_file_get_integer_list (file, setting->name, key, &length, NULL);

		array = g_array_sized_new (FALSE, FALSE, sizeof (guint32), length);
		for (i = 0; i < length; i++)
			g_array_append_val (array, tmp[i]);

		g_object_set (setting, key, array, NULL);
		g_array_free (array, TRUE);
	} else if (type == DBUS_TYPE_G_ARRAY_OF_ARRAY_OF_UINT) {
		/* FIXME */
		g_warning ("Implement me");
	} else {
		g_warning ("Unhandled setting property type (read): '%s/%s' : '%s'",
				 setting->name, key, G_VALUE_TYPE_NAME (value));
	}
}

static NMSetting *
read_setting (GKeyFile *file, const char *name)
{
	NMSetting *setting;

	setting = nm_connection_create_setting (name);
	if (setting) {
		nm_setting_enumerate_values (setting, read_one_setting_value, file);
	} else
		g_warning ("Invalid setting name '%s'", name);

	return setting;
}

NMConnection *
connection_from_file (const char *filename)
{
	GKeyFile *key_file;
	struct stat statbuf;
	gboolean bad_owner, bad_permissions;
	NMConnection *connection = NULL;
	GError *err = NULL;

	if (stat (filename, &statbuf) != 0 || !S_ISREG (statbuf.st_mode))
		return NULL;

	bad_owner = getuid () != statbuf.st_uid;
	bad_permissions = statbuf.st_mode & 0077;

    if (bad_owner || bad_permissions) {
	    g_warning ("Ignorning insecure configuration file '%s'", filename);
	    return NULL;
    }

	key_file = g_key_file_new ();
	if (g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, &err)) {
		gchar **groups;
		gsize length;
		int i;

		connection = nm_connection_new ();

		groups = g_key_file_get_groups (key_file, &length);
		for (i = 0; i < length; i++) {
			NMSetting *setting;

			setting = read_setting (key_file, groups[i]);
			if (setting)
				nm_connection_add_setting (connection, setting);
		}

		g_strfreev (groups);
	} else {
		g_warning ("Error parsing file '%s': %s", filename, err->message);
		g_error_free (err);
	}

	g_key_file_free (key_file);

	return connection;
}