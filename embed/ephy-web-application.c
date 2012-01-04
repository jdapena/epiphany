/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-web-application.c
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

#include "config.h"
#include "ephy-web-application.h"

#include "ephy-debug.h"
#include "ephy-embed-type-builtins.h"
#include "ephy-file-helpers.h"

#include <errno.h>
#include <glib/gi18n.h>
#include <string.h>

#define ERROR_QUARK (g_quark_from_static_string ("ephy-web-application-error"))
#define EPHY_WEB_APP_PREFIX "app-"

G_DEFINE_TYPE (EphyWebApplication, ephy_web_application, G_TYPE_OBJECT)

#define EPHY_WEB_APPLICATION_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EPHY_TYPE_WEB_APPLICATION, EphyWebApplicationPrivate))

struct _EphyWebApplicationPrivate
{
  char *name;
  char *description;
  char *origin;
  char *install_origin;
  char *launch_path;
  char *install_date;
  char *profile_dir;
  EphyWebApplicationStatus status;
};

enum
{
  PROP_0,
  PROP_NAME,
  PROP_DESCRIPTION,
  PROP_ORIGIN,
  PROP_INSTALL_ORIGIN,
  PROP_LAUNCH_PATH,
  PROP_INSTALL_DATE,
  PROP_PROFILE_DIR,
  PROP_STATUS
};

static void
ephy_web_application_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EphyWebApplication *app;
  EphyWebApplicationPrivate *priv;

  app = EPHY_WEB_APPLICATION (object);
  priv = app->priv;

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, priv->name);
    break;
  case PROP_DESCRIPTION:
    g_value_set_string (value, priv->description);
    break;
  case PROP_ORIGIN:
    g_value_set_string (value, priv->origin);
    break;
  case PROP_INSTALL_ORIGIN:
    g_value_set_string (value, priv->install_origin);
    break;
  case PROP_LAUNCH_PATH:
    g_value_set_string (value, priv->launch_path);
    break;
  case PROP_INSTALL_DATE:
    g_value_set_string (value, priv->install_date);
    break;
  case PROP_PROFILE_DIR:
    g_value_set_string (value, priv->profile_dir);
    break;
  case PROP_STATUS:
    g_value_set_enum (value, priv->status);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
ephy_web_application_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EphyWebApplication *app;
  app = EPHY_WEB_APPLICATION (object);

  switch (property_id) {
  case PROP_NAME:
    ephy_web_application_set_name (app, g_value_get_string (value));
    break;
  case PROP_DESCRIPTION:
    ephy_web_application_set_description (app, g_value_get_string (value));
    break;
  case PROP_ORIGIN:
    ephy_web_application_set_origin (app, g_value_get_string (value));
    break;
  case PROP_INSTALL_ORIGIN:
    ephy_web_application_set_install_origin (app, g_value_get_string (value));
    break;
  case PROP_LAUNCH_PATH:
    ephy_web_application_set_launch_path (app, g_value_get_string (value));
    break;
  case PROP_STATUS:
    ephy_web_application_set_status (app, g_value_get_enum (value));
    break;
  case PROP_INSTALL_DATE:
  case PROP_PROFILE_DIR:
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

/**
 * ephy_web_application_get_name:
 * @app: an #EphyWebApplication
 *
 * Obtains the name of the application.
 *
 * Returns: a string
 **/
const char *
ephy_web_application_get_name (EphyWebApplication *app)
{
  return app->priv->name;
}

/**
 * ephy_web_application_set_name:
 * @app: an #EphyWebApplication
 * @name: a string
 *
 * Sets the @name of @app.
 **/
void
ephy_web_application_set_name (EphyWebApplication *app,
                               const char *name)
{
  g_free (app->priv->name);
  app->priv->name = g_strdup (name);
  g_object_notify (G_OBJECT (app), "name");
}

/**
 * ephy_web_application_get_description:
 * @app: an #EphyWebApplication
 *
 * Obtains the description of the application.
 *
 * Returns: a string
 **/
const char *
ephy_web_application_get_description (EphyWebApplication *app)
{
  return app->priv->description;
}

/**
 * ephy_web_application_set_description:
 * @app: an #EphyWebApplication
 * @description: a string
 *
 * Sets the @description of @app. It's displayed in
 * the about:applications list and also in the install
 * dialog.
 **/
void
ephy_web_application_set_description (EphyWebApplication *app,
                                      const char *description)
{
  g_free (app->priv->description);
  app->priv->description = g_strdup (description);
  g_object_notify (G_OBJECT (app), "description");
}

/**
 * ephy_web_application_get_origin:
 * @app: an #EphyWebApplication
 *
 * Obtains the origin of the application. See more
 * details on ephy_web_application_set_origin()
 *
 * Returns: a string
 **/
const char *
ephy_web_application_get_origin (EphyWebApplication *app)
{
  return app->priv->origin;
}

/**
 * ephy_web_application_set_origin:
 * @app: an #EphyWebApplication
 * @origin: a string
 *
 * Sets the @origin of @app. On launching the app, the URL used
 * is the sum of @origin and the launch path. It also implies
 * the limits of the web application. Any URL out of this origin
 * should imply opening the URL in standard browser.
 **/
void
ephy_web_application_set_origin (EphyWebApplication *app,
                                 const char *origin)
{
  g_free (app->priv->origin);
  app->priv->origin = g_strdup (origin);
  g_object_notify (G_OBJECT (app), "origin");
}

/**
 * ephy_web_application_get_install_origin:
 * @app: an #EphyWebApplication
 *
 * Obtains the install origin of the application. See more
 * details on ephy_web_application_set_install_origin()
 *
 * Returns: a string
 **/
const char *
ephy_web_application_get_install_origin (EphyWebApplication *app)
{
  return app->priv->install_origin;
}

/**
 * ephy_web_application_set_install_origin:
 * @app: an #EphyWebApplication
 * @install_origin: a string
 *
 * Sets the @install_origin of @app. It tells the origin of the
 * store or web the application was installed from.
 **/
void
ephy_web_application_set_install_origin (EphyWebApplication *app,
                                         const char *install_origin)
{
  g_free (app->priv->install_origin);
  app->priv->install_origin = g_strdup (install_origin);
  g_object_notify (G_OBJECT (app), "install-origin");
}

/**
 * ephy_web_application_get_launch_path:
 * @app: an #EphyWebApplication
 *
 * Obtains the launch path of the application. See more
 * details on ephy_web_application_set_launch_path()
 *
 * Returns: a string
 **/
const char *
ephy_web_application_get_launch_path (EphyWebApplication *app)
{
  return app->priv->launch_path;
}

/**
 * ephy_web_application_set_launch_path:
 * @app: an #EphyWebApplication
 * @launch_path: a string
 *
 * Sets the launch path of @app. When it's launched, it will run
 * the URI composed with the origin and @launch_path.
 **/
void
ephy_web_application_set_launch_path (EphyWebApplication *app,
                                      const char *launch_path)
{
  g_free (app->priv->launch_path);
  app->priv->launch_path = g_strdup (launch_path);
  g_object_notify (G_OBJECT (app), "launch-path");
}

EphyWebApplicationStatus
ephy_web_application_get_status (EphyWebApplication *app)
{
  return app->priv->status;
}

void
ephy_web_application_set_status (EphyWebApplication *app,
                                 EphyWebApplicationStatus status)
{
  app->priv->status = status;
  g_object_notify (G_OBJECT (app), "status");
}

const char *
ephy_web_application_get_install_date (EphyWebApplication *app)
{
  return app->priv->install_date;
}

const char *
ephy_web_application_get_profile_dir (EphyWebApplication *app)
{
  return app->priv->profile_dir;
}

char *
ephy_web_application_get_settings_file_name (EphyWebApplication *app,
                                             const char *base)
{
  EphyWebApplicationPrivate *priv;

  priv = app->priv;
  if (priv->profile_dir == NULL)
    return NULL;

  return g_build_filename (priv->profile_dir, base, NULL);
}

gboolean
ephy_web_application_is_mozilla_webapp (EphyWebApplication *app)
{
  char *manifest_path;
  GFile *file;
  gboolean result;

  manifest_path = g_build_filename (app->priv->profile_dir, EPHY_WEB_APPLICATION_MOZILLA_MANIFEST, NULL);
  file = g_file_new_for_path (manifest_path);
  result = g_file_query_exists (file, NULL);
  g_object_unref (file);
  g_free (manifest_path);

  return result;
}

gboolean
ephy_web_application_load (EphyWebApplication *app,
                           const char *profile_dir,
                           GError **error)
{
  EphyWebApplicationPrivate *priv = app->priv;
  char *metadata_file = NULL;
  char *contents = NULL;
  gboolean is_ok = TRUE;

  if (priv->status != EPHY_WEB_APPLICATION_EMPTY) {
    g_set_error (error, ERROR_QUARK, 0, "Tried to load a non empty application");
    is_ok = FALSE;
  }

  if (is_ok) {
    priv->profile_dir = g_strdup (profile_dir);
    metadata_file = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_METADATA_FILE);
    is_ok = g_file_get_contents (metadata_file, &contents, NULL, error);
  }

  if (is_ok) {
    GKeyFile *key = NULL;
    key = g_key_file_new ();
    is_ok = g_key_file_load_from_data (key, contents, -1, 0, error);

    if (is_ok) {
      priv->name = g_key_file_get_string (key, "Application", "Name", error);
      priv->origin = g_key_file_get_string (key, "Application", "Origin", error);
      is_ok = (priv->name != NULL) && (priv->origin != NULL);
    }
    if (is_ok) {
      priv->launch_path = g_key_file_get_string (key, "Application", "LaunchPath", NULL);
      priv->install_origin = g_key_file_get_string (key, "Application", "InstallOrigin", NULL);
      priv->description = g_key_file_get_string (key, "Application", "Description", NULL);
    }
    g_key_file_free (key);
  }

  if (is_ok) {
    GFile *file;
    GFileInfo *metadata_info;

    file = g_file_new_for_path (metadata_file);

    /* FIXME: this should use TIME_CREATED but it does not seem to be working. */
    metadata_info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, error);
    is_ok = (metadata_info != NULL);

    if (is_ok) {
      guint64 created;
      GDate *date;

      created = g_file_info_get_attribute_uint64 (metadata_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
      g_object_unref (metadata_info);
      date = g_date_new ();
      g_date_set_time_t (date, (time_t)created);
      priv->install_date = g_malloc0 (128);
      g_date_strftime (priv->install_date, 127, "%x", date);
      g_date_free (date);
    }
    g_object_unref (file);
  }

  g_object_notify (G_OBJECT (app), "name");
  g_object_notify (G_OBJECT (app), "origin");
  g_object_notify (G_OBJECT (app), "install_date");
  if (app->priv->description) g_object_notify (G_OBJECT (app), "description");
  if (app->priv->install_origin) g_object_notify (G_OBJECT (app), "install-origin");
  if (app->priv->launch_path) g_object_notify (G_OBJECT (app), "launch-path");

  ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_INSTALLED);

  g_free (contents);
  g_free (metadata_file);

  return is_ok;
}

static void
ephy_web_application_finalize (GObject *object)
{
  EphyWebApplication *app = EPHY_WEB_APPLICATION (object);
  EphyWebApplicationPrivate *priv;

  priv = app->priv;

  g_free (priv->name);
  g_free (priv->description);
  g_free (priv->origin);
  g_free (priv->install_origin);
  g_free (priv->launch_path);
  g_free (priv->install_date);
  g_free (priv->profile_dir);

  LOG ("EphyWebApplication finalised %p", object);

  G_OBJECT_CLASS (ephy_web_application_parent_class)->finalize (object);
}

static void
ephy_web_application_class_init (EphyWebApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EphyWebApplicationPrivate));

  object_class->get_property = ephy_web_application_get_property;
  object_class->set_property = ephy_web_application_set_property;
  object_class->finalize = ephy_web_application_finalize;

  /**
   * EphyWebApplication::name:
   *
   * Name of the application.
   */
  g_object_class_install_property (object_class, PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "App name",
                                                        "The name of the application",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * EphyWebApplication::description:
   *
   * Description of the application.
   */
  g_object_class_install_property (object_class, PROP_DESCRIPTION,
                                   g_param_spec_string ("description",
                                                        "App description",
                                                        "The description of the application",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * EphyWebApplication::origin:
   *
   * Origin of the application.
   */
  g_object_class_install_property (object_class, PROP_ORIGIN,
                                   g_param_spec_string ("origin",
                                                        "App origin",
                                                        "The origin URL of the application. Anything outside goes to browser",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * EphyWebApplication::install_origin:
   *
   * Install origin of the application.
   */
  g_object_class_install_property (object_class, PROP_INSTALL_ORIGIN,
                                   g_param_spec_string ("install-origin",
                                                        "App install origin",
                                                        "The origin used to install the app",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * EphyWebApplication::launch_path:
   *
   * Launch path of the application.
   */
  g_object_class_install_property (object_class, PROP_LAUNCH_PATH,
                                   g_param_spec_string ("launch-path",
                                                        "App launch path",
                                                        "The path inside origin for the initial URI",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * EphyWebApplication::install_date:
   *
   * The date the app was installed, or %NULL of still not installed.
   */
  g_object_class_install_property (object_class, PROP_INSTALL_DATE,
                                   g_param_spec_string ("install-date",
                                                        "Install date",
                                                        "Date and time the app was installed",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));
  /**
   * EphyWebApplication::profile_dir:
   *
   * Full path of the profile directory of the web application
   */
  g_object_class_install_property (object_class, PROP_PROFILE_DIR,
                                   g_param_spec_string ("profile-dir",
                                                        "Profile directory",
                                                        "Full path to the profile directory",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * EphyWebApplication::status:
   *
   * Current status of the application.
   */
  g_object_class_install_property (object_class, PROP_STATUS,
                                   g_param_spec_enum ("status",
                                                      "App status",
                                                      "Current status of the application",
                                                      EPHY_TYPE_WEB_APPLICATION_STATUS,
                                                      EPHY_WEB_APPLICATION_EMPTY,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

}

static void
ephy_web_application_init (EphyWebApplication *app)
{
  app->priv = EPHY_WEB_APPLICATION_GET_PRIVATE (app);

  LOG ("EphyWebApplication initialising %p", app);

  app->priv->name = NULL;
  app->priv->description = NULL;
  app->priv->origin = NULL;
  app->priv->install_origin = NULL;
  app->priv->launch_path = NULL;
  app->priv->profile_dir = NULL;
  app->priv->install_date = NULL;
  app->priv->status = EPHY_WEB_APPLICATION_EMPTY;

}

/**
 * ephy_web_application_new:
 *
 * Creates a new #EphyWebApplication.
 *
 * Returns: an #EphyWebApplication.
 **/
EphyWebApplication *
ephy_web_application_new (void)
{
  return g_object_new (EPHY_TYPE_WEB_APPLICATION, NULL);
}

char *
ephy_apps_dot_dir (void)
{
  return g_build_filename (g_get_home_dir (),
                           GNOME_DOT_GNOME,
                           "epiphany",
                           NULL);
}

/**
 * ephy_web_application_get_applications:
 *
 * Obtains the list of applications installed and enabled.
 *
 * Returns: a #GList of #EphyWebApplication instances
 */
GList *
ephy_web_application_get_applications (void)
{
  GFileEnumerator *children = NULL;
  GFileInfo *info;
  GList *applications = NULL;
  GFile *dot_dir;

  dot_dir = g_file_new_for_path (ephy_apps_dot_dir ());
  children = g_file_enumerate_children (dot_dir,
                                        "standard::name",
                                        0, NULL, NULL);
  g_object_unref (dot_dir);

  info = g_file_enumerator_next_file (children, NULL, NULL);
  while (info) {
    EphyWebApplication *app;
    const char *name;

    name = g_file_info_get_name (info);
    if (g_str_has_prefix (name, EPHY_WEB_APP_PREFIX)) {
      char *profile_dir;
      profile_dir = g_build_filename (ephy_dot_dir (), name, NULL);

      app = ephy_web_application_new ();
      if (ephy_web_application_load (app, profile_dir, NULL)) {
        applications = g_list_append (applications, app);
      } else {
        g_object_unref (app);
      }
      g_free (profile_dir);
    }

    g_object_unref (info);
    info = g_file_enumerator_next_file (children, NULL, NULL);
  }
  g_object_unref (children);

  return applications;
  
}

GList *
ephy_web_application_get_applications_from_origin (const char *origin)
{
  GList *applications, *node;
  GList *origin_apps;

  applications = ephy_web_application_get_applications ();

  origin_apps = NULL;
  for (node = applications; node != NULL; node = g_list_next (node)) {
    EphyWebApplication *app = (EphyWebApplication *) node->data;

    if (g_strcmp0 (ephy_web_application_get_origin (app), origin) == 0) {
      g_object_ref (app);
      origin_apps = g_list_append (origin_apps, app);
    }
  }
  
  ephy_web_application_free_applications_list (applications);

  return origin_apps;
}

void
ephy_web_application_free_applications_list (GList *applications)
{
  g_list_foreach (applications, (GFunc) g_object_unref, NULL);
  g_list_free (applications);
}
