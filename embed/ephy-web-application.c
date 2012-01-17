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
#include <glib/gstdio.h>
#include <libsoup/soup-gnome.h>
#include <string.h>

#define ERROR_QUARK (g_quark_from_static_string ("ephy-web-application-error"))
#define EPHY_WEB_APP_PREFIX "app-"
#define EPHY_WEB_APP_DESKTOP_FILE_PREFIX "epiphany-"
#define EPHY_TOOLBARS_XML_FILE "epiphany-toolbars-3.xml"

#define EPHY_WEB_APP_TOOLBAR "<?xml version=\"1.0\"?>" \
                             "<toolbars version=\"1.1\">" \
                             "  <toolbar name=\"DefaultToolbar\" hidden=\"true\" editable=\"false\">" \
                             "    <toolitem name=\"NavigationBack\"/>" \
                             "    <toolitem name=\"NavigationForward\"/>" \
                             "    <toolitem name=\"ViewReload\"/>" \
                             "    <toolitem name=\"ViewCancel\"/>" \
                             "  </toolbar>" \
                             "</toolbars>"

G_DEFINE_TYPE (EphyWebApplication, ephy_web_application, G_TYPE_OBJECT)

#define EPHY_WEB_APPLICATION_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EPHY_TYPE_WEB_APPLICATION, EphyWebApplicationPrivate))

struct _EphyWebApplicationPrivate
{
  char *name;
  char *description;
  char *author;
  char *author_url;
  char *origin;
  char *install_origin;
  char *launch_path;
  char *install_date;
  char *profile_dir;
  EphyWebApplicationStatus status;
  GHashTable *custom_keys;
};

enum
{
  PROP_0,
  PROP_NAME,
  PROP_DESCRIPTION,
  PROP_AUTHOR,
  PROP_AUTHOR_URL,
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
  case PROP_AUTHOR:
    g_value_set_string (value, priv->author);
    break;
  case PROP_AUTHOR_URL:
    g_value_set_string (value, priv->author_url);
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
  case PROP_AUTHOR:
    ephy_web_application_set_author (app, g_value_get_string (value));
    break;
  case PROP_AUTHOR_URL:
    ephy_web_application_set_author_url (app, g_value_get_string (value));
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
 * ephy_web_application_get_author:
 * @app: an #EphyWebApplication
 *
 * Obtains the author of the application.
 *
 * Returns: a string
 **/
const char *
ephy_web_application_get_author (EphyWebApplication *app)
{
  return app->priv->author;
}

/**
 * ephy_web_application_set_author:
 * @app: an #EphyWebApplication
 * @author: a string
 *
 * Sets the @author of @app. It's displayed in
 * the about:applications list and also in the install
 * dialog.
 **/
void
ephy_web_application_set_author (EphyWebApplication *app,
                                 const char *author)
{
  g_free (app->priv->author);
  app->priv->author = g_strdup (author);
  g_object_notify (G_OBJECT (app), "author");
}

/**
 * ephy_web_application_get_author_url:
 * @app: an #EphyWebApplication
 *
 * Obtains the URL of the author of the application.
 *
 * Returns: a string
 **/
const char *
ephy_web_application_get_author_url (EphyWebApplication *app)
{
  return app->priv->author_url;
}

/**
 * ephy_web_application_set_author_url:
 * @app: an #EphyWebApplication
 * @author_url: a string
 *
 * Sets the @author_url of @app. It's displayed in
 * the about:applications list and also in the install
 * dialog.
 **/
void
ephy_web_application_set_author_url (EphyWebApplication *app,
                                     const char *author_url)
{
  g_free (app->priv->author_url);
  app->priv->author_url = g_strdup (author_url);
  g_object_notify (G_OBJECT (app), "author-url");
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
  if (priv->profile_dir == NULL && priv->name != NULL) {
    priv->profile_dir = ephy_web_application_get_profile_dir_from_name (priv->name);
  }

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

const char *
ephy_web_application_get_custom_key (EphyWebApplication *app,
				     const char *key)
{
  return (const char*) g_hash_table_lookup (app->priv->custom_keys, (gpointer) key);
}

void
ephy_web_application_set_custom_key (EphyWebApplication *app,
				     const char *key,
				     const char *value)
{
  g_hash_table_insert (app->priv->custom_keys, (gpointer) g_strdup (key), (gpointer) g_strdup (value));
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
      char **custom_keys;
      gsize length = 0, i;

      priv->launch_path = g_key_file_get_string (key, "Application", "LaunchPath", NULL);
      priv->install_origin = g_key_file_get_string (key, "Application", "InstallOrigin", NULL);
      priv->description = g_key_file_get_string (key, "Application", "Description", NULL);
      priv->author = g_key_file_get_string (key, "Application", "Author", NULL);
      priv->author_url = g_key_file_get_string (key, "Application", "AuthorURL", NULL);

      g_hash_table_remove_all (priv->custom_keys);

      custom_keys = g_key_file_get_keys (key, "CustomKeys", &length, NULL);
      for (i = 0; i < length; i++) {
        char *value;

        value = g_key_file_get_string (key, "CustomKeys", custom_keys[i], NULL);
        if (value) {
          ephy_web_application_set_custom_key (app, custom_keys[i], value);
          g_free (value);
        }
      }
      g_strfreev (custom_keys);

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
  if (app->priv->author) g_object_notify (G_OBJECT (app), "author");
  if (app->priv->author_url) g_object_notify (G_OBJECT (app), "author-url");
  if (app->priv->install_origin) g_object_notify (G_OBJECT (app), "install-origin");
  if (app->priv->launch_path) g_object_notify (G_OBJECT (app), "launch-path");

  ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_INSTALLED);

  g_free (contents);
  g_free (metadata_file);

  return is_ok;
}

/**
 * ephy_web_application_delete:
 * @app: an #EphyWebApplication
 * @error: return location for a #GError or %NULL
 * 
 * Deletes all the data associated with @app.
 * 
 * Returns: %TRUE if the @app was succesfully deleted, %FALSE otherwise
 **/
gboolean
ephy_web_application_delete (EphyWebApplication *app, GError **error)
{
  char *desktop_file = NULL, *desktop_path = NULL;
  char *wm_class;
  GFile *profile = NULL, *launcher = NULL;
  gboolean return_value = FALSE;

  if (ephy_web_application_get_status (app) != EPHY_WEB_APPLICATION_INSTALLED) {
    g_set_error (error, ERROR_QUARK, 0, "Tried to delete a non installed application");
    goto out;
  }

  profile = g_file_new_for_path (ephy_web_application_get_profile_dir (app));
  if (!ephy_file_delete_dir_recursively (profile, error))
    goto out;
  g_print ("Deleted application profile.\n");

  wm_class = ephy_web_application_get_wm_class_from_app_title (ephy_web_application_get_name (app));
  desktop_file = g_strconcat (wm_class, ".desktop", NULL);
  g_free (wm_class);
  desktop_path = g_build_filename (g_get_user_data_dir (), "applications", desktop_file, NULL);
  launcher = g_file_new_for_path (desktop_path);
  if (!g_file_delete (launcher, NULL, error))
    goto out;
  g_print ("Deleted application launcher.\n");

  ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_TEMPORARY);

  return_value = TRUE;

out:

  if (profile)
    g_object_unref (profile);

  if (launcher)
    g_object_unref (launcher);
  g_free (desktop_file);
  g_free (desktop_path);

  return return_value;
}

gboolean
ephy_web_application_launch (EphyWebApplication *app)
{
  char *desktop_file_path;
  gboolean result;
  char *uuid_envvar;

  uuid_envvar = g_strdup (g_getenv (EPHY_UUID_ENVVAR));
  g_unsetenv (EPHY_UUID_ENVVAR);

  desktop_file_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_DESKTOP_FILE);
  result = ephy_file_launch_desktop_file (desktop_file_path, NULL, 0, NULL);
  g_free (desktop_file_path);

  if (uuid_envvar) {
    g_setenv (EPHY_UUID_ENVVAR, uuid_envvar, FALSE);
    g_free (uuid_envvar);
  }
  return result;
}

static gboolean
create_desktop_and_metadata_files (EphyWebApplication *app,
                                   GdkPixbuf *icon,
                                   GError **error)
{
  EphyWebApplicationPrivate *priv;
  char *uri_string;
  GKeyFile *desktop_file, *metadata_file;
  char *exec_string;
  char *data;
  char *apps_path, *file_path;
  char *wm_class;
  gboolean is_ok = TRUE;

  g_return_val_if_fail (EPHY_IS_WEB_APPLICATION (app), FALSE);
  priv = app->priv;

  desktop_file = g_key_file_new ();
  metadata_file = g_key_file_new ();
  
  g_key_file_set_value (desktop_file, "Desktop Entry", "Name", priv->name);
  g_key_file_set_value (metadata_file, "Application", "Name", priv->name);

  if (priv->description) {
    g_key_file_set_value (metadata_file, "Application", "Description", priv->description);
    g_key_file_set_value (desktop_file, "Desktop Entry", "Comment", priv->description);
  }
  if (priv->author)
    g_key_file_set_value (metadata_file, "Application", "Author", priv->description);
  if (priv->author_url)
    g_key_file_set_value (metadata_file, "Application", "AuthorURL", priv->author_url);

  uri_string = g_strconcat (priv->origin, priv->launch_path, NULL);
  exec_string = g_strdup_printf ("epiphany --application-mode --profile=\"%s\" %s",
				 priv->profile_dir, 
                                 uri_string);
  g_key_file_set_value (desktop_file, "Desktop Entry", "Exec", exec_string);
  g_free (exec_string);
  g_free (uri_string);

  if (priv->launch_path) g_key_file_set_value (metadata_file, "Application", "LaunchPath", priv->launch_path);
  g_key_file_set_value (metadata_file, "Application", "Origin", priv->origin);
  if (priv->install_origin) g_key_file_set_value (metadata_file, "Application", "InstallOrigin", priv->install_origin);

  g_key_file_set_value (desktop_file, "Desktop Entry", "StartupNotification", "true");
  g_key_file_set_value (desktop_file, "Desktop Entry", "Terminal", "false");
  g_key_file_set_value (desktop_file, "Desktop Entry", "Type", "Application");

  {
    GList *custom_keys, *node;

    custom_keys = g_hash_table_get_keys (priv->custom_keys);
    for (node = custom_keys; node != NULL; node = g_list_next (node)) {
      char *key = (char *) node->data;
      if (key) {
        char *value = (char *) g_hash_table_lookup (priv->custom_keys, (gpointer) key);
        if (value) {
          g_key_file_set_value (metadata_file, "CustomKeys", key, value);
        }
      }
    }
    g_list_free (custom_keys);
  }

  if (icon) {
    GOutputStream *stream;
    char *path;
    GFile *image;

    path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_APP_ICON);
    image = g_file_new_for_path (path);

    stream = (GOutputStream*)g_file_create (image, 0, NULL, error);
    is_ok = (stream != NULL);
    if (is_ok) {
      is_ok = gdk_pixbuf_save_to_stream (icon, stream, "png", NULL, NULL, error);
    }

    if (is_ok) {
      g_key_file_set_value (desktop_file, "Desktop Entry", "Icon", path);
      g_key_file_set_value (metadata_file, "Application", "Icon", path);
    }

    if (stream) g_object_unref (stream);
    g_object_unref (image);
    g_free (path);
  }

  wm_class = ephy_web_application_get_wm_class_from_app_title (priv->name);
  g_key_file_set_value (desktop_file, "Desktop Entry", "StartupWMClass", wm_class);


  data = g_key_file_to_data (metadata_file, NULL, NULL);
  file_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_METADATA_FILE);
  g_key_file_free (metadata_file);

  if (is_ok) {
    is_ok = g_file_set_contents (file_path, data, -1, error);
  }
  g_free (file_path);
  g_free (data);

  data = g_key_file_to_data (desktop_file, NULL, NULL);
  file_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_DESKTOP_FILE);
  g_key_file_free (desktop_file);

  if (is_ok) {
    is_ok = g_file_set_contents (file_path, data, -1, error);
  }
  g_free (data);

  /* Create a symlink in XDG_DATA_DIR/applications for the Shell to
   * pick up this application. */
  apps_path = g_build_filename (g_get_user_data_dir (), "applications", NULL);
  if (is_ok) {
    is_ok = ephy_ensure_dir_exists (apps_path, error);
  }
  if (is_ok) {
    char *filename, *link_path;
    GFile *link;
    filename = g_strconcat (wm_class, ".desktop", NULL);
    link_path = g_build_filename (apps_path, filename, NULL);
    g_free (filename);
    link = g_file_new_for_path (link_path);
    g_free (link_path);
    is_ok = g_file_make_symbolic_link (link, file_path, NULL, NULL);
    g_object_unref (link);
  }
  g_free (wm_class);
  g_free (apps_path);

  return is_ok;
}

static gboolean
create_cookie_jar_for_domain (EphyWebApplication *app, GError **error)
{
  EphyWebApplicationPrivate *priv;
  SoupSession *session;
  GSList *cookies, *p;
  SoupCookieJar *current_jar, *new_jar;
  char *domain, *filename;
  SoupURI *uri;

  priv = app->priv;

  /* Create the new cookie jar */
  filename = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_COOKIE_JAR);
  new_jar = (SoupCookieJar*)soup_cookie_jar_sqlite_new (filename, FALSE);
  g_free (filename);

  /* The app domain for the current uri */
  uri = soup_uri_new (priv->origin);
  domain = uri->host;

  /* The current cookies */
  session = webkit_get_default_session ();
  current_jar = (SoupCookieJar*)soup_session_get_feature (session, SOUP_TYPE_COOKIE_JAR);
  cookies = soup_cookie_jar_all_cookies (current_jar);

  for (p = cookies; p; p = p->next) {
    SoupCookie *cookie = (SoupCookie*)p->data;

    if (g_str_has_suffix (cookie->domain, domain))
      soup_cookie_jar_add_cookie (new_jar, cookie);
    else
      soup_cookie_free (cookie);
  }

  soup_uri_free (uri);
  g_slist_free (cookies);

  return TRUE;
}

/**
 * ephy_web_application_install:
 * @app: an #EphyWebApplication
 * @icon: the icon for the new web application
 * @error: a #GError pointer, or %NULL
 * 
 * Installs @app into desktop, using @icon as the app icon.
 * 
 * Returns: %TRUE if install was successful, %FALSE otherwise
 **/
gboolean
ephy_web_application_install (EphyWebApplication *app,
                              GdkPixbuf *icon,
                              GError **error)
{
  EphyWebApplicationPrivate *priv;
  char *profile_dir = NULL;
  char *toolbar_path = NULL;
  gboolean result = FALSE;

  g_return_val_if_fail (EPHY_IS_WEB_APPLICATION (app), FALSE);
  priv = app->priv;
  if (ephy_web_application_get_status (app) != EPHY_WEB_APPLICATION_TEMPORARY)
    goto out;

  /* If there's already a WebApp profile for the contents of this
   * uri, do nothing. */
  g_free (priv->profile_dir);
  priv->profile_dir = ephy_web_application_get_profile_dir_from_name (ephy_web_application_get_name (app));

  /* Create the profile directory, populate it. */
  if (g_mkdir_with_parents (priv->profile_dir, 488) == -1) {
    g_set_error (error, ERROR_QUARK, 0, "Failed to create directory %s", profile_dir);
    goto out;
  }

  /* Things we need in a WebApp's profile:
     - Toolbar layout
     - Our own cookies file, copying the relevant cookies for the
       app's domain.
  */
  toolbar_path = g_build_filename (priv->profile_dir, EPHY_TOOLBARS_XML_FILE, NULL);
  if (!g_file_set_contents (toolbar_path, EPHY_WEB_APP_TOOLBAR, -1, error))
    goto out;

  if (!create_cookie_jar_for_domain (app, error)) {
    goto out;
  }

  /* Create the deskop file. */
  if (!create_desktop_and_metadata_files (app, icon, error))
    goto out;

  g_object_notify (G_OBJECT (app), "profile-dir");
  ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_INSTALLED);
  result = TRUE;

out:
  if (toolbar_path)
    g_free (toolbar_path);

  return result;
}


static void
ephy_web_application_finalize (GObject *object)
{
  EphyWebApplication *app = EPHY_WEB_APPLICATION (object);
  EphyWebApplicationPrivate *priv;

  priv = app->priv;

  g_free (priv->name);
  g_free (priv->description);
  g_free (priv->author);
  g_free (priv->author_url);
  g_free (priv->origin);
  g_free (priv->install_origin);
  g_free (priv->launch_path);
  g_free (priv->install_date);
  g_free (priv->profile_dir);
  g_hash_table_unref (priv->custom_keys);

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
   * EphyWebApplication::author:
   *
   * Author of the application.
   */
  g_object_class_install_property (object_class, PROP_AUTHOR,
                                   g_param_spec_string ("author",
                                                        "App author",
                                                        "The author of the application",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * EphyWebApplication::author-url:
   *
   * URL of the author of the application.
   */
  g_object_class_install_property (object_class, PROP_AUTHOR_URL,
                                   g_param_spec_string ("author-url",
                                                        "App author URL",
                                                        "The URL of the author of the application",
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
  app->priv->author = NULL;
  app->priv->author_url = NULL;
  app->priv->origin = NULL;
  app->priv->install_origin = NULL;
  app->priv->launch_path = NULL;
  app->priv->profile_dir = NULL;
  app->priv->install_date = NULL;
  app->priv->status = EPHY_WEB_APPLICATION_EMPTY;
  app->priv->custom_keys = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

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

GList *
ephy_web_application_get_applications_from_install_origin (const char *origin)
{
  GList *applications, *node;
  GList *origin_apps;

  applications = ephy_web_application_get_applications ();

  origin_apps = NULL;
  for (node = applications; node != NULL; node = g_list_next (node)) {
    EphyWebApplication *app = (EphyWebApplication *) node->data;

    if (g_strcmp0 (ephy_web_application_get_install_origin (app), origin) == 0) {
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

char *
ephy_web_application_get_profile_dir_from_name (const char *name)
{
  char *app_dir, *wm_class, *profile_dir;

  wm_class = ephy_web_application_get_wm_class_from_app_title (name);
  app_dir = g_strconcat (EPHY_WEB_APP_PREFIX, wm_class, NULL);
  profile_dir = g_build_filename (ephy_apps_dot_dir (), app_dir, NULL);
  g_free (wm_class);
  g_free (app_dir);

  return profile_dir;
}

EphyWebApplication *
ephy_web_application_from_name (const char *name)
{
  char *profile_dir;
  EphyWebApplication *result;

  profile_dir = ephy_web_application_get_profile_dir_from_name (name);

  result = ephy_web_application_new ();
  if (!ephy_web_application_load (result, profile_dir, NULL)) {
    g_object_unref (result);
    result = NULL;
  }
  g_free (profile_dir);

  return result;
}

/* This is necessary because of gnome-shell's guessing of a .desktop
   filename from WM_CLASS property. */
char *
ephy_web_application_get_wm_class_from_app_title (const char *title)
{
  char *normal_title;
  char *wm_class;
  char *checksum;

  normal_title = g_utf8_strdown (title, -1);
  g_strdelimit (normal_title, " ", '-');
  g_strdelimit (normal_title, G_DIR_SEPARATOR_S, '-');
  checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA1, title, -1);
  wm_class = g_strconcat (EPHY_WEB_APP_DESKTOP_FILE_PREFIX, normal_title, "-", checksum, NULL);

  g_free (checksum);
  g_free (normal_title);

  return wm_class;
}

void
ephy_web_application_set_full_uri (EphyWebApplication *app,
                                   const char *full_uri)
{
  EphyWebApplicationPrivate *priv;
  SoupURI *uri, *host_uri;

  priv = app->priv;

  uri = soup_uri_new (full_uri);

  g_free (priv->launch_path);
  priv->launch_path = soup_uri_to_string (uri, TRUE);

  host_uri = soup_uri_copy_host (uri);
  g_free (priv->origin);
  priv->origin = soup_uri_to_string (host_uri, FALSE);

  soup_uri_free (uri);
  soup_uri_free (host_uri);

  g_object_notify (G_OBJECT (app), "origin");
  g_object_notify (G_OBJECT (app), "launch-path");
}

char *
ephy_web_application_get_full_uri    (EphyWebApplication *app)
{
  EphyWebApplicationPrivate *priv;

  priv = app->priv;

  return priv->origin?g_strconcat (priv->origin, priv->launch_path, NULL):g_strdup(priv->launch_path);
}
