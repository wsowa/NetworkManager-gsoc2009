SUBDIRS= \
	named-manager \
	vpn-manager \
	dhcp-manager \
	ip6-manager \
	supplicant-manager \
	ppp-manager \
	backends \
	dnsmasq-manager \
	modem-manager \
	bluez-manager \
	system-settings \
	. \
	tests

INCLUDES = -I${top_srcdir}                   \
           -I${top_srcdir}/include           \
           -I${top_builddir}/marshallers   \
           -I${top_srcdir}/src/named-manager \
           -I${top_srcdir}/src/vpn-manager   \
           -I${top_srcdir}/src/dhcp-manager  \
           -I${top_srcdir}/src/ip6-manager  \
           -I${top_srcdir}/src/supplicant-manager  \
           -I${top_srcdir}/src/dnsmasq-manager  \
           -I${top_srcdir}/src/modem-manager  \
           -I$(top_srcdir)/src/bluez-manager \
           -I$(top_srcdir)/src/system-settings \
           -I${top_srcdir}/libnm-util \
           -I${top_srcdir}/libnm-glib \
           -I${top_srcdir}/callouts

###########################################
# DHCP test library
###########################################

noinst_LTLIBRARIES = libtest-dhcp.la

libtest_dhcp_la_SOURCES = \
	nm-ip4-config.c \
	nm-hostname-provider.c \
	nm-dbus-manager.c

libtest_dhcp_la_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	$(DBUS_CFLAGS) \
	$(LIBNL_CFLAGS)

libtest_dhcp_la_LIBADD = \
	$(top_builddir)/libnm-util/libnm-util.la \
	$(GLIB_LIBS) \
	$(DBUS_LIBS) \
	$(LIBNL_LIBS)


###########################################
# NetworkManager
###########################################

sbin_PROGRAMS = NetworkManager

NetworkManager_SOURCES = \
		nm-call-store.c \
		nm-call-store.h \
		nm-device.c \
		nm-device.h \
		nm-device-interface.c \
		nm-device-interface.h \
		nm-device-private.h \
		nm-device-ethernet.c \
		nm-device-ethernet.h \
		nm-device-wifi.c \
		nm-device-wifi.h \
		nm-device-olpc-mesh.c	\
		nm-device-olpc-mesh.h	\
		nm-device-bt.c \
		nm-device-bt.h \
		NetworkManagerAP.c \
		NetworkManagerAP.h \
		nm-dbus-manager.h \
		nm-dbus-manager.c \
		nm-udev-manager.c \
		nm-udev-manager.h \
		nm-hostname-provider.c \
		nm-hostname-provider.h \
		nm-ip4-config.c \
		nm-ip4-config.h \
		nm-ip6-config.c \
		nm-ip6-config.h \
		nm-secrets-provider-interface.c \
		nm-secrets-provider-interface.h \
		nm-active-connection.h \
		nm-active-connection.c \
		NetworkManager.c \
		NetworkManagerPolicy.c \
		NetworkManagerPolicy.h \
		NetworkManagerUtils.c \
		NetworkManagerUtils.h \
		NetworkManagerSystem.c \
		NetworkManagerSystem.h \
		nm-logging.c \
		nm-logging.h \
		nm-manager.c \
		nm-manager.h \
		nm-netlink-monitor.c \
		nm-netlink-monitor.h \
		nm-activation-request.c \
		nm-activation-request.h \
		nm-properties-changed-signal.c \
		nm-properties-changed-signal.h \
		wpa.c \
		wpa.h \
		nm-netlink.c \
		nm-netlink.h \
		nm-dhcp4-config.c \
		nm-dhcp4-config.h

nm-access-point-glue.h: $(top_srcdir)/introspection/nm-access-point.xml
	dbus-binding-tool --prefix=nm_access_point --mode=glib-server --output=$@ $<

nm-manager-glue.h: $(top_srcdir)/introspection/nm-manager.xml
	dbus-binding-tool --prefix=nm_manager --mode=glib-server --output=$@ $<

nm-device-interface-glue.h: $(top_srcdir)/introspection/nm-device.xml
	dbus-binding-tool --prefix=nm_device_interface --mode=glib-server --output=$@ $<

nm-device-ethernet-glue.h: $(top_srcdir)/introspection/nm-device-ethernet.xml
	dbus-binding-tool --prefix=nm_device_ethernet --mode=glib-server --output=$@ $<

nm-device-wifi-glue.h: $(top_srcdir)/introspection/nm-device-wifi.xml
	dbus-binding-tool --prefix=nm_device_wifi --mode=glib-server --output=$@ $<

nm-device-bt-glue.h: $(top_srcdir)/introspection/nm-device-bt.xml
	dbus-binding-tool --prefix=nm_device_bt --mode=glib-server --output=$@ $<

nm-device-olpc-mesh-glue.h: $(top_srcdir)/introspection/nm-device-olpc-mesh.xml
	dbus-binding-tool --prefix=nm_device_olpc_mesh --mode=glib-server --output=$@ $<

nm-ip4-config-glue.h: $(top_srcdir)/introspection/nm-ip4-config.xml
	dbus-binding-tool --prefix=nm_ip4_config --mode=glib-server --output=$@ $<

nm-ip6-config-glue.h: $(top_srcdir)/introspection/nm-ip6-config.xml
	dbus-binding-tool --prefix=nm_ip6_config --mode=glib-server --output=$@ $<

nm-active-connection-glue.h: $(top_srcdir)/introspection/nm-active-connection.xml
	dbus-binding-tool --prefix=nm_active_connection --mode=glib-server --output=$@ $<

nm-dhcp4-config-glue.h: $(top_srcdir)/introspection/nm-dhcp4-config.xml
	dbus-binding-tool --prefix=nm_dhcp4_config --mode=glib-server --output=$@ $<

BUILT_SOURCES = \
	nm-access-point-glue.h \
	nm-manager-glue.h \
	nm-device-interface-glue.h \
	nm-device-ethernet-glue.h \
	nm-device-wifi-glue.h \
	nm-device-olpc-mesh-glue.h \
	nm-device-bt-glue.h \
	nm-ip4-config-glue.h \
	nm-ip6-config-glue.h \
	nm-active-connection-glue.h \
	nm-dhcp4-config-glue.h

NetworkManager_CPPFLAGS = \
	$(DBUS_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(HAL_CFLAGS) \
	$(GUDEV_CFLAGS) \
	$(OPENSSL_CFLAGS) \
	$(LIBNL_CFLAGS) \
	$(GMODULE_CFLAGS) \
	-DG_DISABLE_DEPRECATED \
	-DBINDIR=\"$(bindir)\" \
	-DSBINDIR=\"$(sbindir)\" \
	-DLIBEXECDIR=\"$(libexecdir)\" \
	-DDATADIR=\"$(datadir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DNM_RUN_DIR=\"$(rundir)\" \
	-DNMLOCALEDIR=\"$(datadir)/locale\" \
	-DARP_DEBUG

NetworkManager_LDADD = \
	$(top_builddir)/marshallers/libmarshallers.la \
	./named-manager/libnamed-manager.la \
	./vpn-manager/libvpn-manager.la \
	./dhcp-manager/libdhcp-manager.la \
	./ip6-manager/libip6-manager.la \
	./supplicant-manager/libsupplicant-manager.la \
	./dnsmasq-manager/libdnsmasq-manager.la \
	./ppp-manager/libppp-manager.la \
	./modem-manager/libmodem-manager.la \
	./bluez-manager/libbluez-manager.la \
	./system-settings/libsystem-settings.la \
	./backends/libnmbackend.la \
	$(top_builddir)/libnm-util/libnm-util.la \
	$(DBUS_LIBS) \
	$(GLIB_LIBS) \
	$(GUDEV_LIBS) \
	$(LIBNL_LIBS) \
	$(GMODULE_LIBS)

NetworkManager_LDFLAGS = -rdynamic

libexec_PROGRAMS = nm-crash-logger
nm_crash_logger_SOURCES = nm-crash-logger.c
nm_crash_logger_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	-DG_DISABLE_DEPRECATED \
	-DBINDIR=\"$(bindir)\" \
	-DSBINDIR=\"$(sbindir)\" \
	-DDATADIR=\"$(datadir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DLOCALSTATEDIR=\"$(localstatedir)\"
nm_crash_logger_LDADD = $(GLIB_LIBS)


NetworkManagerdir = $(datadir)/NetworkManager
NetworkManager_DATA = gdb-cmd

dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = NetworkManager.conf

EXTRA_DIST = \
	$(dbusservice_DATA) \
	$(NetworkManager_DATA)

rundir=$(localstatedir)/run/NetworkManager
install-data-hook:
	$(mkinstalldirs) -m 0700 $(DESTDIR)$(rundir)

CLEANFILES = $(BUILT_SOURCES)

