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

#ifndef NM_SETTING_WIRED_H
#define NM_SETTING_WIRED_H

#include <nm-setting.h>

G_BEGIN_DECLS

#define NM_TYPE_SETTING_WIRED            (nm_setting_wired_get_type ())
#define NM_SETTING_WIRED(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_SETTING_WIRED, NMSettingWired))
#define NM_SETTING_WIRED_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_SETTING_WIRED, NMSettingWiredClass))
#define NM_IS_SETTING_WIRED(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_SETTING_WIRED))
#define NM_IS_SETTING_WIRED_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_SETTING_WIRED))
#define NM_SETTING_WIRED_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_SETTING_WIRED, NMSettingWiredClass))

#define NM_SETTING_WIRED_SETTING_NAME "802-3-ethernet"

typedef enum
{
	NM_SETTING_WIRED_ERROR_UNKNOWN = 0,
	NM_SETTING_WIRED_ERROR_INVALID_PROPERTY,
	NM_SETTING_WIRED_ERROR_MISSING_PROPERTY
} NMSettingWiredError;

#define NM_TYPE_SETTING_WIRED_ERROR (nm_setting_wired_error_get_type ()) 
GType nm_setting_wired_error_get_type (void);

#define NM_SETTING_WIRED_ERROR nm_setting_wired_error_quark ()
GQuark nm_setting_wired_error_quark (void);

#define NM_SETTING_WIRED_PORT "port"
#define NM_SETTING_WIRED_SPEED "speed"
#define NM_SETTING_WIRED_DUPLEX "duplex"
#define NM_SETTING_WIRED_AUTO_NEGOTIATE "auto-negotiate"
#define NM_SETTING_WIRED_MAC_ADDRESS "mac-address"
#define NM_SETTING_WIRED_MTU "mtu"

typedef struct {
	NMSetting parent;
} NMSettingWired;

typedef struct {
	NMSettingClass parent;

	/* Padding for future expansion */
	void (*_reserved1) (void);
	void (*_reserved2) (void);
	void (*_reserved3) (void);
	void (*_reserved4) (void);
} NMSettingWiredClass;

GType nm_setting_wired_get_type (void);

NMSetting        *nm_setting_wired_new                (void);
const char       *nm_setting_wired_get_port           (NMSettingWired *setting);
guint32           nm_setting_wired_get_speed          (NMSettingWired *setting);
const char       *nm_setting_wired_get_duplex         (NMSettingWired *setting);
gboolean          nm_setting_wired_get_auto_negotiate (NMSettingWired *setting);
const GByteArray *nm_setting_wired_get_mac_address    (NMSettingWired *setting);
guint32           nm_setting_wired_get_mtu            (NMSettingWired *setting);

G_END_DECLS

#endif /* NM_SETTING_WIRED_H */
