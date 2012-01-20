/*
 *  Copyright © 2011 Igalia S.L.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__EPHY_EPIPHANY_H_INSIDE__) && !defined (EPIPHANY_COMPILATION)
#error "Only <epiphany/epiphany.h> can be included directly."
#endif

#ifndef EPHY_WEB_APP_UTILS_H
#define EPHY_WEB_APP_UTILS_H

#include "ephy-web-application.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <JavaScriptCore/JavaScript.h>

G_BEGIN_DECLS

typedef gboolean (*EphyWebApplicationInstallCallback) (gint dialog_response,
                                                       EphyWebApplication *app,
                                                       gpointer userdata);
typedef void     (*EphyWebApplicationInstallManifestCallback) (GError *error,
                                                               gpointer userdata);

#define EPHY_WEB_APP_PREFIX "app-"
#define EPHY_WEB_APP_ICON_NAME "app-icon.png"

char    *ephy_web_application_create (const char *address, const char *name, const char *description, GdkPixbuf *icon);

void     ephy_web_application_show_install_dialog (GtkWindow *window,
                                                   const char *dialog_title,
                                                   const char *install_action,
                                                   EphyWebApplication *app,
                                                   const char *icon_href,
                                                   GdkPixbuf *icon_pixbuf,
                                                   EphyWebApplicationInstallCallback callback,
                                                   gpointer userdata);

void     ephy_web_application_install_manifest (GtkWindow *window,
						const char *origin,
						const char *manifest_path,
						const char *receipt,
                                                const char *install_origin,
                                                EphyWebApplicationInstallManifestCallback callback,
                                                gpointer userdata);
void     ephy_web_application_install_crx_extension (const char *origin,
                                                     const char *crx_path);

void     ephy_web_application_setup_mozilla_api (JSGlobalContextRef context);
void     ephy_web_application_setup_chrome_api  (JSGlobalContextRef context);

G_END_DECLS

#endif

