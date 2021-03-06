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
 * Copyright (C) 2004 - 2005 Colin Walters <walters@redhat.com>
 * Copyright (C) 2004 - 2008 Red Hat, Inc.
 * Copyright (C) 2005 - 2008 Novell, Inc.
 *   and others
 */

#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include <unistd.h>
#include <glib.h>

#include <glib/gi18n.h>

#include "nm-named-manager.h"
#include "nm-ip4-config.h"
#include "nm-utils.h"
#include "NetworkManagerSystem.h"
#include "NetworkManagerUtils.h"

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#endif

#ifndef RESOLV_CONF
#define RESOLV_CONF "/etc/resolv.conf"
#endif

#define ADDR_BUF_LEN 50

G_DEFINE_TYPE(NMNamedManager, nm_named_manager, G_TYPE_OBJECT)

#define NM_NAMED_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                         NM_TYPE_NAMED_MANAGER, \
                                         NMNamedManagerPrivate))


struct NMNamedManagerPrivate {
	NMIP4Config *   vpn_config;
	NMIP4Config *   device_config;
	GSList *        configs;
};


NMNamedManager *
nm_named_manager_get (void)
{
	static NMNamedManager * singleton = NULL;

	if (!singleton) {
		singleton = NM_NAMED_MANAGER (g_object_new (NM_TYPE_NAMED_MANAGER, NULL));
	} else {
		g_object_ref (singleton);
	}

	g_assert (singleton);
	return singleton;
}


GQuark
nm_named_manager_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("nm_named_manager_error");

	return quark;
}

typedef struct {
	GPtrArray *nameservers;
	const char *domain;
	GPtrArray *searches;
} NMResolvConfData;

static void
merge_one_ip4_config (NMResolvConfData *rc, NMIP4Config *src)
{
	guint32 num, i;

	num = nm_ip4_config_get_num_nameservers (src);
	for (i = 0; i < num; i++) {
		struct in_addr addr;
		char buf[INET_ADDRSTRLEN];

		addr.s_addr = nm_ip4_config_get_nameserver (src, i);
		if (inet_ntop (AF_INET, &addr, buf, INET_ADDRSTRLEN) > 0)
			g_ptr_array_add (rc->nameservers, g_strdup (buf));
	}

	num = nm_ip4_config_get_num_domains (src);
	for (i = 0; i < num; i++) {
		if (!rc->domain)
			rc->domain = nm_ip4_config_get_domain (src, i);
		g_ptr_array_add (rc->searches, g_strdup (nm_ip4_config_get_domain (src, i)));
	}

	num = nm_ip4_config_get_num_searches (src);
	for (i = 0; i < num; i++)
		g_ptr_array_add (rc->searches, g_strdup (nm_ip4_config_get_search (src, i)));
}

static void
merge_one_ip6_config (NMResolvConfData *rc, NMIP6Config *src)
{
	guint32 num, i;

	num = nm_ip6_config_get_num_nameservers (src);
	for (i = 0; i < num; i++) {
		const struct in6_addr *addr;
		char buf[INET6_ADDRSTRLEN];

		addr = nm_ip6_config_get_nameserver (src, i);

		/* inet_ntop is probably supposed to do this for us, but it doesn't */
		if (IN6_IS_ADDR_V4MAPPED (addr)) {
			if (inet_ntop (AF_INET, &(addr->s6_addr32[3]), buf, INET_ADDRSTRLEN) > 0)
				g_ptr_array_add (rc->nameservers, g_strdup (buf));
		} else {
			if (inet_ntop (AF_INET6, addr, buf, INET6_ADDRSTRLEN) > 0)
				g_ptr_array_add (rc->nameservers, g_strdup (buf));
		}
	}

	num = nm_ip6_config_get_num_domains (src);
	for (i = 0; i < num; i++) {
		if (!rc->domain)
			rc->domain = nm_ip6_config_get_domain (src, i);
		g_ptr_array_add (rc->searches, g_strdup (nm_ip6_config_get_domain (src, i)));
	}

	num = nm_ip6_config_get_num_searches (src);
	for (i = 0; i < num; i++)
		g_ptr_array_add (rc->searches, g_strdup (nm_ip6_config_get_search (src, i)));
}


#if defined(TARGET_SUSE)
/**********************************/
/* SUSE */

static void
netconfig_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	pid_t pid = getpid ();
	setpgid (pid, pid);
}

static GPid
run_netconfig (GError **error, gint *stdin_fd)
{
	char *argv[5];
	char *tmp;
	GPid pid = -1;

	argv[0] = "/sbin/netconfig";
	argv[1] = "modify";
	argv[2] = "--service";
	argv[3] = "NetworkManager";
	argv[4] = NULL;

	tmp = g_strjoinv (" ", argv);
	nm_debug ("Spawning '%s'", tmp);
	g_free (tmp);

	if (!g_spawn_async_with_pipes (NULL, argv, NULL, 0, netconfig_child_setup,
	                               NULL, &pid, stdin_fd, NULL, NULL, error))
		return -1;

	return pid;
}

static void
write_to_netconfig (gint fd, const char *key, const char *value)
{
	char *str;
	int x;

	str = g_strdup_printf ("%s='%s'\n", key, value);
	nm_debug ("Writing to netconfig: %s", str);
	x = write (fd, str, strlen (str));
	g_free (str);
}

static gboolean
dispatch_netconfig (const char *domain,
                    char **searches,
                    char **nameservers,
                    const char *iface,
                    GError **error)
{
	char *str;
	GPid pid;
	gint fd;
	int ret;

	pid = run_netconfig (error, &fd);
	if (pid < 0)
		return FALSE;

	// FIXME: this is wrong. We are not writing out the iface-specific
	// resolv.conf data, we are writing out an already-fully-merged
	// resolv.conf. Assuming netconfig works in the obvious way, then
	// there are various failure modes, such as, eg, bringing up a VPN on
	// eth0, then bringing up wlan0, then bringing down the VPN. Because
	// NMNamedManager would have claimed that the VPN DNS server was also
	// part of the wlan0 config, it will remain in resolv.conf after the
	// VPN goes down, even though it is presumably no longer reachable
	// at that point.
	write_to_netconfig (fd, "INTERFACE", iface);

	if (searches) {
		str = g_strjoinv (" ", searches);

		if (domain) {
			char *tmp;

			tmp = g_strconcat (domain, " ", str, NULL);
			g_free (str);
			str = tmp;
		}

		write_to_netconfig (fd, "DNSSEARCH", str);
		g_free (str);
	}

	if (nameservers) {
		str = g_strjoinv (" ", nameservers);
		write_to_netconfig (fd, "DNSSERVERS", str);
		g_free (str);
	}

	close (fd);

	/* Wait until the process exits */

 again:

	ret = waitpid (pid, NULL, 0);
	if (ret < 0 && errno == EINTR)
		goto again;

	return ret > 0;
}
#endif


static gboolean
write_resolv_conf (FILE *f, const char *domain,
                   char **searches,
                   char **nameservers,
                   GError **error)
{
	char *domain_str = NULL;
	char *searches_str = NULL;
	char *nameservers_str = NULL;
	int i;
	gboolean retval = FALSE;

	if (fprintf (f, "%s","# Generated by NetworkManager\n") < 0) {
		g_set_error (error,
				   NM_NAMED_MANAGER_ERROR,
				   NM_NAMED_MANAGER_ERROR_SYSTEM,
				   "Could not write " RESOLV_CONF ": %s\n",
				   g_strerror (errno));
		return FALSE;
	}

	if (domain)
		domain_str = g_strconcat ("domain ", domain, "\n", NULL);

	if (searches) {
		char *tmp_str;

		tmp_str = g_strjoinv (" ", searches);
		searches_str = g_strconcat ("search ", tmp_str, "\n", NULL);
		g_free (tmp_str);
	}

	if (nameservers) {
		GString *str;
		int num;

		str = g_string_new ("");
		num = g_strv_length (nameservers);

		for (i = 0; i < num; i++) {
			if (i == 3) {
				g_string_append (str, "# ");
				g_string_append (str, _("NOTE: the libc resolver may not support more than 3 nameservers."));
				g_string_append (str, "\n# ");
				g_string_append (str, _("The nameservers listed below may not be recognized."));
				g_string_append_c (str, '\n');
			}

			g_string_append (str, "nameserver ");
			g_string_append (str, nameservers[i]);
			g_string_append_c (str, '\n');
		}

		nameservers_str = g_string_free (str, FALSE);
	}

	if (fprintf (f, "%s%s%s",
		     domain_str ? domain_str : "",
	             searches_str ? searches_str : "",
	             nameservers_str ? nameservers_str : "") != -1)
		retval = TRUE;

	g_free (domain_str);
	g_free (searches_str);
	g_free (nameservers_str);

	return retval;
}

#ifdef RESOLVCONF_PATH
static gboolean
dispatch_resolvconf (const char *domain,
                     char **searches,
                     char **nameservers,
                     const char *iface,
                     GError **error)
{
	char *cmd;
	FILE *f;
	gboolean retval = FALSE;

	if (! g_file_test (RESOLVCONF_PATH, G_FILE_TEST_IS_EXECUTABLE))
		return FALSE;

	if (domain || searches || nameservers) {
		cmd = g_strconcat (RESOLVCONF_PATH, " -a ", "NetworkManager", NULL);
		nm_info ("(%s): writing resolv.conf to %s", iface, RESOLVCONF_PATH);
		if ((f = popen (cmd, "w")) == NULL)
			g_set_error (error,
				     NM_NAMED_MANAGER_ERROR,
				     NM_NAMED_MANAGER_ERROR_SYSTEM,
				     "Could not write to %s: %s\n",
				     RESOLVCONF_PATH,
				     g_strerror (errno));
		else {
			retval = write_resolv_conf (f, domain, searches, nameservers, error);
			retval &= (pclose (f) == 0);
		}
	} else {
		cmd = g_strconcat (RESOLVCONF_PATH, " -d ", "NetworkManager", NULL);
		nm_info ("(%s): removing resolv.conf from %s", iface, RESOLVCONF_PATH);
		if (nm_spawn_process (cmd) == 0)
			retval = TRUE;
	}

	g_free (cmd);

	return retval;
}
#endif

static gboolean
update_resolv_conf (const char *domain,
                    char **searches,
                    char **nameservers,
                    const char *iface,
                    GError **error)
{
	const char *tmp_resolv_conf = RESOLV_CONF ".tmp";
	char tmp_resolv_conf_realpath [PATH_MAX];
	FILE *f;
	int do_rename = 1;
	int old_errno = 0;

	if (!realpath (tmp_resolv_conf, tmp_resolv_conf_realpath))
		strcpy (tmp_resolv_conf_realpath, tmp_resolv_conf);

	if ((f = fopen (tmp_resolv_conf_realpath, "w")) == NULL) {
		do_rename = 0;
		old_errno = errno;
		if ((f = fopen (RESOLV_CONF, "w")) == NULL) {
			g_set_error (error,
			             NM_NAMED_MANAGER_ERROR,
			             NM_NAMED_MANAGER_ERROR_SYSTEM,
			             "Could not open %s: %s\nCould not open %s: %s\n",
			             tmp_resolv_conf_realpath,
			             g_strerror (old_errno),
			             RESOLV_CONF,
			             g_strerror (errno));
			return FALSE;
		}
		strcpy (tmp_resolv_conf_realpath, RESOLV_CONF);
	}

	write_resolv_conf (f, domain, searches, nameservers, error);

	if (fclose (f) < 0) {
		if (*error == NULL) {
			g_set_error (error,
					   NM_NAMED_MANAGER_ERROR,
					   NM_NAMED_MANAGER_ERROR_SYSTEM,
					   "Could not close %s: %s\n",
					   tmp_resolv_conf_realpath,
					   g_strerror (errno));
		}
	}

	if (*error == NULL && do_rename) {
		if (rename (tmp_resolv_conf, RESOLV_CONF) < 0) {
			g_set_error (error,
					   NM_NAMED_MANAGER_ERROR,
					   NM_NAMED_MANAGER_ERROR_SYSTEM,
					   "Could not replace " RESOLV_CONF ": %s\n",
					   g_strerror (errno));
		}
	}

	return *error ? FALSE : TRUE;
}

static gboolean
rewrite_resolv_conf (NMNamedManager *mgr, const char *iface, GError **error)
{
	NMNamedManagerPrivate *priv;
	NMResolvConfData rc;
	GSList *iter;
	const char *domain = NULL;
	char **searches = NULL;
	char **nameservers = NULL;
	int num, i, len;
	gboolean success = FALSE;

	g_return_val_if_fail (error != NULL, FALSE);
	g_return_val_if_fail (*error == NULL, FALSE);

	priv = NM_NAMED_MANAGER_GET_PRIVATE (mgr);

	rc.nameservers = g_ptr_array_new ();
	rc.domain = NULL;
	rc.searches = g_ptr_array_new ();

	if (priv->vpn_config)
		merge_one_ip4_config (&rc, priv->vpn_config);

	if (priv->device_config)
		merge_one_ip4_config (&rc, priv->device_config);

	for (iter = priv->configs; iter; iter = g_slist_next (iter)) {
		if (NM_IS_IP4_CONFIG (iter->data)) {
			NMIP4Config *config = NM_IP4_CONFIG (iter->data);

			if ((config == priv->vpn_config) || (config == priv->device_config))
				continue;

			merge_one_ip4_config (&rc, config);
		} else {
			NMIP6Config *config = NM_IP6_CONFIG (iter->data);

			merge_one_ip6_config (&rc, config);
		}
	}

	domain = rc.domain;

	/* Per 'man resolv.conf', the search list is limited to 6 domains
	 * totalling 256 characters.
	 */
	num = MIN (rc.searches->len, 6);
	for (i = 0, len = 0; i < num; i++) {
		len += strlen (rc.searches->pdata[i]) + 1; /* +1 for spaces */
		if (len > 256)
			break;
	}
	g_ptr_array_set_size (rc.searches, i);
	if (rc.searches->len) {
		g_ptr_array_add (rc.searches, NULL);
		searches = (char **) g_ptr_array_free (rc.searches, FALSE);
	} else
		g_ptr_array_free (rc.searches, TRUE);

	if (rc.nameservers->len) {
		g_ptr_array_add (rc.nameservers, NULL);
		nameservers = (char **) g_ptr_array_free (rc.nameservers, FALSE);
	} else
		g_ptr_array_free (rc.nameservers, TRUE);

#ifdef RESOLVCONF_PATH
	success = dispatch_resolvconf (domain, searches, nameservers, iface, error);
#endif

#ifdef TARGET_SUSE
	if (success == FALSE)
		success = dispatch_netconfig (domain, searches, nameservers, iface, error);
#endif

	if (success == FALSE)
		success = update_resolv_conf (domain, searches, nameservers, iface, error);

	if (success)
		nm_system_update_dns ();

	if (searches)
		g_strfreev (searches);
	if (nameservers)
		g_strfreev (nameservers);

	return success;
}

gboolean
nm_named_manager_add_ip4_config (NMNamedManager *mgr,
								 const char *iface,
								 NMIP4Config *config,
								 NMNamedIPConfigType cfg_type)
{
	NMNamedManagerPrivate *priv;
	GError *error = NULL;

	g_return_val_if_fail (mgr != NULL, FALSE);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (config != NULL, FALSE);

	priv = NM_NAMED_MANAGER_GET_PRIVATE (mgr);

	switch (cfg_type) {
	case NM_NAMED_IP_CONFIG_TYPE_VPN:
		priv->vpn_config = config;
		break;
	case NM_NAMED_IP_CONFIG_TYPE_BEST_DEVICE:
		priv->device_config = config;
		break;
	default:
		break;
	}

	/* Don't allow the same zone added twice */
	if (!g_slist_find (priv->configs, config))
		priv->configs = g_slist_append (priv->configs, g_object_ref (config));

	if (!rewrite_resolv_conf (mgr, iface, &error)) {
		nm_warning ("Could not commit DNS changes.  Error: '%s'", error ? error->message : "(none)");
		g_error_free (error);
	}

	return TRUE;
}

gboolean
nm_named_manager_remove_ip4_config (NMNamedManager *mgr,
									const char *iface,
									NMIP4Config *config)
{
	NMNamedManagerPrivate *priv;
	GError *error = NULL;

	g_return_val_if_fail (mgr != NULL, FALSE);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (config != NULL, FALSE);

	priv = NM_NAMED_MANAGER_GET_PRIVATE (mgr);

	/* Can't remove it if it wasn't in the list to begin with */
	if (!g_slist_find (priv->configs, config))
		return FALSE;

	priv->configs = g_slist_remove (priv->configs, config);

	if (config == priv->vpn_config)
		priv->vpn_config = NULL;

	if (config == priv->device_config)
		priv->device_config = NULL;

	g_object_unref (config);

	if (!rewrite_resolv_conf (mgr, iface, &error)) {
		nm_warning ("Could not commit DNS changes.  Error: '%s'", error ? error->message : "(none)");
		if (error)
			g_error_free (error);
	}

	return TRUE;
}

gboolean
nm_named_manager_add_ip6_config (NMNamedManager *mgr,
								 const char *iface,
								 NMIP6Config *config,
								 NMNamedIPConfigType cfg_type)
{
	NMNamedManagerPrivate *priv;
	GError *error = NULL;

	g_return_val_if_fail (mgr != NULL, FALSE);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (config != NULL, FALSE);

	g_return_val_if_fail (cfg_type == NM_NAMED_IP_CONFIG_TYPE_DEFAULT, FALSE);

	priv = NM_NAMED_MANAGER_GET_PRIVATE (mgr);

	/* Don't allow the same zone added twice */
	if (!g_slist_find (priv->configs, config))
		priv->configs = g_slist_append (priv->configs, g_object_ref (config));

	if (!rewrite_resolv_conf (mgr, iface, &error)) {
		nm_warning ("Could not commit DNS changes.  Error: '%s'", error ? error->message : "(none)");
		g_error_free (error);
	}

	return TRUE;
}

gboolean
nm_named_manager_remove_ip6_config (NMNamedManager *mgr,
									const char *iface,
									NMIP6Config *config)
{
	NMNamedManagerPrivate *priv;
	GError *error = NULL;

	g_return_val_if_fail (mgr != NULL, FALSE);
	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (config != NULL, FALSE);

	priv = NM_NAMED_MANAGER_GET_PRIVATE (mgr);

	/* Can't remove it if it wasn't in the list to begin with */
	if (!g_slist_find (priv->configs, config))
		return FALSE;

	priv->configs = g_slist_remove (priv->configs, config);

	g_object_unref (config);	

	if (!rewrite_resolv_conf (mgr, iface, &error)) {
		nm_warning ("Could not commit DNS changes.  Error: '%s'", error ? error->message : "(none)");
		if (error)
			g_error_free (error);
	}

	return TRUE;
}


static void
nm_named_manager_init (NMNamedManager *mgr)
{
}

static void
nm_named_manager_finalize (GObject *object)
{
	NMNamedManagerPrivate *priv = NM_NAMED_MANAGER_GET_PRIVATE (object);

	g_slist_foreach (priv->configs, (GFunc) g_object_unref, NULL);
	g_slist_free (priv->configs);

	G_OBJECT_CLASS (nm_named_manager_parent_class)->finalize (object);
}

static void
nm_named_manager_class_init (NMNamedManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = nm_named_manager_finalize;

	g_type_class_add_private (object_class, sizeof (NMNamedManagerPrivate));
}

