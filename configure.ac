AC_PREREQ(2.52)

AC_INIT(NetworkManager, 0.7.996, dcbw@redhat.com, NetworkManager)
AM_INIT_AUTOMAKE([1.9 subdir-objects tar-ustar no-dist-gzip dist-bzip2])
AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

dnl Define _GNU_SOURCE for various things like strcasestr()
AC_GNU_SOURCE

dnl
dnl Require programs
dnl
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LIBTOOL

dnl ensure that when the Automake generated makefile calls aclocal,
dnl it honours the $ACLOCAL_FLAGS environment variable
ACLOCAL_AMFLAGS="\${ACLOCAL_FLAGS}"
if test -n "$ac_macro_dir"; then
    ACLOCAL_AMFLAGS="-I $ac_macro_dir $ACLOCAL_AMFLAGS"
fi
AC_SUBST([ACLOCAL_AMFLAGS])

dnl maintainer mode stuff
if test $USE_MAINTAINER_MODE = yes; then
	DISABLE_DEPRECATED="-DG_DISABLE_DEPRECATED -DGCONF_DISABLE_DEPRECATED"
else
	DISABLE_DEPRECATED=""
fi
AC_SUBST(DISABLE_DEPRECATED)

dnl
dnl Required headers
dnl
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h paths.h sys/ioctl.h sys/time.h syslog.h unistd.h)

dnl
dnl Checks for typedefs, structures, and compiler characteristics.
dnl
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_HEADER_TIME

dnl
dnl Checks for library functions.
dnl
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MEMCMP
AC_CHECK_FUNCS(select socket uname)

dnl
dnl translation support
dnl
GETTEXT_PACKAGE=NetworkManager
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])
IT_PROG_INTLTOOL([0.35.0])
AM_GLIB_GNU_GETTEXT

dnl
dnl Documentation
dnl
AC_ARG_WITH(docs, AS_HELP_STRING([--with-docs], [Build NetworkManager documentation]))
AM_CONDITIONAL(WITH_DOCS, test "x$with_docs" = "xyes")
case $with_docs in
    yes)
        enable_gtk_doc=yes
        ;;
    *)
        with_docs=no
        ;;
esac

dnl
dnl Make sha1.c happy on big endian systems
dnl
AC_C_BIGENDIAN

AC_ARG_WITH(distro, AS_HELP_STRING([--with-distro=DISTRO], [Specify the Linux distribution to target: One of redhat, suse, gentoo, debian, arch, slackware, paldo or mandriva]))
if test "z$with_distro" = "z"; then
	AC_CHECK_FILE(/etc/redhat-release,with_distro="redhat")
	AC_CHECK_FILE(/etc/SuSE-release,with_distro="suse")
	AC_CHECK_FILE(/etc/fedora-release,with_distro="redhat")
	AC_CHECK_FILE(/etc/gentoo-release,with_distro="gentoo")
	AC_CHECK_FILE(/etc/debian_version,with_distro="debian")
	AC_CHECK_FILE(/etc/arch-release,with_distro="arch")
	AC_CHECK_FILE(/etc/slackware-version,with_distro="slackware")
	AC_CHECK_FILE(/etc/frugalware-release,with_distro="frugalware")
	AC_CHECK_FILE(/etc/mandriva-release,with_distro="mandriva")
	if test "z$with_distro" = "z"; then
		with_distro=`lsb_release -is`
	fi
fi
with_distro=`echo ${with_distro} | tr '[[:upper:]]' '[[:lower:]]' `

if test "z$with_distro" = "z"; then
	echo "Linux distribution autodetection failed, you must specify the distribution to target using --with-distro=DISTRO"
	exit 1
else
	case $with_distro in
		redhat|suse|gentoo|debian|slackware|arch|paldo|frugalware|mandriva) ;;
		*)
			echo "Your distribution (${with_distro}) is not yet supported!  (patches welcome)"
			exit 1
			;;
	esac
fi

AM_CONDITIONAL(TARGET_REDHAT, test x"$with_distro" = xredhat)
if test x"$with_distro" = xredhat; then
  AC_DEFINE(TARGET_REDHAT, 1, [Define if you have Fedora or RHEL])
fi

AM_CONDITIONAL(TARGET_SUSE, test x"$with_distro" = xsuse)
if test x"$with_distro" = xsuse; then
  AC_DEFINE(TARGET_SUSE, 1, [Define if you have OpenSUSE or SLES])
fi

AM_CONDITIONAL(TARGET_GENTOO, test x"$with_distro" = xgentoo)
if test x"$with_distro" = xgentoo; then
  AC_DEFINE(TARGET_GENTOO, 1, [Define if you have Gentoo])
fi

AM_CONDITIONAL(TARGET_DEBIAN, test x"$with_distro" = xdebian)
if test x"$with_distro" = xdebian; then
  AC_DEFINE(TARGET_DEBIAN, 1, [Define if you have Debian])
fi

AM_CONDITIONAL(TARGET_SLACKWARE, test x"$with_distro" = xslackware)
if test x"$with_distro" = xslackware; then
  AC_DEFINE(TARGET_SLACKWARE, 1, [Define if you have Slackware])
fi

AM_CONDITIONAL(TARGET_ARCH, test x"$with_distro" = xarch)
if test x"$with_distro" = xarch; then
  AC_DEFINE(TARGET_ARCH, 1, [Define if you have Arch])
fi

AM_CONDITIONAL(TARGET_PALDO, test x"$with_distro" = xpaldo)
if test x"$with_distro" = xpaldo; then
  AC_DEFINE(TARGET_PALDO, 1, [Define if you have Paldo])
fi

AM_CONDITIONAL(TARGET_FRUGALWARE, test x"$with_distro" = xfrugalware)
if test x"$with_distro" = xfrugalware; then
  AC_DEFINE(TARGET_FRUGALWARE, 1, [Define if you have Frugalware])
fi

AM_CONDITIONAL(TARGET_MANDRIVA, test x"$with_distro" = xmandriva)
if test x"$with_distro" = xmandriva; then
  AC_DEFINE(TARGET_MANDRIVA, 1, [Define if you have Mandriva])
fi

AC_MSG_CHECKING([Linux Wireless Extensions >= 18])
AC_TRY_COMPILE([#ifndef __user
                #define __user
                #endif
                #include <sys/types.h>
                #include <linux/types.h>
                #include <sys/socket.h>
                #include <net/if.h>
                #include <wireless.h>],
               [#ifndef IWEVGENIE
                #error "not found"
                #endif],
               [ac_have_iwevgenie=yes],
               [ac_have_iwevgenie=no])
AC_MSG_RESULT($ac_have_iwevgenie)
if test "$ac_have_iwevgenie" = no; then
	AC_MSG_ERROR(wireless-tools library and development headers >= 28pre9 not installed or not functional)
fi

PKG_CHECK_MODULES(DBUS, dbus-1 >= 1.1 dbus-glib-1 >= 0.75)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(GLIB, gthread-2.0 glib-2.0 >= 2.16 gobject-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GMODULE, gmodule-2.0)
AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

PKG_CHECK_MODULES(GUDEV, gudev-1.0)
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

PKG_CHECK_MODULES(GIO, gio-2.0)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

AC_ARG_WITH(udev-dir, AS_HELP_STRING([--with-udev-dir=DIR], [where the udev base directory is]))
if test -n "$with_udev_dir" ; then
	UDEV_BASE_DIR="$with_udev_dir"
else
	UDEV_BASE_DIR="/lib/udev"
fi
AC_SUBST(UDEV_BASE_DIR)

PKG_CHECK_MODULES(LIBNL, libnl-1 >= 1.0-pre8)
AC_SUBST(LIBNL_CFLAGS)
AC_SUBST(LIBNL_LIBS)
NM_LIBNL_CHECK

PKG_CHECK_MODULES(UUID, uuid)
AC_SUBST(UUID_CFLAGS)
AC_SUBST(UUID_LIBS)

PKG_CHECK_MODULES(POLKIT, polkit-gobject-1)

AC_SUBST(POLKIT_CFLAGS)

AC_ARG_WITH(crypto, AS_HELP_STRING([--with-crypto=nss | gnutls], [Cryptography library to use for certificate and key operations]),ac_crypto=$withval, ac_crypto=nss)

with_nss=no
with_gnutls=no
if test x"$ac_crypto" = xnss; then
  PKG_CHECK_MODULES(NSS, [nss >= 3.11])
  AC_DEFINE(HAVE_NSS, 1, [Define if you have NSS])
  with_nss=yes
elif test x"$ac_crypto" = xgnutls; then
  PKG_CHECK_MODULES(GNUTLS, [gnutls >= 1.2])
  AC_PATH_PROG(LIBGCRYPT_CONFIG, libgcrypt-config, no)
  if test x"$LIBGCRYPT_CONFIG" = xno; then
    AC_MSG_ERROR([gnutls explicitly requested but gcrypt not found on system])
  else
    AC_DEFINE(HAVE_GNUTLS, 1, [Define if you have libgnutls])
    LIBGCRYPT_CFLAGS=`$LIBGCRYPT_CONFIG --cflags`
    LIBGCRYPT_LIBS=`$LIBGCRYPT_CONFIG --libs`
    AC_SUBST(LIBGCRYPT_CFLAGS)
    AC_SUBST(LIBGCRYPT_LIBS)
    with_gnutls=yes
  fi
else
  AC_MSG_ERROR([Please choose either 'nss' or 'gnutls' for certificate and key operations])
fi
AM_CONDITIONAL(WITH_NSS, test x"$with_nss" != xno)
AM_CONDITIONAL(WITH_GNUTLS, test x"$with_gnutls" != xno)

# Shouldn't ever trigger this, but just in case...
if test x"$ac_nss" = xno -a x"$ac_gnutls" = xno; then
  AC_MSG_ERROR([Could not find required development headers and libraries for '$ac_crypto'])
fi

GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

AC_ARG_WITH(dbus-sys-dir, AS_HELP_STRING([--with-dbus-sys-dir=DIR], [where D-BUS system.d directory is]))

if test -n "$with_dbus_sys_dir" ; then
    DBUS_SYS_DIR="$with_dbus_sys_dir"
else
    DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

# PPPD
AC_CHECK_HEADERS(pppd/pppd.h,,
  AC_MSG_ERROR(couldn't find pppd.h. pppd development headers are required.))

AC_ARG_WITH([pppd-plugin-dir], AS_HELP_STRING([--with-pppd-plugin-dir=DIR], [path to the pppd plugins directory]))

if test -n "$with_pppd_plugin_dir" ; then
	PPPD_PLUGIN_DIR="$with_pppd_plugin_dir"
else
	PPPD_PLUGIN_DIR="${libdir}/pppd/2.4.4"
fi
AC_SUBST(PPPD_PLUGIN_DIR)

# DHCP client
AC_ARG_WITH([dhcp-client], AS_HELP_STRING([--with-dhcp-client=dhcpcd|dhclient], [path to the chosen dhcp client]))
# If a full path is given, use that and do not test if it works or not.
case "${with_dhcp_client}" in
	/*)
		DHCP_CLIENT_PATH="${with_dhcp_client}"
		AC_MSG_NOTICE(using the DHCP client ${DHCP_CLIENT_PATH})
		;;
esac
if test -z "$DHCP_CLIENT_PATH" -a \( -z "$with_dhcp_client" -o x`basename "$with_dhcp_client"` = "xdhclient" \); then
	# We only work with ISC dhclient - the FreeBSD and OpenBSD derivatives don't have the same userland.
	AC_MSG_CHECKING(for dhclient)
	for client in "$with_dhcp_client" /sbin/dhclient /usr/pkg/sbin/dhclient /usr/local/sbin/dhclient; do
		test -x "$client" || continue
		case `"$client" --version 2>&1` in
			"isc-dhclient-"*) DHCP_CLIENT_PATH="$client"; break;;
		esac
	done
	if test -z "$DHCP_CLIENT_PATH"; then
		AC_MSG_RESULT(no)
		if test -n "$with_dhcp_client"; then
			AC_MSG_ERROR([Could not find ISC dhclient])
		fi
	else
		AC_MSG_RESULT($DHCP_CLIENT_PATH)
	fi
fi
if test -z "$DHCP_CLIENT_PATH" -a \( -z "$with_dhcp_client" -o x`basename "$with_dhcp_client"` = "xdhcpcd" \); then
	test -n "$DHCP_CLIENT_PATH" && echo bar
	# We fully work with upstream dhcpcd-4
	AC_MSG_CHECKING([for dhcpcd])
	for client in "$with_dhcp_client" /sbin/dhcpcd /usr/pkg/sbin/dhcpcd /usr/local/sbin/dhcpcd; do
		test -x "$client" || continue
		case `"$client" --version 2>/dev/null` in
			"dhcpcd "[123]*);;
			"dhcpcd "*) DHCP_CLIENT_PATH="$client"; break;;
		esac
	done
	if test -z "$DHCP_CLIENT_PATH"; then
		AC_MSG_RESULT(no)
		if test -n "$with_dhcp_client"; then
			AC_MSG_ERROR([Could not find dhcpcd-4 or newer])
		fi
	else
		AC_MSG_RESULT($DHCP_CLIENT_PATH)
	fi
fi
if test -z "$DHCP_CLIENT_PATH"; then
	# DHCP clients are not a build time dependency, only runtime.
	# dhclient has been the longtime default for NM and it's in /sbin
	# in most distros, so use it.
	AC_MSG_WARN([Could not find a suitable DHCP client])
	DHCP_CLIENT_PATH=/sbin/dhclient
	AC_MSG_WARN([Falling back to ISC dhclient, ${DHCP_CLIENT_PATH}])
fi
AC_SUBST(DHCP_CLIENT_PATH)
DHCP_CLIENT=`basename "$DHCP_CLIENT_PATH"`
if test "$DHCP_CLIENT" != "dhclient" -a "$DHCP_CLIENT" != "dhcpcd"; then
	AC_MSG_ERROR([No backend for the DHCP client ${DHCP_CLIENT}])
fi
AC_SUBST(DHCP_CLIENT)

# resolvconf support
AC_ARG_WITH([resolvconf],
	    AS_HELP_STRING([--with-resolvconf=yes|no|path], [Enable resolvconf support]),
	    with_resolvconf="$withval",with_resolvconf=no)
# If a full path is given, use that and do not test if it works or not.
case "${with_resolvconf}" in
	/*)
		RESOLVCONF_PATH="${with_resolvconf}"
		AC_MSG_NOTICE(setting resolvconf path to ${RESOLVCONF_PATH})
		;;
	no)	AC_MSG_NOTICE(resolvconf support disabled)
		;;
	*)
		AC_MSG_CHECKING(for resolvconf)
		for path in /sbin /usr/sbin /usr/pkg/sbin /usr/local/sbin; do
			if test -x "${path}/resolvconf"; then
				RESOLVCONF_PATH="${path}/resolvconf"
				break
			fi
		done
		if test -n "${RESOLVCONF_PATH}"; then
			AC_MSG_RESULT($RESOLVCONF_PATH)
		else
			AC_MSG_RESULT(no)
		fi
		;;
esac
AC_SUBST(RESOLVCONF_PATH)
if test -n "${RESOLVCONF_PATH}"; then
	AC_DEFINE_UNQUOTED(RESOLVCONF_PATH, "$RESOLVCONF_PATH", [Define if you have a resolvconf implementation])
fi

# iptables path
AC_ARG_WITH(iptables, AC_HELP_STRING([--with-iptables=/path/to/iptables], [path to iptables])) 
if test "x${with_iptables}" = x; then
  AC_PATH_PROG(IPTABLES_PATH, iptables, [], $PATH:/sbin:/usr/sbin)
  if ! test -x "$IPTABLES_PATH"; then
        AC_MSG_ERROR(iptables was not installed.)
  fi
else
  IPTABLES_PATH="$with_iptables"
fi
AC_DEFINE_UNQUOTED(IPTABLES_PATH, "$IPTABLES_PATH", [Define to path of iptables binary])
AC_SUBST(IPTABLES_PATH)

# system CA certificates path
AC_ARG_WITH(system-ca-path, AS_HELP_STRING([--with-system-ca-path=/path/to/ssl/certs], [path to system CA certificates])) 
if test "x${with_system_ca_path}" = x; then
  SYSTEM_CA_PATH=/etc/ssl/certs
else
  SYSTEM_CA_PATH="$with_system_ca_path"
fi
AC_DEFINE_UNQUOTED(SYSTEM_CA_PATH, "$SYSTEM_CA_PATH", [Define to path to system CA certificates])
AC_SUBST(SYSTEM_CA_PATH)

NM_COMPILER_WARNINGS

GTK_DOC_CHECK(1.0)

dnl
dnl Tests
dnl
AC_ARG_WITH(tests, AS_HELP_STRING([--with-tests], [Build NetworkManager tests]))
AM_CONDITIONAL(WITH_TESTS, test "x$with_tests" = "xyes")
case $with_tests in
    yes)
        with_tests=yes
        ;;
    *)
        with_tests=no
        ;;
esac


AC_CONFIG_FILES([
Makefile
include/Makefile
src/Makefile
src/tests/Makefile
marshallers/Makefile
src/named-manager/Makefile
src/vpn-manager/Makefile
src/dhcp-manager/Makefile
src/ip6-manager/Makefile
src/supplicant-manager/Makefile
src/supplicant-manager/tests/Makefile
src/ppp-manager/Makefile
src/dnsmasq-manager/Makefile
src/modem-manager/Makefile
src/bluez-manager/Makefile
src/system-settings/Makefile
src/backends/Makefile
libnm-util/libnm-util.pc
libnm-util/Makefile
libnm-util/tests/Makefile
libnm-util/tests/certs/Makefile
libnm-glib/libnm-glib.pc
libnm-glib/libnm-glib-vpn.pc
libnm-glib/Makefile
callouts/Makefile
tools/Makefile
system-settings/Makefile
system-settings/plugins/Makefile
system-settings/plugins/ifupdown/Makefile
system-settings/plugins/ifcfg-rh/Makefile
system-settings/plugins/ifcfg-rh/tests/Makefile
system-settings/plugins/ifcfg-rh/tests/network-scripts/Makefile
system-settings/plugins/ifcfg-suse/Makefile
system-settings/plugins/keyfile/Makefile
system-settings/plugins/keyfile/io/Makefile
system-settings/plugins/keyfile/tests/Makefile
system-settings/plugins/keyfile/tests/keyfiles/Makefile
test/Makefile
initscript/Makefile
initscript/RedHat/Makefile
initscript/RedHat/NetworkManager
initscript/Gentoo/Makefile
initscript/Gentoo/NetworkManager
initscript/Debian/Makefile
initscript/Debian/NetworkManager
initscript/Slackware/Makefile
initscript/Slackware/rc.networkmanager
initscript/SUSE/Makefile
initscript/SUSE/networkmanager
initscript/Arch/Makefile
initscript/Arch/networkmanager
initscript/paldo/Makefile
initscript/paldo/NetworkManager
initscript/Mandriva/Makefile
initscript/Mandriva/networkmanager
introspection/Makefile
man/Makefile
man/NetworkManager.8
man/nm-tool.1
po/Makefile.in
policy/Makefile
docs/Makefile
docs/libnm-glib/Makefile
docs/libnm-util/Makefile
NetworkManager.pc
])
AC_OUTPUT

echo
echo Distribution targeting: ${with_distro}
echo 'if this is not correct, please specifiy your distro with --with-distro=DISTRO'

echo
echo Building documentation: ${with_docs}
echo
echo Building tests: ${with_tests}
echo

