/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-web-application.h
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

#ifndef _EPHY_WEB_APPLICATION_H
#define _EPHY_WEB_APPLICATION_H

#include "ephy-embed.h"

#include <glib-object.h>
#include <webkit/webkit.h>

G_BEGIN_DECLS

#define EPHY_TYPE_WEB_APPLICATION              ephy_web_application_get_type()
#define EPHY_WEB_APPLICATION(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), EPHY_TYPE_WEB_APPLICATION, EphyWebApplication))
#define EPHY_WEB_APPLICATION_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), EPHY_TYPE_WEB_APPLICATION, EphyWebApplicationClass))
#define EPHY_IS_WEB_APPLICATION(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EPHY_TYPE_WEB_APPLICATION))
#define EPHY_IS_WEB_APPLICATION_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), EPHY_TYPE_WEB_APPLICATION))
#define EPHY_WEB_APPLICATION_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), EPHY_TYPE_WEB_APPLICATION, EphyWebApplicationClass))

typedef enum
{
  EPHY_WEB_APPLICATION_EMPTY,
  EPHY_WEB_APPLICATION_LOADING,
  EPHY_WEB_APPLICATION_TEMPORARY,
  EPHY_WEB_APPLICATION_INSTALLED
} EphyWebApplicationStatus;

typedef enum
{
  EPHY_WEB_APPLICATION_CANCELLED, /* mozilla: denied */
  EPHY_WEB_APPLICATION_FORBIDDEN, /* mozilla: permissionDenied */
  EPHY_WEB_APPLICATION_MANIFEST_URL_ERROR, /* mozilla: manifestURLError */
  EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, /* mozilla: manifestParseError */
  EPHY_WEB_APPLICATION_MANIFEST_INVALID, /* mozilla: invalidManifest */
  EPHY_WEB_APPLICATION_NETWORK, /* mozilla: networkError */
  EPHY_WEB_APPLICATION_CRX_EXTRACT_FAILED, /* Chrome webstore CRX: couldn't extract */
  EPHY_WEB_APPLICATION_CHROME_EXTENSIONS_UNSUPPORTED, /* Chrome webstore CRX: extensions are not supported */
  EPHY_WEB_APPLICATION_UNSUPPORTED_PERMISSIONS, /* Chrome manifest: permissions are not supported */
} EphyWebApplicationError;

#define EPHY_WEB_APPLICATION_ERROR_QUARK (g_quark_from_static_string ("ephy-web-application-error"))

/* Files */
#define EPHY_WEB_APPLICATION_METADATA_FILE "ephy-web-app.metadata"
#define EPHY_WEB_APPLICATION_DESKTOP_FILE "ephy-web-app.desktop"
#define EPHY_WEB_APPLICATION_APP_ICON "app-icon.png"
#define EPHY_WEB_APPLICATION_COOKIE_JAR "cookies.sqlite"


typedef struct _EphyWebApplication EphyWebApplication;
typedef struct _EphyWebApplicationClass EphyWebApplicationClass;
typedef struct _EphyWebApplicationPrivate EphyWebApplicationPrivate;

struct _EphyWebApplication
{
  GObject parent;

  EphyWebApplicationPrivate *priv;
};

struct _EphyWebApplicationClass
{
  GObjectClass parent_class;
};


GType         ephy_web_application_get_type              (void) G_GNUC_CONST;

EphyWebApplication *ephy_web_application_new             (void);
gboolean                ephy_web_application_load        (EphyWebApplication *app,
                                                          const char *profile_dir,
                                                          GError **error);
gboolean                ephy_web_application_delete      (EphyWebApplication *app,
                                                          GError **error);
gboolean                ephy_web_application_install     (EphyWebApplication *app,
                                                          GdkPixbuf *icon,
                                                          GError **error);
gboolean                ephy_web_application_launch      (EphyWebApplication *app);

const char *        ephy_web_application_get_name        (EphyWebApplication *app);
void                ephy_web_application_set_name        (EphyWebApplication *app,
                                                          const char *name);
const char *        ephy_web_application_get_description (EphyWebApplication *app);
void                ephy_web_application_set_description (EphyWebApplication *app,
                                                          const char *description);
const char *        ephy_web_application_get_author      (EphyWebApplication *app);
void                ephy_web_application_set_author      (EphyWebApplication *app,
                                                          const char *author);
const char *        ephy_web_application_get_author_url  (EphyWebApplication *app);
void                ephy_web_application_set_author_url  (EphyWebApplication *app,
                                                          const char *author_url);
const char *        ephy_web_application_get_origin      (EphyWebApplication *app);
void                ephy_web_application_set_origin      (EphyWebApplication *app,
                                                          const char *origin);
const char *        ephy_web_application_get_uri_regex   (EphyWebApplication *app);
void                ephy_web_application_set_uri_regex   (EphyWebApplication *app,
							  const char *uri_regex);
GList *             ephy_web_application_get_permissions (EphyWebApplication *app);
void                ephy_web_application_set_permissions (EphyWebApplication *app,
							  GList *permissions);
gboolean            ephy_web_application_match_permission (EphyWebApplication *app,
							   const char *permission);
gboolean            ephy_web_application_match_uri       (EphyWebApplication *app,
							  const char *uri);
const char *        ephy_web_application_get_install_origin (EphyWebApplication *app);
void                ephy_web_application_set_install_origin (EphyWebApplication *app,
                                                             const char *install_origin);
const char *        ephy_web_application_get_launch_path (EphyWebApplication *app);
void                ephy_web_application_set_launch_path (EphyWebApplication *app,
                                                          const char *launch_path);
const char *        ephy_web_application_get_options_path(EphyWebApplication *app);
void                ephy_web_application_set_options_path(EphyWebApplication *app,
                                                          const char *launch_path);

void                ephy_web_application_set_full_uri    (EphyWebApplication *app,
                                                          const char *full_uri);
char *              ephy_web_application_get_full_uri    (EphyWebApplication *app);
char *              ephy_web_application_get_options_uri (EphyWebApplication *app);

EphyWebApplicationStatus ephy_web_application_get_status (EphyWebApplication *app);
void                ephy_web_application_set_status (EphyWebApplication *app,
                                                     EphyWebApplicationStatus status);

const char *        ephy_web_application_get_install_date(EphyWebApplication *app);
const char *        ephy_web_application_get_profile_dir (EphyWebApplication *app);

char *              ephy_web_application_get_settings_file_name (EphyWebApplication *app,
                                                                 const char *base);

const char *        ephy_web_application_get_custom_key (EphyWebApplication *app,
                                                         const char *key);
void                ephy_web_application_set_custom_key (EphyWebApplication *app,
                                                         const char *key,
                                                         const char *value);


EphyWebApplication * ephy_web_application_get_self (void);
GList * ephy_web_application_get_applications (void);
EphyWebApplication * ephy_web_application_from_name (const char *name);
GList * ephy_web_application_get_applications_from_origin (const char *origin);
GList * ephy_web_application_get_applications_from_install_origin (const char *install_origin);
void    ephy_web_application_free_applications_list (GList *applications);
char * ephy_web_application_get_profile_dir_from_name (const char *name);

/* SHOULD GET PRIVATE ONCE FINISHED */
char *ephy_web_application_get_wm_class_from_app_title (const char *title);


char *  ephy_apps_dot_dir (void);


G_END_DECLS

#endif /* _EPHY_WEB_APPLICATION_H */
