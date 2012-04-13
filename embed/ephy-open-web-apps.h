/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-open-web-apps.h
 * This file is part of Epiphany
 *
 * Copyright © 2012 - Igalia S.L.
 *
 * Epiphany is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Epiphany is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Epiphany; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#if !defined (__EPHY_EPIPHANY_H_INSIDE__) && !defined (EPIPHANY_COMPILATION)
#error "Only <epiphany/epiphany.h> can be included directly."
#endif

#ifndef _EPHY_OPEN_WEB_APPS_H
#define _EPHY_OPEN_WEB_APPS_H

#include "ephy-web-application.h"

#include <glib.h>
#include <gtk/gtk.h>

#define EPHY_WEB_APPLICATION_OPEN_WEB_APPS_MANIFEST "ephy-web-app.open-web-apps.manifest"
#define EPHY_WEB_APPLICATION_OPEN_WEB_APPS_RECEIPT "ephy-web-app.open-web-apps.receipt"

G_BEGIN_DECLS

typedef void     (*EphyOpenWebAppsInstallManifestCallback) (const char *origin,
							    GError *error,
							    gpointer userdata);
typedef void     (*EphyOpenWebAppsInstallManifestFromURICallback) (const char *origin,
								   GError *error,
								   gpointer userdata);


void     ephy_open_web_apps_install_manifest          (GtkWindow *window,
						       const char *origin,
						       const char *manifest_path,
						       const char *receipt,
						       const char *install_origin,
						       EphyOpenWebAppsInstallManifestCallback callback,
						       gpointer userdata);

void     ephy_open_web_apps_install_manifest_from_uri (const char *url,
						       const char *receipt,
						       const char *install_origin,
						       EphyOpenWebAppsInstallManifestFromURICallback callback,
						       gpointer userdata);

EphyWebApplication * ephy_open_web_apps_get_application_from_origin (const char *origin);

GList * ephy_open_web_apps_get_applications_from_install_origin (const char *install_origin);


G_END_DECLS

#endif /* _EPHY_OPEN_WEB_APPS_H */
