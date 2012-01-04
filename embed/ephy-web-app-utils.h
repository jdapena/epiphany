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

#include <glib.h>
#include <gtk/gtk.h>
#include <JavaScriptCore/JavaScript.h>

G_BEGIN_DECLS

typedef gboolean (*EphyWebApplicationInstallCallback) (gint dialog_response, 
						       const char *app_name,
						       const char *profile_dir,
						       gpointer userdata);

#define EPHY_WEB_APP_PREFIX "app-"
#define EPHY_WEB_APP_ICON_NAME "app-icon.png"

char    *ephy_web_application_create (const char *address, const char *name, const char *description, GdkPixbuf *icon);

gboolean ephy_web_application_delete (const char *name);

char    *ephy_web_application_get_profile_directory (const char *name);

GList   *ephy_web_application_get_application_list (void);

void     ephy_web_application_free_application_list (GList *list);

gboolean ephy_web_application_exists (const char *name);

void     ephy_web_application_show_install_dialog (GtkWindow *window,
						   const char *address,
                                                   const char *dialog_title,
                                                   const char *install_action,
                                                   const char *app_name,
                                                   const char *app_description,
                                                   const char *icon_href,
						   GdkPixbuf *icon_pixbuf,
						   EphyWebApplicationInstallCallback callback,
						   gpointer userdata);

void     ephy_web_application_install_manifest (GtkWindow *window,
						const char *origin,
						const char *manifest_path,
						const char *receipt,
						const char *install_origin);

void     ephy_web_application_setup_mozilla_api (JSGlobalContextRef context);

G_END_DECLS

#endif

