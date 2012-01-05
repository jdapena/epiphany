/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 sts=2 et: */
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

#include "config.h"
#include "ephy-web-app-utils.h"

#include "ephy-debug.h"
#include "ephy-file-helpers.h"
#include "ephy-web-application.h"

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libnotify/notify.h>
#include <libsoup/soup-gnome.h>
#include <webkit/webkit.h>
#include <stdlib.h>

#define ERROR_QUARK (g_quark_from_static_string ("ephy-web-application-error"))

static char *
get_origin (const char *url)
{
  char *origin;
  SoupURI *uri, *host_uri;

  uri = soup_uri_new (url);

  host_uri = soup_uri_copy_host (uri);
  origin = soup_uri_to_string (host_uri, FALSE);
  soup_uri_free (host_uri);
  soup_uri_free (uri);

  return origin;
}

#define EPHY_WEB_APP_TOOLBAR "<?xml version=\"1.0\"?>" \
                             "<toolbars version=\"1.1\">" \
                             "  <toolbar name=\"DefaultToolbar\" hidden=\"true\" editable=\"false\">" \
                             "    <toolitem name=\"NavigationBack\"/>" \
                             "    <toolitem name=\"NavigationForward\"/>" \
                             "    <toolitem name=\"ViewReload\"/>" \
                             "    <toolitem name=\"ViewCancel\"/>" \
                             "  </toolbar>" \
                             "</toolbars>"

#define EPHY_TOOLBARS_XML_FILE "epiphany-toolbars-3.xml"

static char *
create_desktop_and_metadata_files (const char *address,
                                   const char *profile_dir,
                                   const char *name,
                                   const char *description,
                                   GdkPixbuf *icon)
{
  GKeyFile *desktop_file = NULL;
  GKeyFile *metadata_file = NULL;
  char *exec_string = NULL;
  char *origin;
  char *launch_path;
  SoupURI *uri = NULL, *host_uri = NULL;
  char *data = NULL;
  char *apps_path, *file_path = NULL;
  char *wm_class;
  GError *error = NULL;

  g_return_val_if_fail (profile_dir, NULL);

  wm_class = ephy_web_application_get_wm_class_from_app_title (name);

  if (!wm_class)
    goto out;

  desktop_file = g_key_file_new ();
  metadata_file = g_key_file_new ();
  
  g_key_file_set_value (desktop_file, "Desktop Entry", "Name", name);
  g_key_file_set_value (metadata_file, "Application", "Name", name);

  g_key_file_set_value (metadata_file, "Application", "Description", description);

  exec_string = g_strdup_printf ("epiphany --application-mode --profile=\"%s\" %s",
                                 profile_dir,
                                 address);
  g_key_file_set_value (desktop_file, "Desktop Entry", "Exec", exec_string);
  g_free (exec_string);

  uri = soup_uri_new (address);
  launch_path = soup_uri_to_string (uri, TRUE);
  g_key_file_set_value (metadata_file, "Application", "LaunchPath", launch_path);
  g_free (launch_path);
  host_uri = soup_uri_copy_host (uri);
  origin = soup_uri_to_string (host_uri, FALSE);
  g_key_file_set_value (metadata_file, "Application", "Origin", origin);
  g_free (origin);
  soup_uri_free (uri);
  soup_uri_free (host_uri);

  g_key_file_set_value (desktop_file, "Desktop Entry", "StartupNotify", "true");
  g_key_file_set_value (desktop_file, "Desktop Entry", "Terminal", "false");
  g_key_file_set_value (desktop_file, "Desktop Entry", "Type", "Application");

  if (icon) {
    GOutputStream *stream;
    char *path;
    GFile *image;

    path = g_build_filename (profile_dir, EPHY_WEB_APPLICATION_APP_ICON, NULL);
    image = g_file_new_for_path (path);

    stream = (GOutputStream*)g_file_create (image, 0, NULL, NULL);
    gdk_pixbuf_save_to_stream (icon, stream, "png", NULL, NULL, NULL);
    g_key_file_set_value (desktop_file, "Desktop Entry", "Icon", path);
    g_key_file_set_value (metadata_file, "Application", "Icon", path);

    g_object_unref (stream);
    g_object_unref (image);
    g_free (path);
  }

  g_key_file_set_value (desktop_file, "Desktop Entry", "StartupWMClass", wm_class);

  data = g_key_file_to_data (metadata_file, NULL, NULL);
  file_path = g_build_filename (profile_dir, EPHY_WEB_APPLICATION_METADATA_FILE, NULL);
  g_key_file_free (metadata_file);

  g_file_set_contents (file_path, data, -1, NULL);
  g_free (file_path);
  g_free (data);

  data = g_key_file_to_data (desktop_file, NULL, NULL);
  file_path = g_build_filename (profile_dir, EPHY_WEB_APPLICATION_DESKTOP_FILE, NULL);
  g_key_file_free (desktop_file);

  if (!g_file_set_contents (file_path, data, -1, NULL)) {
    g_free (file_path);
    file_path = NULL;
  }

  /* Create a symlink in XDG_DATA_DIR/applications for the Shell to
   * pick up this application. */
  apps_path = g_build_filename (g_get_user_data_dir (), "applications", NULL);
  if (ephy_ensure_dir_exists (apps_path, &error)) {
    char *filename, *link_path;
    GFile *link;
    filename = g_strconcat (wm_class, ".desktop", NULL);
    link_path = g_build_filename (apps_path, filename, NULL);
    g_free (filename);
    link = g_file_new_for_path (link_path);
    g_free (link_path);
    g_file_make_symbolic_link (link, file_path, NULL, NULL);
    g_object_unref (link);
  } else {
    g_warning ("Error creating application symlink: %s", error->message);
    g_error_free (error);
  }
  g_free (wm_class);
  g_free (apps_path);

out:
  g_free (wm_class);
  g_free (data);
  g_key_file_free (desktop_file);
  g_key_file_free (metadata_file);

  return file_path;
}

static void
create_cookie_jar_for_domain (const char *address, const char *directory)
{
  SoupSession *session;
  GSList *cookies, *p;
  SoupCookieJar *current_jar, *new_jar;
  char *domain, *filename;
  SoupURI *uri;

  /* Create the new cookie jar */
  filename = g_build_filename (directory, "cookies.sqlite", NULL);
  new_jar = (SoupCookieJar*)soup_cookie_jar_sqlite_new (filename, FALSE);
  g_free (filename);

  /* The app domain for the current view */
  uri = soup_uri_new (address);
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
}

/**
 * ephy_web_application_create:
 * @address: the address of the new web application
 * @name: the name for the new web application
 * @description: the description of the new web application
 * @icon: the icon for the new web application
 * 
 * Creates a new Web Application for @address.
 * 
 * Returns: (transfer-full): the path to the desktop file representing the new application
 **/
char *
ephy_web_application_create (const char *address, const char *name, const char *description, GdkPixbuf *icon)
{
  char *profile_dir = NULL;
  char *toolbar_path = NULL;
  char *desktop_file_path = NULL;

  /* If there's already a WebApp profile for the contents of this
   * view, do nothing. */
  profile_dir = ephy_web_application_get_profile_dir_from_name (name);
  if (g_file_test (profile_dir, G_FILE_TEST_IS_DIR))
    goto out;

  /* Create the profile directory, populate it. */
  if (g_mkdir (profile_dir, 488) == -1) {
    LOG ("Failed to create directory %s", profile_dir);
    goto out;
  }

  /* Things we need in a WebApp's profile:
     - Toolbar layout
     - Our own cookies file, copying the relevant cookies for the
       app's domain.
  */
  toolbar_path = g_build_filename (profile_dir, EPHY_TOOLBARS_XML_FILE, NULL);
  if (!g_file_set_contents (toolbar_path, EPHY_WEB_APP_TOOLBAR, -1, NULL))
    goto out;

  create_cookie_jar_for_domain (address, profile_dir);

  /* Create the deskop file. */
  desktop_file_path = create_desktop_and_metadata_files (address, profile_dir, name, description, icon);

out:
  if (toolbar_path)
    g_free (toolbar_path);

  if (profile_dir)
    g_free (profile_dir);

  return desktop_file_path;
}

typedef struct {
  EphyWebApplication *app;
  GtkWidget *image;
  GtkWidget *entry;
  GtkWidget *spinner;
  GtkWidget *box;
  char *icon_href;
  EphyWebApplicationInstallCallback callback;
  gpointer userdata;
} EphyApplicationDialogData;

static void
ephy_application_dialog_data_free (EphyApplicationDialogData *data)
{
  g_object_unref (data->app);
  g_free (data->icon_href);
  g_slice_free (EphyApplicationDialogData, data);
}

static void
download_status_changed_cb (WebKitDownload *download,
                            GParamSpec *spec,
                            EphyApplicationDialogData *data)
{
  WebKitDownloadStatus status = webkit_download_get_status (download);
  const char *destination;

  switch (status)
  {
	case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    destination = g_filename_from_uri (webkit_download_get_destination_uri (download),
                                       NULL, NULL);
	  gtk_image_set_from_file (GTK_IMAGE (data->image), destination);
	  break;
  case WEBKIT_DOWNLOAD_STATUS_ERROR:
  case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
  default:
    break;
  }
}

static void
download_icon_and_set_image (EphyApplicationDialogData *data)
{
  WebKitNetworkRequest *request;
  WebKitDownload *download;
  char *destination, *destination_uri, *tmp_filename;

  request = webkit_network_request_new (data->icon_href);
  download = webkit_download_new (request);
  g_object_unref (request);

  tmp_filename = ephy_file_tmp_filename ("ephy-download-XXXXXX", NULL);
  destination = g_build_filename (ephy_file_tmp_dir (), tmp_filename, NULL);
  destination_uri = g_filename_to_uri (destination, NULL, NULL);
  webkit_download_set_destination_uri (download, destination_uri);
  g_free (destination);
  g_free (destination_uri);
  g_free (tmp_filename);

  g_signal_connect (download, "notify::status",
                    G_CALLBACK (download_status_changed_cb), data);

  webkit_download_start (download);	
}

static void
fill_default_application_image (EphyApplicationDialogData *data,
                                const char *icon_href,
                                GdkPixbuf *icon_pixbuf)
{
  if (icon_pixbuf) {
    gtk_image_set_from_pixbuf (GTK_IMAGE (data->image), icon_pixbuf);
  }
  if (icon_href) {
    data->icon_href = g_strdup (icon_href);
    download_icon_and_set_image (data);
  }
}

static void
notify_launch_cb (NotifyNotification *notification,
                  char *action,
                  gpointer user_data)
{
  char * desktop_file = user_data;
  /* A gross hack to be able to launch epiphany from within
   * Epiphany. Might be a good idea to figure out a better
   * solution... */
  g_unsetenv (EPHY_UUID_ENVVAR);
  ephy_file_launch_desktop_file (desktop_file, NULL, 0, NULL);
  g_free (desktop_file);
}

static gboolean
confirm_web_application_overwrite (GtkWindow *parent,
                                   const char *name)
{
  GtkResponseType response;
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (parent, 0,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   _("A web application named '%s' already exists. "
                                     "Do you want to replace it?"),
                                   name);
  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                          _("Cancel"),
                          GTK_RESPONSE_CANCEL,
                          _("Replace"),
                          GTK_RESPONSE_OK,
                          NULL);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("An application with the same name "
                                              "already exists. Replacing it will "
                                              "overwrite it."));
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return response == GTK_RESPONSE_OK;
}

static void
dialog_application_install_response_cb (GtkDialog *dialog,
                                        gint response,
                                        EphyApplicationDialogData *data)
{
  gboolean created = FALSE;
  if (response == GTK_RESPONSE_OK) {
    char *message;
    NotifyNotification *notification;
    EphyWebApplication *existing_app;

    existing_app = 
      ephy_web_application_from_name (gtk_entry_get_text (GTK_ENTRY (data->entry)));

    if (existing_app != NULL) {
      if (confirm_web_application_overwrite (GTK_WINDOW (dialog),
                                             gtk_entry_get_text (GTK_ENTRY (data->entry)))) {
        ephy_web_application_delete (existing_app, NULL);
        g_object_unref (existing_app);
      } else {
        g_object_unref (existing_app);
        if (data->callback) {
          data->callback (GTK_RESPONSE_CANCEL, NULL, data->userdata);
          ephy_application_dialog_data_free (data);
          gtk_widget_destroy (GTK_WIDGET (dialog));
        }
        return;
      }
    }

    ephy_web_application_set_name (data->app,
                                   gtk_entry_get_text (GTK_ENTRY (data->entry)));
    /* Create Web Application, including a new profile and .desktop file. */
    created = ephy_web_application_install (data->app,
                                            gtk_image_get_pixbuf (GTK_IMAGE (data->image)),
                                            NULL);

    if (created) {
      message = g_strdup_printf (_("The application '%s' is ready to be used"),
                                 ephy_web_application_get_name (data->app));
    } else {
      message = g_strdup_printf (_("The application '%s' could not be created"),
                                 ephy_web_application_get_name (data->app));
      response = GTK_RESPONSE_CANCEL;
    }

    notification = notify_notification_new (message, NULL, NULL);
    g_free (message);

    if (created) {
      char *desktop_file;

      desktop_file =
        ephy_web_application_get_settings_file_name (data->app,
                                                     EPHY_WEB_APPLICATION_DESKTOP_FILE);
      notify_notification_add_action (notification, "launch", _("Launch"),
                                      (NotifyActionCallback)notify_launch_cb,
                                      desktop_file,
                                      NULL);
      notify_notification_set_icon_from_pixbuf (notification,
                                                gtk_image_get_pixbuf (GTK_IMAGE (data->image)));
			g_free (desktop_file);
    }

    notify_notification_set_timeout (notification, NOTIFY_EXPIRES_DEFAULT);
    notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
    notify_notification_set_hint (notification, "transient", g_variant_new_boolean (TRUE));
    notify_notification_show (notification, NULL);
  }

  if (data->callback) {
    if (created && !data->callback (response, data->app, data->userdata))
      ephy_web_application_delete (data->app, NULL);
  }

  ephy_application_dialog_data_free (data);
  gtk_widget_destroy (GTK_WIDGET (dialog));
}


void
ephy_web_application_show_install_dialog (GtkWindow *window,
                                          const char *dialog_title,
                                          const char *install_action,
                                          EphyWebApplication *app,
                                          const char *icon_href,
                                          GdkPixbuf *icon_pixbuf,
                                          EphyWebApplicationInstallCallback callback,
                                          gpointer userdata)
{
  GtkWidget *dialog, *box, *image, *entry, *label, *content_area;
  EphyApplicationDialogData *data;

  /* Show dialog with icon, title. */
  dialog = gtk_dialog_new_with_buttons (dialog_title,
                                        GTK_WINDOW (window),
                                        0,
                                        GTK_STOCK_CANCEL,
                                        GTK_RESPONSE_CANCEL,
                                        install_action,
                                        GTK_RESPONSE_OK,
                                        NULL);

  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  gtk_box_set_spacing (GTK_BOX (content_area), 14); /* 14 + 2 * 5 = 24 */

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_container_add (GTK_CONTAINER (content_area), box);

  image = gtk_image_new ();
  gtk_widget_set_size_request (image, 128, 128);
  gtk_container_add (GTK_CONTAINER (box), image);

  entry = gtk_entry_new ();
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_box_pack_end (GTK_BOX (box), entry, FALSE, FALSE, 0);

  label = gtk_label_new (ephy_web_application_get_description (app));
  gtk_box_pack_end (GTK_BOX (box), label, FALSE, FALSE, 0);
  
  data = g_slice_new0 (EphyApplicationDialogData);
  data->app = g_object_ref (app);
  data->image = image;
  data->entry = entry;
  data->callback = callback;
  data->userdata = userdata;

  fill_default_application_image (data, icon_href, icon_pixbuf);
  gtk_entry_set_text (GTK_ENTRY (entry), ephy_web_application_get_name (app));

  gtk_widget_show_all (dialog);
  if (ephy_web_application_get_description (app) == NULL)
    gtk_widget_hide (label);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  g_signal_connect (dialog, "response",
                    G_CALLBACK (dialog_application_install_response_cb),
                    data);
  gtk_widget_show_all (dialog);
}

typedef struct {
  char *manifest_path;
  char *receipt;
} MozAppInstallData;
    
static gboolean
mozapp_install_cb (gint response,
                   EphyWebApplication *app,
                   gpointer userdata)
{
  gboolean result = TRUE;
  GError *err = NULL;
  MozAppInstallData *mozapp_install_data = (MozAppInstallData *) userdata;

  if (response == GTK_RESPONSE_OK) {
    {
      GFile *origin_manifest, *destination_manifest;
      char *manifest_install_path;

      origin_manifest = g_file_new_for_path (mozapp_install_data->manifest_path);
      manifest_install_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_MOZILLA_MANIFEST);
      destination_manifest = g_file_new_for_path (manifest_install_path);

      result = g_file_copy (origin_manifest, destination_manifest, 
                            G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
                            NULL, NULL, NULL, 
                            &err);

      g_object_unref (origin_manifest);
      g_object_unref (destination_manifest);
      g_free (manifest_install_path);
    }

    if (result && mozapp_install_data->receipt) {
      char *receipt_path;

      receipt_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_MOZILLA_RECEIPT);

      result = g_file_set_contents (receipt_path, mozapp_install_data->receipt, -1, &err);

      g_free (receipt_path);
    }

  }

  if (!result) {
    if (err) g_error_free (err);
  }

  g_free (mozapp_install_data->manifest_path);
  g_free (mozapp_install_data->receipt);
  g_slice_free (MozAppInstallData, mozapp_install_data);

  return result;
}

void
ephy_web_application_install_manifest (GtkWindow *window,
                                       const char *origin,
                                       const char *manifest_path,
                                       const char *receipt,
                                       const char *install_origin)
{
  JsonParser *parser;
  GError *error = NULL;
  char *manifest_file_path;
  MozAppInstallData *mozapp_install_data;

  manifest_file_path = g_filename_from_uri (manifest_path, NULL, &error);
  if (!manifest_file_path) {
    return;
  }

  parser = json_parser_new ();
  if (json_parser_load_from_file (parser,
                                  manifest_file_path,
                                  &error)) {
    JsonNode *root_node;
    JsonNode *node;
    EphyWebApplication *app;
    char *icon_href = NULL;

    app = ephy_web_application_new ();
    ephy_web_application_set_install_origin (app, install_origin);
    ephy_web_application_set_origin (app, origin);

    // TODO : free nodes
    root_node = json_parser_get_root (parser);
    node = json_path_query ("$.name", root_node, NULL);
    if (node) {
      if (JSON_NODE_HOLDS_ARRAY (node)) {
        JsonArray *array = json_node_get_array (node);
        if (json_array_get_length (array) > 0) {
          ephy_web_application_set_name (app, json_array_get_string_element (array, 0));
        }
      }
      json_node_free (node);
    }

    node = json_path_query ("$.description", root_node, NULL);
    if (node) {
      if (JSON_NODE_HOLDS_ARRAY (node)) {
        JsonArray *array = json_node_get_array (node);
        if (json_array_get_length (array) > 0) {
          ephy_web_application_set_description (app, json_array_get_string_element (array, 0));
        }
      }
      json_node_free (node);
    }

    node = json_path_query ("$.developer.name", root_node, NULL);
    if (node) {
      if (JSON_NODE_HOLDS_ARRAY (node)) {
        JsonArray *array = json_node_get_array (node);
        if (json_array_get_length (array) > 0) {
          ephy_web_application_set_author (app, json_array_get_string_element (array, 0));
        }
      }
      json_node_free (node);
    }

    node = json_path_query ("$.developer.url", root_node, NULL);
    if (node) {
      if (JSON_NODE_HOLDS_ARRAY (node)) {
        JsonArray *array = json_node_get_array (node);
        if (json_array_get_length (array) > 0) {
          ephy_web_application_set_author_url (app, json_array_get_string_element (array, 0));
        }
      }
      json_node_free (node);
    }

    node = json_path_query ("$.launch_path", root_node, NULL);
    if (node) {
      if (JSON_NODE_HOLDS_ARRAY (node)) {
        JsonArray *array = json_node_get_array (node);
        if (json_array_get_length (array) > 0) {
          ephy_web_application_set_launch_path (app, json_array_get_string_element (array, 0));
        }
      }
      json_node_free (node);
    }

    node = json_path_query ("$.icons", root_node, NULL);
    if (node) {
      if (JSON_NODE_HOLDS_ARRAY (node)) {
        JsonArray *array;

        array = json_node_get_array (node);
        if (json_array_get_length (array) > 0) {
          JsonObject *object;

          object = json_array_get_object_element (array, 0);
          if (object) {
            GList *members, *node, *best_node;
            unsigned long int best_size;

            best_size = 0;
            best_node = NULL;
            members = json_object_get_members (object);
            for (node = members; node != NULL; node = g_list_next (node)) {
              unsigned long int node_size;
              node_size = strtoul(node->data, NULL, 10);
              if (node_size != ULONG_MAX && node_size >= best_size) {
                best_size = node_size;
                best_node = node;
              }
            }
            if (best_node) {
              icon_href = g_strconcat (origin, json_object_get_string_member (object, (char *) best_node->data), NULL);
            }
            g_list_free (members);
          }
        }
      }
      json_node_free (node);
    }

    mozapp_install_data = g_slice_new0 (MozAppInstallData);
    mozapp_install_data->manifest_path = g_strdup (manifest_file_path);
    mozapp_install_data->receipt = g_strdup (receipt);

    ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_TEMPORARY);

    ephy_web_application_show_install_dialog (window,
                                              _("Install web application"), _("Install"),
                                              app, icon_href, NULL,
                                              mozapp_install_cb, (gpointer) mozapp_install_data);

    g_object_unref (app);
    g_free (icon_href);
    g_free (manifest_file_path);
    

  } else {
    g_debug ("%s: %s", __FUNCTION__, error->message);
  }

  g_object_unref (parser);
}

static void mozapps_init_cb(JSContextRef ctx, JSObjectRef object)
{
/* ... */
}

static void mozapps_finalize_cb(JSObjectRef object)
{
/* ... */
}

static JSObjectRef
mozapps_app_object_from_application (JSContextRef context, EphyWebApplication *app, GError **error)
{
  GFile *manifest_file = NULL;
  gboolean is_ok = TRUE;
  char *manifest_contents = NULL;
  JSObjectRef result = NULL;

  if (is_ok) {
    char *manifest_path;
    manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_MOZILLA_MANIFEST);
    manifest_file = g_file_new_for_path (manifest_path);
    g_free (manifest_path);
    
    is_ok = g_file_query_exists (manifest_file, NULL);
    if (!is_ok) {
      g_set_error (error, ERROR_QUARK, 0, "Manifest doesn't exist for this application");
    }
  }
  
  if (is_ok) is_ok = g_file_load_contents (manifest_file, NULL, &manifest_contents, NULL, NULL, error);

  if (is_ok) {
    GFileInfo *metadata_info;
    guint64 created;

    result = JSObjectMake (context, NULL, NULL);
    
    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("manifest"),
                         JSValueMakeFromJSONString (context, JSStringCreateWithUTF8CString (manifest_contents)), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         NULL);

    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("origin"),
                         JSValueMakeString (context, JSStringCreateWithUTF8CString (ephy_web_application_get_origin(app))), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         NULL);
    
    metadata_info = g_file_query_info (manifest_file, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, error);
    is_ok = (metadata_info != NULL);
    created = g_file_info_get_attribute_uint64 (metadata_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    
    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("install_time"),
                         JSValueMakeNumber (context, created), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         NULL);
    
  }

  if (is_ok) {
    char *receipt_path;
    GFile *receipt_file;
    
    receipt_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_MOZILLA_RECEIPT);
    receipt_file = g_file_new_for_path (receipt_path);
    g_free (receipt_path);

    if (g_file_query_exists (receipt_file, NULL)) {
      char *receipt_contents;
      if (g_file_load_contents (receipt_file, NULL, &receipt_contents, NULL, NULL, NULL)) {
        JSObjectSetProperty (context, result, 
                             JSStringCreateWithUTF8CString ("receipt"),
                             JSValueMakeFromJSONString (context, JSStringCreateWithUTF8CString (receipt_contents)), 
                             kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                             NULL);
      }
    }
    g_object_unref (receipt_file);
  }

  if (is_ok && ephy_web_application_get_install_origin (app) != NULL) {
    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("installOrigin"),
                         JSValueMakeString (context, JSStringCreateWithUTF8CString (ephy_web_application_get_install_origin (app))), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         NULL);
  }
  
  g_free (manifest_contents);
  if (manifest_file) g_object_unref (manifest_file);
  
  return result;
}

static JSObjectRef mozapps_app_object_from_origin (JSContextRef context, const char *origin, GError **error)
{
  EphyWebApplication *app;
  JSObjectRef result = NULL;

  app = ephy_web_application_new ();
  if (!ephy_web_application_load (app, ephy_dot_dir (), NULL)) {
    GList *origin_applications, *node;
    g_object_unref (app);
    app = NULL;
    origin_applications = ephy_web_application_get_applications_from_origin (origin);
    for (node = origin_applications; node != NULL; node = g_list_next (node)) {
      if (ephy_web_application_is_mozilla_webapp (EPHY_WEB_APPLICATION (node->data))) {
        app = EPHY_WEB_APPLICATION (node->data);
        g_object_ref (app);
        break;
      }
    }
    ephy_web_application_free_applications_list (origin_applications);
  }

  if (app == NULL) {
    g_set_error (error, ERROR_QUARK, 0, "App does not exist");
  } else {
    result = mozapps_app_object_from_application (context, app, error);
    g_object_unref (app);
  }
  
  return result;
}

static JSValueRef
mozapps_am_installed (JSContextRef context,
                      JSObjectRef function,
                      JSObjectRef thisObject,
                      size_t argumentCount,
                      const JSValueRef arguments[],
                      JSValueRef *exception)
{
  // TODO: create exception
  if (argumentCount != 1) {
    return NULL;
  }
  if (!JSValueIsObject (context, arguments[0])) {
    return NULL;
  } else {
    JSObjectRef object_ref;
    JSStringRef script_ref;
    JSValueRef location_value;
    JSStringRef location_str;
    char *location;
    int location_len;
    SoupURI *uri, *host_uri;
    char *origin;
    JSValueRef callback_parameter = NULL;
    JSValueRef callback_arguments[1];

    object_ref = JSValueToObject (context, arguments[0], exception);
    if (object_ref == NULL || !JSObjectIsFunction (context, object_ref)) {
      return NULL;
    }

    script_ref = JSStringCreateWithUTF8CString ("window.location.href");

    location_value = JSEvaluateScript (context, script_ref, NULL, NULL, 0, exception);
    if (!JSValueIsString (context, location_value)) {
      goto amInstalledFinish;
    }
    location_str = JSValueToStringCopy (context, location_value, NULL);
    location_len = JSStringGetMaximumUTF8CStringSize (location_str);
    location = g_malloc0(location_len);
    JSStringGetUTF8CString (location_str, location, location_len);

    uri = soup_uri_new (location);
    host_uri = soup_uri_copy_host (uri);
    origin = soup_uri_to_string (host_uri, FALSE);

    soup_uri_free (host_uri);
    soup_uri_free (uri);
    g_free (location);

    if (origin == NULL) {
      goto amInstalledFinish;
    } else {
      callback_parameter = mozapps_app_object_from_origin (context, origin, NULL);
    }
    
    g_warning("%s: %s", __FUNCTION__, origin);
    g_free (origin);

  amInstalledFinish:

    if (callback_parameter == NULL) {
      callback_parameter = JSValueMakeNull (context);
    }
    callback_arguments[0] = callback_parameter;
    
    JSObjectCallAsFunction (context, object_ref, NULL, 1, callback_arguments, NULL);
  }

  return NULL;
}

static JSObjectRef mozapps_app_objects_from_install_origin (JSContextRef context, const char *origin, GError **error)
{
  JSObjectRef result = NULL;
  GList *origin_applications, *node;
  GList *js_objects_list = NULL;
  int array_count;
  JSValueRef *array_arguments = NULL;

  origin_applications = ephy_web_application_get_applications_from_install_origin (origin);
  for (node = origin_applications; node != NULL; node = g_list_next (node)) {
    EphyWebApplication *app = (EphyWebApplication *) node->data;
    JSObjectRef app_object;

    app_object = mozapps_app_object_from_application (context, app, error);
    if (app_object != NULL) {
      js_objects_list = g_list_append (js_objects_list, app_object);
    }
  }
  ephy_web_application_free_applications_list (origin_applications);

  array_count = g_list_length (js_objects_list);
  if (array_count > 0) {
    int i = 0;
    array_arguments = g_malloc0 (sizeof(JSValueRef *) * array_count);
    for (node = js_objects_list; node != NULL; node = g_list_next (node)) {
      array_arguments[i] = (JSValueRef) node->data;
      i++;
    }
  }
  result = JSObjectMakeArray (context, array_count, array_arguments, NULL);

  return result;
}

static JSValueRef
mozapps_get_installed_by (JSContextRef context,
                          JSObjectRef function,
                          JSObjectRef thisObject,
                          size_t argumentCount,
                          const JSValueRef arguments[],
                          JSValueRef *exception)
{
  // TODO: create exception
  if (argumentCount != 1) {
    return NULL;
  }
  if (!JSValueIsObject (context, arguments[0])) {
    return NULL;
  } else {
    JSObjectRef object_ref;
    JSStringRef script_ref;
    JSValueRef location_value;
    JSStringRef location_str;
    char *location;
    int location_len;
    SoupURI *uri, *host_uri;
    char *origin;
    JSValueRef callback_parameter = NULL;
    JSValueRef callback_arguments[1];

    object_ref = JSValueToObject (context, arguments[0], exception);
    if (object_ref == NULL || !JSObjectIsFunction (context, object_ref)) {
      return NULL;
    }

    script_ref = JSStringCreateWithUTF8CString ("window.location.href");

    location_value = JSEvaluateScript (context, script_ref, NULL, NULL, 0, exception);
    if (!JSValueIsString (context, location_value)) {
      goto getInstalledByFinish;
    }
    location_str = JSValueToStringCopy (context, location_value, NULL);
    location_len = JSStringGetMaximumUTF8CStringSize (location_str);
    location = g_malloc0(location_len);
    JSStringGetUTF8CString (location_str, location, location_len);

    uri = soup_uri_new (location);
    host_uri = soup_uri_copy_host (uri);
    origin = soup_uri_to_string (host_uri, FALSE);

    soup_uri_free (host_uri);
    soup_uri_free (uri);
    g_free (location);

    if (origin == NULL) {
      goto getInstalledByFinish;
    } else {
      callback_parameter = mozapps_app_objects_from_install_origin (context, origin, NULL);
    }
    
    g_warning("%s: %s", __FUNCTION__, origin);
    g_free (origin);

  getInstalledByFinish:

    if (callback_parameter == NULL) {
      callback_parameter = JSValueMakeNull (context);
    }
    callback_arguments[0] = callback_parameter;
    
    JSObjectCallAsFunction (context, object_ref, NULL, 1, callback_arguments, NULL);
  }

  return NULL;
}

typedef struct {
  char *install_origin;
  char *url;
  char *local_path;
  char *receipt;
} EphyMozAppInstallManifestData;

static void
mozapp_install_manifest_download_status_changed_cb (WebKitDownload *download,
                                                    GParamSpec *spec,
                                                    EphyMozAppInstallManifestData *manifest_data)
{
	WebKitDownloadStatus status = webkit_download_get_status (download);

	switch (status) {
	case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    {
      char *origin;

      origin = get_origin (manifest_data->url);
      ephy_web_application_install_manifest (NULL,
                                             origin,
                                             manifest_data->local_path,
                                             manifest_data->receipt,
                                             manifest_data->install_origin);
      g_free (origin);
    }

	case WEBKIT_DOWNLOAD_STATUS_ERROR:
	case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
    g_free (manifest_data->url);
    g_free (manifest_data->local_path);
    g_free (manifest_data->receipt);
    g_free (manifest_data->install_origin);
    g_slice_free (EphyMozAppInstallManifestData, manifest_data);

		g_object_unref (download);

		break;
	default:
		break;
	}
}


static JSValueRef
mozapps_install (JSContextRef context,
                 JSObjectRef function,
                 JSObjectRef thisObject,
                 size_t argumentCount,
                 const JSValueRef arguments[],
                 JSValueRef *exception)
{
  char *url;
  char *receipt = NULL;
  JSStringRef url_str;
  int max_url_str_size;
  WebKitNetworkRequest *request;
  WebKitDownload *download;
  EphyMozAppInstallManifestData *install_manifest_data;
	char *destination, *destination_uri, *tmp_filename;

  if (argumentCount < 1 || argumentCount > 4) {
    return NULL;
  }
  if (!JSValueIsString (context, arguments[0])) {
    return NULL;
  }
  url_str = JSValueToStringCopy (context, arguments[0], NULL);
  if (url_str == NULL) {
    return NULL;
  }

  max_url_str_size = JSStringGetMaximumUTF8CStringSize (url_str);
  url = g_malloc0 (max_url_str_size);
  JSStringGetUTF8CString (url_str, url, max_url_str_size);

  if (argumentCount > 1) {
    JSStringRef json_str_receipt;

    json_str_receipt = JSValueCreateJSONString (context, arguments[1], 2, NULL);

    if (json_str_receipt) {
      int max_receipt_str_size = JSStringGetMaximumUTF8CStringSize (json_str_receipt);
      receipt = g_malloc0 (max_receipt_str_size);
      JSStringGetUTF8CString (json_str_receipt, receipt, max_receipt_str_size);
    }
  }

  install_manifest_data = g_slice_new0 (EphyMozAppInstallManifestData);
  install_manifest_data->url = g_strdup (url);
  install_manifest_data->receipt = g_strdup (receipt);

  {
    JSStringRef script_ref;
    JSValueRef location_value;

    script_ref = JSStringCreateWithUTF8CString ("window.location.href");

    location_value = JSEvaluateScript (context, script_ref, NULL, NULL, 0, exception);
    if (JSValueIsString (context, location_value)) {
      JSStringRef location_str;
      gint location_len;
      char *location;
      location_str = JSValueToStringCopy (context, location_value, NULL);
      location_len = JSStringGetMaximumUTF8CStringSize (location_str);
      location = g_malloc0(location_len);
      JSStringGetUTF8CString (location_str, location, location_len);

      install_manifest_data->install_origin = get_origin (location);
      g_free (location);
    } else {
      install_manifest_data->install_origin = NULL;
    }
  }

  request = webkit_network_request_new (url);
  download = webkit_download_new (request);
  g_object_unref (request);

	tmp_filename = ephy_file_tmp_filename ("ephy-download-XXXXXX", NULL);
	destination = g_build_filename (ephy_file_tmp_dir (), tmp_filename, NULL);
	destination_uri = g_filename_to_uri (destination, NULL, NULL);
  webkit_download_set_destination_uri (download, destination_uri);
  install_manifest_data->local_path = g_strdup (destination_uri);
	g_free (destination);
	g_free (destination_uri);
	g_free (tmp_filename);

  g_signal_connect (G_OBJECT (download), "notify::status",
                    G_CALLBACK (mozapp_install_manifest_download_status_changed_cb), install_manifest_data);

  webkit_download_start (download);

  return NULL;
}

static const JSStaticFunction mozapps_class_staticfuncs[] =
{
{ "amInstalled", mozapps_am_installed, kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontEnum },
{ "install", mozapps_install, kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontEnum },
{ "getInstalledBy", mozapps_get_installed_by, kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontEnum },
{ NULL, NULL, 0 }
};

static const JSClassDefinition mozapps_class_def =
{
0,
kJSClassAttributeNone,
"EphyMozAppsClass",
NULL,

NULL,
mozapps_class_staticfuncs,

mozapps_init_cb,
mozapps_finalize_cb,

NULL,
NULL,
NULL,
NULL,
NULL,
NULL,
NULL,
NULL,
NULL
};


void
ephy_web_application_setup_mozilla_api (JSGlobalContextRef context)
{
  JSClassRef mozAppsClassDef = JSClassCreate (&mozapps_class_def);
  JSObjectRef mozAppsClassObj = JSObjectMake (context, mozAppsClassDef, context);
  JSObjectRef globalObj = JSContextGetGlobalObject(context);
  JSStringRef navigatorStr = JSStringCreateWithUTF8CString("navigator");
  JSValueRef navigatorRef = JSObjectGetProperty (context, globalObj,
                                                 navigatorStr, NULL);
  JSObjectRef navigatorObj = JSValueToObject (context, navigatorRef, NULL);
  JSStringRef mozAppsStr = JSStringCreateWithUTF8CString("mozApps");
  JSObjectSetProperty(context, navigatorObj, mozAppsStr, mozAppsClassObj,
                      kJSPropertyAttributeNone, NULL);
}
