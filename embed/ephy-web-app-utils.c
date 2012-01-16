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

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libnotify/notify.h>
#include <libsoup/soup-gnome.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/tree.h>
#include <stdlib.h>
#include <webkit/webkit.h>

#define ERROR_QUARK (g_quark_from_static_string ("ephy-web-application-error"))

#define DEFAULT_CHROME_WEBSTORE_CRX_UPDATE_PATH "http://clients2.google.com/service/update2/crx"

static char *
js_string_to_utf8 (JSStringRef js_string)
{
  int length;
  char *result;

  length = JSStringGetMaximumUTF8CStringSize (js_string);
  if (length == 0)
    return NULL;
  result = g_malloc0 (length);
  JSStringGetUTF8CString (js_string, result, length);
  return result;
}

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

  gtk_widget_destroy (GTK_WIDGET (dialog));
  if (data->callback) {
    if (created) {
      if (!data->callback (response, data->app, data->userdata))
        ephy_web_application_delete (data->app, NULL);
    } else {
      data->callback (GTK_RESPONSE_CANCEL, data->app, data->userdata);
    }
  }

  ephy_application_dialog_data_free (data);
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
  GtkWidget *dialog, *hbox, *vbox, *image, *entry, *description_label, *content_area;
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

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_container_add (GTK_CONTAINER (content_area), hbox);

  image = gtk_image_new ();
  gtk_widget_set_size_request (image, 128, 128);
  gtk_container_add (GTK_CONTAINER (hbox), image);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add (GTK_CONTAINER (hbox), vbox);

  entry = gtk_entry_new ();
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_container_add (GTK_CONTAINER (vbox), entry);

  description_label = gtk_label_new (ephy_web_application_get_description (app));
  gtk_label_set_line_wrap (GTK_LABEL (description_label), TRUE);
  gtk_container_add (GTK_CONTAINER (vbox), description_label);
  
  if (ephy_web_application_get_author_url (app) != NULL) {
    GtkWidget *author_button;
    GtkWidget *author_button_label;
    if (ephy_web_application_get_author (app) != NULL) {
      author_button = gtk_link_button_new_with_label (ephy_web_application_get_author_url (app),
                                                      ephy_web_application_get_author (app));
    } else {
      author_button = gtk_link_button_new (ephy_web_application_get_author_url (app));
    }
    author_button_label = gtk_bin_get_child (GTK_BIN (author_button));
    if (GTK_IS_LABEL (author_button_label)) {
      gtk_label_set_line_wrap (GTK_LABEL (author_button_label), TRUE);
    }
    gtk_container_add (GTK_CONTAINER (vbox), author_button);
  } else if (ephy_web_application_get_author (app) != NULL) {
    GtkWidget *author_label;
    author_label = gtk_label_new (ephy_web_application_get_author (app));
    gtk_label_set_line_wrap (GTK_LABEL (author_label), TRUE);
    gtk_container_add (GTK_CONTAINER (vbox), author_label);
  }
  
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
    gtk_widget_hide (description_label);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  g_signal_connect (dialog, "response",
                    G_CALLBACK (dialog_application_install_response_cb),
                    data);
  gtk_widget_show_all (dialog);
}

typedef struct {
  char *manifest_path;
  char *receipt;
  GError *error;
  EphyWebApplicationInstallManifestCallback callback;
  gpointer userdata;
} MozAppInstallData;

static void
finish_mozapp_install_data (MozAppInstallData *install_data)
{
  if (install_data->callback) {
    install_data->callback (install_data->error, install_data->userdata);
  }
  g_free (install_data->manifest_path);
  g_free (install_data->receipt);
  if  (install_data->error) {
    g_error_free (install_data->error);
  }
  g_slice_free (MozAppInstallData, install_data);
}
    
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

  } else {
    g_set_error (&(mozapp_install_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_CANCELLED, "User cancelled installation");
  }

  finish_mozapp_install_data (mozapp_install_data);

  return result;
}

void
ephy_web_application_install_manifest (GtkWindow *window,
                                       const char *origin,
                                       const char *manifest_path,
                                       const char *receipt,
                                       const char *install_origin,
                                       EphyWebApplicationInstallManifestCallback callback,
                                       gpointer userdata)
{
  JsonParser *parser;
  GError *error = NULL;
  char *manifest_file_path;
  MozAppInstallData *mozapp_install_data;

  manifest_file_path = g_filename_from_uri (manifest_path, NULL, &error);
  if (!manifest_file_path) {
    if (callback) {
      g_set_error (&error, ERROR_QUARK, EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, "Couldn't open manifest");
      callback (error, userdata);
      g_error_free (error);
    }
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
    mozapp_install_data->error = NULL;
    mozapp_install_data->callback = callback;
    mozapp_install_data->userdata = userdata;

    ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_TEMPORARY);

    ephy_web_application_show_install_dialog (window,
                                              _("Install web application"), _("Install"),
                                              app, icon_href, NULL,
                                              mozapp_install_cb, (gpointer) mozapp_install_data);

    g_object_unref (app);
    g_free (icon_href);
    g_free (manifest_file_path);
    

  } else {
    error->domain = ERROR_QUARK;
    error->code = EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR;
    if (callback) {
      callback (error, userdata);
      g_error_free (error);
    }
  }

  g_object_unref (parser);
}

static JSValueRef
mozapps_app_object_from_application (JSContextRef context, EphyWebApplication *app, JSValueRef *exception)
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
    /* This is not a mozapp application, but shouldn't throw an error. It can be an app
     * installed using other API's */
  }
  
  if (is_ok) is_ok = g_file_load_contents (manifest_file, NULL, &manifest_contents, NULL, NULL, NULL);

  if (is_ok) {
    GFileInfo *metadata_info;
    guint64 created;

    result = JSObjectMake (context, NULL, NULL);
    
    
    metadata_info = g_file_query_info (manifest_file, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, NULL);
    is_ok = (metadata_info != NULL);
    created = g_file_info_get_attribute_uint64 (metadata_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    
    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("manifest"),
                         JSValueMakeFromJSONString (context, JSStringCreateWithUTF8CString (manifest_contents)), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         exception);

    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("origin"),
                         JSValueMakeString (context, JSStringCreateWithUTF8CString (ephy_web_application_get_origin(app))), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         exception);

    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("install_time"),
                         JSValueMakeNumber (context, created), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         exception);
    is_ok = (*exception == NULL);
    
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
                             exception);
        is_ok = (*exception == NULL);
      }
    }
    g_object_unref (receipt_file);
  }

  if (is_ok && ephy_web_application_get_install_origin (app) != NULL) {
    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("installOrigin"),
                         JSValueMakeString (context, JSStringCreateWithUTF8CString (ephy_web_application_get_install_origin (app))), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         exception);
    is_ok = (*exception == NULL);
  }
  
  g_free (manifest_contents);
  if (manifest_file) g_object_unref (manifest_file);

  if (*exception || !is_ok) {
    return JSValueMakeNull (context);
  } else {
    return result;
  }
}

static JSValueRef mozapps_app_object_from_origin (JSContextRef context, const char *origin, JSValueRef *exception)
{
  EphyWebApplication *app;
  JSValueRef result;

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
    result = JSValueMakeNull (context);
  } else {
    result = mozapps_app_object_from_application (context, app, exception);
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
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  }
  if (!JSValueIsObject (context, arguments[0])) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
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
      *exception = JSValueMakeNumber (context, 1);
      return JSValueMakeNull (context);
    }

    script_ref = JSStringCreateWithUTF8CString ("window.location.href");
    location_value = JSEvaluateScript (context, script_ref, NULL, NULL, 0, exception);
    JSStringRelease (script_ref);

    if (!JSValueIsString (context, location_value) || *exception) {
      *exception = JSValueMakeNumber (context, 1);
      goto amInstalledFinish;
    }
    location_str = JSValueToStringCopy (context, location_value, exception);
    if (*exception != NULL) {
      goto amInstalledFinish;
    }
    location_len = JSStringGetMaximumUTF8CStringSize (location_str);
    location = g_malloc0(location_len);
    JSStringGetUTF8CString (location_str, location, location_len);
    JSStringRelease (location_str);

    uri = soup_uri_new (location);
    host_uri = soup_uri_copy_host (uri);
    origin = soup_uri_to_string (host_uri, FALSE);

    soup_uri_free (host_uri);
    soup_uri_free (uri);
    g_free (location);

    if (origin == NULL) {
      *exception = JSValueMakeNumber (context, 1);
      goto amInstalledFinish;
    } else {
      callback_parameter = mozapps_app_object_from_origin (context, origin, exception);
    }
    
    g_free (origin);

  amInstalledFinish:

    if (*exception == NULL) {
      callback_arguments[0] = callback_parameter;

      return JSObjectCallAsFunction (context, object_ref, thisObject, 1, callback_arguments, exception);
    } else {
      return JSValueMakeNull (context);
    }
  }
}

static JSValueRef mozapps_app_objects_from_install_origin (JSContextRef context, const char *origin, JSValueRef *exception)
{
  GList *origin_applications, *node;
  GList *js_objects_list = NULL;
  int array_count;
  JSValueRef *array_arguments = NULL;

  origin_applications = ephy_web_application_get_applications_from_install_origin (origin);
  for (node = origin_applications; node != NULL; node = g_list_next (node)) {
    EphyWebApplication *app = (EphyWebApplication *) node->data;
    JSValueRef app_object;

    app_object = mozapps_app_object_from_application (context, app, exception);
    if (app_object != NULL && ! JSValueIsNull (context, app_object)) {
      js_objects_list = g_list_append (js_objects_list, JSValueToObject (context, app_object, exception));
    }
    if (*exception) {
      break;
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
  if (*exception || array_count == 0) {
    return JSValueMakeNull (context);
  } else {
    return JSObjectMakeArray (context, array_count, array_arguments, exception);
  }
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
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull(context);
  }
  if (!JSValueIsObject (context, arguments[0])) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull(context);
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
      *exception = JSValueMakeNumber (context, 1);
      return JSValueMakeNull(context);
    }

    script_ref = JSStringCreateWithUTF8CString ("window.location.href");
    location_value = JSEvaluateScript (context, script_ref, NULL, NULL, 0, exception);
    JSStringRelease (script_ref);

    if (!JSValueIsString (context, location_value)) {
      *exception = JSValueMakeNumber (context, 1);
      goto getInstalledByFinish;
    }

    location_str = JSValueToStringCopy (context, location_value, NULL);
    location_len = JSStringGetMaximumUTF8CStringSize (location_str);
    location = g_malloc0(location_len);
    JSStringGetUTF8CString (location_str, location, location_len);
    JSStringRelease (location_str);

    uri = soup_uri_new (location);
    host_uri = soup_uri_copy_host (uri);
    origin = soup_uri_to_string (host_uri, FALSE);

    soup_uri_free (host_uri);
    soup_uri_free (uri);
    g_free (location);

    if (origin == NULL) {
      *exception = JSValueMakeNumber (context, 1);
      goto getInstalledByFinish;
    } else {
      callback_parameter = mozapps_app_objects_from_install_origin (context, origin, exception);
    }
    
    g_free (origin);

  getInstalledByFinish:

    if (*exception != NULL) {
      return JSValueMakeNull (context);
    } else {
      callback_arguments[0] = callback_parameter;
    
      return JSObjectCallAsFunction (context, object_ref, thisObject, 1, callback_arguments, exception);
    }
  }
}

typedef struct {
  char *install_origin;
  char *url;
  char *local_path;
  char *receipt;
  JSGlobalContextRef context;
  JSObjectRef thisObject;
  JSValueRef onSuccessCallback;
  JSValueRef onErrorCallback;
  GError *error;
} EphyMozAppInstallManifestData;

static JSValueRef
finish_install_manifest (EphyMozAppInstallManifestData *manifest_data, JSValueRef *exception)
{
  JSGlobalContextRef context;
  JSValueRef result;
  context = manifest_data->context;

  if (*exception == NULL && manifest_data->error == NULL && manifest_data->onSuccessCallback) {
    JSObjectCallAsFunction (context,
                            JSValueToObject (context, manifest_data->onSuccessCallback, NULL),
                            manifest_data->thisObject, 0, NULL, exception);
  } else if (*exception == NULL && manifest_data->error != NULL && manifest_data->onErrorCallback) {
    JSValueRef errorValue[1];
    JSStringRef codeString = NULL;
    JSStringRef messageString;
    JSStringRef prop_name;
    JSValueRef prop_value;

    errorValue[0] = JSObjectMakeError (context, 0, 0, exception);

    if (*exception == NULL) {
      if (manifest_data->error->domain == ERROR_QUARK) {
        switch (manifest_data->error->code) {
        case EPHY_WEB_APPLICATION_FORBIDDEN:
          codeString = JSStringCreateWithUTF8CString ("permissionDenied"); break;
        case EPHY_WEB_APPLICATION_MANIFEST_URL_ERROR:
          codeString = JSStringCreateWithUTF8CString ("manifestURLError"); break;
        case EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR:
          codeString = JSStringCreateWithUTF8CString ("manifestParseError"); break;
        case EPHY_WEB_APPLICATION_MANIFEST_INVALID:
          codeString = JSStringCreateWithUTF8CString ("invalidManifest"); break;
        case EPHY_WEB_APPLICATION_NETWORK:
          codeString = JSStringCreateWithUTF8CString ("networkError"); break;
        case EPHY_WEB_APPLICATION_CANCELLED:
        default:
          codeString = JSStringCreateWithUTF8CString ("denied"); break;
        }
      } else {
        codeString = JSStringCreateWithUTF8CString ("denied");
      }

      prop_name = JSStringCreateWithUTF8CString ("code");
      prop_value = JSValueMakeString (context, codeString);
      JSStringRelease (codeString);
      JSObjectSetProperty (context,
                           JSValueToObject (context, errorValue[0], NULL),
                           prop_name,
                           prop_value,
                           kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                           exception);
      JSStringRelease (prop_name);
    }

    if (*exception == NULL) {
      prop_name = JSStringCreateWithUTF8CString ("message");
      messageString = JSStringCreateWithUTF8CString (manifest_data->error->message);
      prop_value = JSValueMakeString (context, messageString);
      JSStringRelease (messageString);
      JSObjectSetProperty (context,
                           JSValueToObject (context, errorValue[0], NULL),
                           prop_name,
                           prop_value,
                           kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                           exception);
      JSStringRelease (prop_name);
    }

    if (*exception == NULL) {
      JSObjectCallAsFunction (context,
                              JSValueToObject (context, manifest_data->onErrorCallback, NULL),
                              manifest_data->thisObject, 1, errorValue, exception);
    }
  }

  g_free (manifest_data->url);
  g_free (manifest_data->local_path);
  g_free (manifest_data->receipt);
  g_free (manifest_data->install_origin);
  if (manifest_data->error)
    g_error_free (manifest_data->error);
  g_slice_free (EphyMozAppInstallManifestData, manifest_data);

  if (manifest_data->thisObject)
    JSValueUnprotect (context, manifest_data->thisObject);
  if (manifest_data->onSuccessCallback)
    JSValueUnprotect (context, manifest_data->onSuccessCallback);
  if (manifest_data->onErrorCallback)
    JSValueUnprotect (context, manifest_data->onErrorCallback);

  result = (*exception != NULL)?JSValueMakeNull(context):JSValueMakeUndefined(context);
  JSGlobalContextRelease (context);
  return result;
}

static void
install_manifest_cb (GError *error, gpointer userdata)
{
  EphyMozAppInstallManifestData *manifest_data = (EphyMozAppInstallManifestData *) userdata;
  JSValueRef exception = NULL;

  if (manifest_data->error == NULL && error != NULL) {
    manifest_data->error = g_error_copy (error);
  }

  finish_install_manifest (manifest_data, &exception);
}

static void
mozapp_install_manifest_download_status_changed_cb (WebKitDownload *download,
                                                    GParamSpec *spec,
                                                    EphyMozAppInstallManifestData *manifest_data)
{
	WebKitDownloadStatus status = webkit_download_get_status (download);
  JSValueRef exception = NULL;

	switch (status) {
	case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    {
      char *origin;

      origin = get_origin (manifest_data->url);
      ephy_web_application_install_manifest (NULL,
                                             origin,
                                             manifest_data->local_path,
                                             manifest_data->receipt,
                                             manifest_data->install_origin,
                                             install_manifest_cb,
                                             manifest_data);
      g_free (origin);
      g_object_unref (download);
    }
    break;

	case WEBKIT_DOWNLOAD_STATUS_ERROR:
    g_set_error (&(manifest_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_NETWORK, "Network error retrieving manifest");
    finish_install_manifest (manifest_data, &exception);
    g_object_unref (download);
    break;
	case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
    g_set_error (&(manifest_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_CANCELLED, "Application retrieval cancelled");
    finish_install_manifest (manifest_data, &exception);
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
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  }
  if (!JSValueIsString (context, arguments[0])) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  }
  url_str = JSValueToStringCopy (context, arguments[0], exception);
  if (url_str == NULL) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  }

  max_url_str_size = JSStringGetMaximumUTF8CStringSize (url_str);
  url = g_malloc0 (max_url_str_size);
  JSStringGetUTF8CString (url_str, url, max_url_str_size);
  JSStringRelease (url_str);

  if (argumentCount > 1) {
    JSStringRef json_str_receipt;

    json_str_receipt = JSValueCreateJSONString (context, arguments[1], 2, exception);
    if (*exception == NULL && json_str_receipt) {
      int max_receipt_str_size = JSStringGetMaximumUTF8CStringSize (json_str_receipt);
      receipt = g_malloc0 (max_receipt_str_size);
      JSStringGetUTF8CString (json_str_receipt, receipt, max_receipt_str_size);
      JSStringRelease (json_str_receipt);
    }
  }
  if (*exception != NULL) {
    g_free (url);
    return JSValueMakeNull (context);
  }

  install_manifest_data = g_slice_new0 (EphyMozAppInstallManifestData);
  install_manifest_data->url = url;
  install_manifest_data->local_path = NULL;
  install_manifest_data->install_origin = NULL;
  install_manifest_data->receipt = g_strdup (receipt);
  install_manifest_data->context = JSObjectGetPrivate (thisObject);
  JSGlobalContextRetain (install_manifest_data->context);
  install_manifest_data->thisObject = thisObject;
  if (thisObject != NULL)
    JSValueProtect (context, install_manifest_data->thisObject);
  install_manifest_data->onSuccessCallback = NULL;
  install_manifest_data->onErrorCallback = NULL;
  install_manifest_data->error = NULL;

  if (argumentCount > 2) {
    install_manifest_data->onSuccessCallback = arguments[2];
    JSValueProtect (context, install_manifest_data->onSuccessCallback);
  }

  if (argumentCount > 3) {
    install_manifest_data->onErrorCallback = arguments[3];
    JSValueProtect (context, install_manifest_data->onErrorCallback);
  }

  {
    JSStringRef script_ref;
    JSValueRef location_value;

    script_ref = JSStringCreateWithUTF8CString ("window.location.href");
    location_value = JSEvaluateScript (context, script_ref, NULL, NULL, 0, exception);
    JSStringRelease (script_ref);

    if ((*exception == NULL) && JSValueIsString (context, location_value)) {
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

  if (*exception != NULL) {
    return finish_install_manifest (install_manifest_data, exception);
  }

  request = webkit_network_request_new (url);
  if (request == NULL) {
    /* URL is invalid */
    g_set_error (&(install_manifest_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_MANIFEST_URL_ERROR, "Manifest URL is invalid");

    return finish_install_manifest (install_manifest_data, exception);
  }
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

  return JSValueMakeUndefined(context);
}

static const JSStaticFunction mozapps_class_staticfuncs[] =
{
{ "amInstalled", mozapps_am_installed, kJSPropertyAttributeNone },
{ "install", mozapps_install, kJSPropertyAttributeNone },
{ "getInstalledBy", mozapps_get_installed_by, kJSPropertyAttributeNone },
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

NULL,
NULL,

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
  JSClassRef mozAppsClassDef;
  JSObjectRef mozAppsClassObj;
  JSObjectRef globalObj;
  JSStringRef navigatorStr;
  JSValueRef navigatorRef;
  JSObjectRef navigatorObj;
  JSStringRef mozAppsStr;
  JSValueRef exception = NULL;

  globalObj = JSContextGetGlobalObject(context);
  navigatorStr = JSStringCreateWithUTF8CString("navigator");
  navigatorRef = JSObjectGetProperty (context, globalObj,
                                      navigatorStr, &exception);
  JSStringRelease (navigatorStr);
  navigatorObj = JSValueToObject (context, navigatorRef, &exception);


  mozAppsClassDef = JSClassCreate (&mozapps_class_def);
  mozAppsClassObj = JSObjectMake (context, mozAppsClassDef, context);
  JSObjectSetPrivate (mozAppsClassObj, context);
  mozAppsStr = JSStringCreateWithUTF8CString("mozApps");
  JSObjectSetProperty(context, navigatorObj, mozAppsStr, mozAppsClassObj,
                      kJSPropertyAttributeNone, &exception);
  JSClassRelease (mozAppsClassDef);
  JSStringRelease (mozAppsStr);
}

static gboolean
chrome_install_cb (gint response,
                   EphyWebApplication *app,
                   gpointer userdata)
{
  gboolean result = TRUE;
  char *manifest_path = (char *) userdata;

  if (response == GTK_RESPONSE_OK) {
    GFile *origin_manifest, *destination_manifest;
    char *manifest_install_path;
    GError *error = NULL;
    
    origin_manifest = g_file_new_for_path (manifest_path);
    manifest_install_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_MANIFEST);
    destination_manifest = g_file_new_for_path (manifest_install_path);

    result = g_file_copy (origin_manifest, destination_manifest, 
                          G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
                          NULL, NULL, NULL, 
                          &error);

    if (!result && error != NULL) {
      g_warning ("%s : %s", __FUNCTION__, error->message);
    }

    g_object_unref (origin_manifest);
    g_object_unref (destination_manifest);
    g_free (manifest_install_path);
  } else {
    result = FALSE;
  }

  g_free (manifest_path);

  return result;
}

static void
ephy_web_application_install_chrome_manifest (const char *origin,
                                              const char *manifest_url,
                                              const char *manifest_path)
{
  JsonParser *parser;
  GError *error = NULL;
  char *manifest_file_path;
  SoupURI *manifest_uri;

  if (!manifest_path || !manifest_url || !origin)
    return;

  manifest_file_path = g_filename_from_uri (manifest_path, NULL, &error);
  if (!manifest_file_path) {
    return;
  }

  manifest_uri = soup_uri_new (manifest_url);
  if (!manifest_uri) {
    g_free (manifest_file_path);
    return;
  }

  parser = json_parser_new ();
  if (json_parser_load_from_file (parser,
                                  manifest_file_path,
                                  NULL)) {
    JsonNode *root_node;
    JsonNode *node;
    EphyWebApplication *app;
    char *icon_href = NULL;

    app = ephy_web_application_new ();
    ephy_web_application_set_install_origin (app, origin);
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

    node = json_path_query ("$.launch_url", root_node, NULL);
    if (node) {
      if (JSON_NODE_HOLDS_ARRAY (node)) {
        JsonArray *array = json_node_get_array (node);
        if (json_array_get_length (array) > 0) {
          SoupURI *launch_uri;

          launch_uri = soup_uri_new_with_base (manifest_uri, json_array_get_string_element (array, 0));
          if (launch_uri) {
            char *launch_path;

            launch_path = soup_uri_to_string (launch_uri, TRUE);
            ephy_web_application_set_launch_path (app, launch_path);
            soup_uri_free (launch_uri);
          }
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
              SoupURI *icon_uri;

              icon_uri = soup_uri_new_with_base (manifest_uri, json_object_get_string_member (object, (char *) best_node->data));
              if (icon_uri) {
                icon_href = soup_uri_to_string (icon_uri, FALSE);
                soup_uri_free (icon_uri);
              }
            }
            g_list_free (members);
          }
        }
      }
      json_node_free (node);
    }

    ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_TEMPORARY);

    ephy_web_application_show_install_dialog (NULL,
                                              _("Install web application"), _("Install"),
                                              app, icon_href, NULL,
                                              chrome_install_cb, (gpointer) manifest_file_path);

    g_object_unref (app);
    g_free (icon_href);
    

  }
  soup_uri_free (manifest_uri);

  g_object_unref (parser);
}

typedef struct {
  char *install_origin;
  char *manifest_url;
  char *local_path;
} EphyChromeAppInstallManifestData;

static void
chrome_app_install_manifest_download_status_changed_cb (WebKitDownload *download,
                                                        GParamSpec *spec,
                                                        EphyChromeAppInstallManifestData *manifest_data)
{
  WebKitDownloadStatus status = webkit_download_get_status (download);

  switch (status) {
  case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    ephy_web_application_install_chrome_manifest (manifest_data->install_origin,
                                                  manifest_data->manifest_url,
                                                  manifest_data->local_path);
  case WEBKIT_DOWNLOAD_STATUS_ERROR:
  case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
    g_free (manifest_data->local_path);
    g_free (manifest_data->manifest_url);
    g_free (manifest_data->install_origin);
    g_slice_free (EphyChromeAppInstallManifestData, manifest_data);
    break;
  default:
    break;
  }
}

static JSValueRef
chrome_app_get_is_installed (JSContextRef context,
                             JSObjectRef object,
                             JSStringRef propertyName,
                             JSValueRef *exception)
{
  EphyWebApplication *app;
  bool is_installed = FALSE;

  app = ephy_web_application_new();
  if (ephy_web_application_load (app, ephy_dot_dir (), NULL)) {
    char *manifest_path;
    GFile *manifest_file;

    manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_MANIFEST);
    manifest_file = g_file_new_for_path (manifest_path);
    g_free (manifest_path);
    
    is_installed = g_file_query_exists (manifest_file, NULL);
    g_object_unref (manifest_file);

    if (!is_installed) {
      manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_WEBSTORE_MANIFEST);
      manifest_file = g_file_new_for_path (manifest_path);
      g_free (manifest_path);
    
      is_installed = g_file_query_exists (manifest_file, NULL);
      g_object_unref (manifest_file);
    }
  }

  return is_installed?JSValueMakeBoolean (context, TRUE):JSValueMakeUndefined (context);
}

static JSValueRef
chrome_app_get_details (JSContextRef context,
                        JSObjectRef function,
                        JSObjectRef thisObject,
                        size_t argumentCount,
                        const JSValueRef arguments[],
                        JSValueRef *exception)
{
  EphyWebApplication *app;
  gboolean is_ok = TRUE;
  GFile *manifest_file = NULL;
  char *manifest_contents = NULL;
  JSValueRef result = NULL;

  if (argumentCount != 0) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  }

  app = ephy_web_application_new();
  if (ephy_web_application_load (app, ephy_dot_dir (), NULL)) {
    char *manifest_path;

    manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_MANIFEST);
    manifest_file = g_file_new_for_path (manifest_path);
    g_free (manifest_path);
    
    is_ok = g_file_query_exists (manifest_file, NULL);
  }

  if (is_ok) {
    is_ok = g_file_load_contents (manifest_file, NULL, &manifest_contents, NULL, NULL, NULL);
  }

  if (is_ok) {
    JSStringRef manifest_string;

    manifest_string = JSStringCreateWithUTF8CString (manifest_contents);
    result = JSValueMakeFromJSONString (context, manifest_string);
    JSStringRelease (manifest_string);
  }

  if (manifest_file) g_object_unref (manifest_file);
  g_free (manifest_contents);

  if (is_ok) {
    return result?result:JSValueMakeUndefined (context);
  } else {
    return JSValueMakeNull (context);
  }

  return (result && is_ok)?result:JSValueMakeUndefined (context);
}

static JSValueRef
chrome_app_install (JSContextRef context,
                    JSObjectRef function,
                    JSObjectRef thisObject,
                    size_t argumentCount,
                    const JSValueRef arguments[],
                    JSValueRef *exception)
{
  JSStringRef fetch_href_string;
  JSObjectRef fetch_href;
  JSValueRef href_value;
  JSStringRef href_string;
  int href_length;
  JSStringRef fetch_window_href_string;
  JSValueRef window_href_value;
  char *window_href = NULL;
  char *href = NULL;

  if (argumentCount != 0) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  }

  fetch_href_string = JSStringCreateWithUTF8CString
    ("links = document.getElementsByTagName(\"link\");\n"
     "console.log(links.length);\n"
     "for (var i = 0; i < links.length; i++) {\n"
     "  console.log(links[i].rel);\n"
     "  console.log(links[i].href);\n"
     "  if (links[i].rel == 'chrome-application-definition' && links[i].href != null) {\n"
     "    return links[i].href;\n"
     "    break;\n"
     "  }\n"
     "}\n"
     "return null;");
  fetch_href = JSObjectMakeFunction (context, NULL, 0, NULL, fetch_href_string, NULL, 1, exception);
  JSStringRelease (fetch_href_string);
  if (*exception) return JSValueMakeNull (context);

  href_value = JSObjectCallAsFunction (context, fetch_href, NULL, 0, NULL, exception);
  if (*exception) return JSValueMakeNull (context);

  if (!JSValueIsString (context, href_value)) {
    return JSValueMakeUndefined (context);
  }
  href_string = JSValueToStringCopy (context, href_value, exception);
  if (*exception) return JSValueMakeNull (context);

  href_length = JSStringGetMaximumUTF8CStringSize (href_string);
  href = g_malloc0 (href_length);
  JSStringGetUTF8CString (href_string, href, href_length);
  JSStringRelease (href_string);

  fetch_window_href_string = JSStringCreateWithUTF8CString ("window.location.href");
  window_href_value = JSEvaluateScript (context, fetch_window_href_string, NULL, NULL, 0, exception);
  JSStringRelease (fetch_window_href_string);

  if (!JSValueIsString (context, window_href_value) || *exception) {
    *exception = JSValueMakeNumber (context, 1);
  }
  if (*exception == NULL) {
    JSStringRef window_href_string;
    window_href_string = JSValueToStringCopy (context, window_href_value, exception);

    if (*exception == NULL) {
      int window_href_length;

      window_href_length = JSStringGetMaximumUTF8CStringSize (window_href_string);
      window_href = g_malloc0(window_href_length);
      JSStringGetUTF8CString (window_href_string, window_href, window_href_length);
      JSStringRelease (window_href_string);
    }
  }
  g_warning ("HREF's window %s and manifest %s", window_href, href);

  if (window_href && href) {
    char *window_origin;
    char *manifest_origin;

    window_origin = get_origin (window_href);
    manifest_origin = get_origin (href);

    if (window_origin && manifest_origin && g_strcmp0 (window_origin, manifest_origin) == 0) {
      WebKitNetworkRequest *request;

      request = webkit_network_request_new (href);
      if (request == NULL) {
        *exception = JSValueMakeNumber (context, 1);
      } else {
        WebKitDownload *download;
        char *tmp_filename;
        char *destination;
        char *destination_uri;
        EphyChromeAppInstallManifestData *install_manifest_data;

        download = webkit_download_new (request);
        g_object_unref (request);

        tmp_filename = ephy_file_tmp_filename ("ephy-download-XXXXXX", NULL);
        destination = g_build_filename (ephy_file_tmp_dir (), tmp_filename, NULL);
        destination_uri = g_filename_to_uri (destination, NULL, NULL);
        webkit_download_set_destination_uri (download, destination_uri);

        install_manifest_data = g_slice_new0 (EphyChromeAppInstallManifestData);
        install_manifest_data->manifest_url = g_strdup (href);
        install_manifest_data->install_origin = g_strdup (manifest_origin);
        install_manifest_data->local_path = g_strdup (destination_uri);
        g_free (destination);
        g_free (destination_uri);
        g_free (tmp_filename);

        g_signal_connect (G_OBJECT (download), "notify::status",
                          G_CALLBACK (chrome_app_install_manifest_download_status_changed_cb), install_manifest_data);

        webkit_download_start (download);
      }
    } else {
      *exception = JSValueMakeNumber (context, 1);
    }
    g_free (window_origin);
    g_free (manifest_origin);
  }

  g_free (href);
  g_free (window_href);

  if (*exception) {
    return JSValueMakeNull (context);
  } else {
    return JSValueMakeUndefined (context);
  }
}

static const JSStaticValue chrome_app_class_statisvalues[] =
{
  { "isInstalled", chrome_app_get_is_installed, NULL, kJSPropertyAttributeReadOnly },
  { NULL, NULL, NULL, 0 }
};

static const JSStaticFunction chrome_app_class_staticfuncs[] =
{
{ "install", chrome_app_install, kJSPropertyAttributeNone },
{ "getDetails", chrome_app_get_details, kJSPropertyAttributeNone },
{ NULL, NULL, 0 }
};

static const JSClassDefinition chrome_app_class_def =
{
0,
kJSClassAttributeNone,
"EphyChromeAppClass",
NULL,

chrome_app_class_statisvalues,
chrome_app_class_staticfuncs,

NULL,
NULL,

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

static JSValueRef
chrome_webstore_private_install (JSContextRef context,
                                 JSObjectRef function,
                                 JSObjectRef thisObject,
                                 size_t argumentCount,
                                 const JSValueRef arguments[],
                                 JSValueRef *exception)
{
  return JSValueMakeNull (context);
}

typedef struct {
  EphyWebApplication *app;
  char *id;
  char *default_locale;
  char *manifest_data;
  char *update_url;
  char *icon_url;
  GdkPixbuf *icon_pixbuf;
  char *crx_file_path;
  GError *error;
  JSGlobalContextRef context;
  JSObjectRef this_object;
  JSObjectRef callback_function;
} ChromeWebstoreInstallData;

static void
finish_chrome_webstore_install_data (ChromeWebstoreInstallData *install_data)
{
  if (install_data->callback_function && 
      JSObjectIsFunction (install_data->context, install_data->callback_function)) {
    JSStringRef result_string;
    JSValueRef parameters[1];

    if (install_data->error) {
      if (install_data->error->domain == ERROR_QUARK) {
        switch (install_data->error->code) {
        case EPHY_WEB_APPLICATION_FORBIDDEN:
          result_string = JSStringCreateWithUTF8CString ("permission_denied"); break;
        case EPHY_WEB_APPLICATION_CANCELLED:
          result_string = JSStringCreateWithUTF8CString ("user_cancelled"); break;
        case EPHY_WEB_APPLICATION_MANIFEST_URL_ERROR:
        case EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR:
        case EPHY_WEB_APPLICATION_MANIFEST_INVALID:
        case EPHY_WEB_APPLICATION_CRX_EXTRACT_FAILED:
          result_string = JSStringCreateWithUTF8CString ("manifest_error"); break;
        default:
          result_string = JSStringCreateWithUTF8CString ("unknown_error");
        }
      } else {
        result_string = JSStringCreateWithUTF8CString ("unknown_error");
      }
    } else {
      result_string = JSStringCreateWithUTF8CString ("");
    }
    parameters[0] = JSValueMakeString (install_data->context, result_string);
    JSStringRelease (result_string);

    if (JSObjectIsFunction (install_data->context, install_data->callback_function)) {
      JSObjectCallAsFunction (install_data->context, install_data->callback_function, install_data->this_object, 1, parameters, NULL);
    }
  }

  if (install_data->callback_function)
    JSValueUnprotect (install_data->context, install_data->callback_function);
  if (install_data->this_object)
    JSValueUnprotect (install_data->context, install_data->this_object);
  JSGlobalContextRelease (install_data->context);

  g_object_unref (install_data->app);
  g_free (install_data->crx_file_path);
  g_free (install_data->manifest_data);
  g_free (install_data->update_url);
  g_free (install_data->icon_url);
  g_free (install_data->id);
  g_free (install_data->default_locale);
  if (install_data->icon_pixbuf)
    g_object_unref (install_data->icon_pixbuf);
  if (install_data->error) {
    g_error_free (install_data->error);
  }
  g_slice_free (ChromeWebstoreInstallData, install_data);
}

typedef struct {
  char *crx_file_path;
  char *extract_path;
  char buffer[128];
  GInputStream *crx_read;
  GError *error;
} CRXExtractData;

typedef struct {
  char magic_number[4];
  guint32 version;
  guint32 public_key_length;
  guint32 signature_length;
} CRXFirstHeader;

static void
finish_crx_extract_data (CRXExtractData *extract_data)
{
  g_free (extract_data->crx_file_path);
  g_free (extract_data->extract_path);
  g_free  (extract_data);
}

static ssize_t
extract_data_archive_read (struct archive *archive, 
                           void *data,
                           const void **buffer)
{
  gssize read_bytes;
  CRXExtractData *extract_data = (CRXExtractData *) data;

  *buffer = extract_data->buffer;
  read_bytes = g_input_stream_read (G_INPUT_STREAM (extract_data->crx_read),
                                    extract_data->buffer,
                                    sizeof (extract_data->buffer),
                                    NULL,
                                    &extract_data->error);
  return read_bytes;
}

static int
extract_data_archive_close (struct archive *zip_archive,
                            void *data)
{
  CRXExtractData *extract_data = (CRXExtractData *) data;
  g_object_unref (extract_data->crx_read);
  extract_data->crx_read = NULL;

  return ARCHIVE_OK;
}

static void
crx_extract_thread (GSimpleAsyncResult *result,
                    GObject *object,
                    GCancellable *cancellable)
{
  CRXExtractData *extract_data;
  GFile *crx_file;
  GInputStream *crx_is = NULL;
  GInputStream *crx_read = NULL;
  GError *error = NULL;
  CRXFirstHeader first_header;
  int skip_count;
  struct archive *zip_archive = NULL;
  int archive_result;
  char *buffer;

  extract_data = g_simple_async_result_get_op_res_gpointer (result);

  crx_file = g_file_new_for_path (extract_data->crx_file_path);
  crx_is = (GInputStream *) g_file_read (crx_file, cancellable, &error);
  g_object_unref (crx_file);
  if (error) goto finish;
  crx_read = (GInputStream *) g_data_input_stream_new (crx_is);
  extract_data->crx_read = g_object_ref (crx_read);
  g_object_unref (crx_is);
  g_data_input_stream_set_byte_order (G_DATA_INPUT_STREAM (crx_read), G_LITTLE_ENDIAN);

  buffer = (char *) &first_header;
  bzero (buffer, sizeof (first_header));

  do {
    gsize bytes_read = 0;

    memmove (buffer, buffer + 1, sizeof (first_header) - 1);

    if (!g_input_stream_read_all (crx_read, buffer + (sizeof (first_header) - 1), sizeof (char) , &bytes_read, cancellable, &error) || bytes_read == 0) {
      g_set_error (&error, ERROR_QUARK, EPHY_WEB_APPLICATION_CRX_EXTRACT_FAILED, "CRX header not found");
      goto finish;
    }

    if (strncmp (first_header.magic_number, "Cr24", 4) != 0) {
      continue;
    }

    if (first_header.version != 2) {
      continue;
    }
    break;
  } while (TRUE);

  /* We don't implement signature check, so we skip */
  skip_count = first_header.public_key_length + first_header.signature_length;
  while (skip_count > 0) {
    gssize result;
    result = g_input_stream_skip (crx_read, skip_count, cancellable, &error);
    if (result == -1) {
      goto finish;
    }
    skip_count -= result;
  }

  /* So offset now is the zip file, we use libarchive with it */
  zip_archive = archive_read_new ();
  archive_read_support_format_zip (zip_archive);
  archive_read_open (zip_archive, extract_data,
                     NULL,
                     extract_data_archive_read,
                     extract_data_archive_close);
  
  do {
    struct archive_entry *entry;
    archive_result = archive_read_next_header (zip_archive, &entry);

    if (archive_result >= ARCHIVE_WARN && archive_result <= ARCHIVE_OK) {
      if (archive_result < ARCHIVE_OK) {
        archive_set_error (zip_archive, ARCHIVE_OK, "No error");
        archive_clear_error (zip_archive);

      } else {
        char *new_path;
        new_path = g_strdup_printf ("%s/%s", extract_data->extract_path, archive_entry_pathname (entry));
        archive_entry_set_pathname (entry, new_path);
        archive_result = archive_read_extract (zip_archive, entry, 0);
        g_free (new_path);
      }
    }

  } while (archive_result != ARCHIVE_EOF && archive_result != ARCHIVE_FATAL);
  

 finish:

  if (zip_archive) {
    archive_read_finish (zip_archive);
  }
  if (crx_read) {
    if (!g_input_stream_is_closed (crx_read))
      g_input_stream_close (crx_read, cancellable, &error);
    g_object_unref  (crx_read);
  }
  if (error) {
    extract_data->error = g_error_copy (error);
    g_error_free (error);
  }
}

static void
crx_extract (const char *crx_file_path,
             const char *extract_path,
             gint io_priority,
             GCancellable *cancellable,
             GAsyncReadyCallback callback,
             gpointer userdata)
{
  GSimpleAsyncResult *simple;
  CRXExtractData *extract_data;

  simple = g_simple_async_result_new (NULL,
                                      callback, userdata,
                                      crx_extract);

  extract_data = g_new0 (CRXExtractData, 1);
  g_simple_async_result_set_op_res_gpointer (simple, extract_data, (GDestroyNotify) finish_crx_extract_data);
  extract_data->crx_file_path = g_strdup (crx_file_path);
  extract_data->extract_path = g_strdup (extract_path);
  
  if (g_mkdir_with_parents (extract_path, 0755) == -1) {
    g_set_error (&(extract_data->error), ERROR_QUARK, 
                 EPHY_WEB_APPLICATION_CRX_EXTRACT_FAILED,
                 "Couldn't create destination folder");
    g_simple_async_result_complete_in_idle (simple);
    g_object_unref (simple);
    return;
  }

  g_simple_async_result_run_in_thread (simple, crx_extract_thread, io_priority, cancellable);
  g_object_unref (simple);
}

static gboolean
crx_extract_finish (GAsyncResult *result,
                    GError **error)
{
  CRXExtractData *extract_data;
  extract_data = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
  if (extract_data->error)
    *error = g_error_copy (extract_data->error);
  return *error == NULL;
}

static gboolean 
chrome_webstore_install_cb (gint response,
                            EphyWebApplication *app,
                            gpointer userdata)
{
  gboolean result = TRUE;
  ChromeWebstoreInstallData *install_data = (ChromeWebstoreInstallData *) userdata;

  if (response == GTK_RESPONSE_OK) {
    {
      char *manifest_install_path;

      manifest_install_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_WEBSTORE_MANIFEST);

      result = g_file_set_contents (manifest_install_path,
                                    install_data->manifest_data,
                                    -1,
                                    &(install_data->error));

      g_free (manifest_install_path);

    }

  } else {
    g_set_error (&(install_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_CANCELLED, "User cancelled installation");
  }

  finish_chrome_webstore_install_data (install_data);

  return result;
}

static char *
crx_extract_msg_id (const char *str)
{
  if (g_str_has_prefix (str, "__MSG_") && g_str_has_suffix (str + 6, "__"))
    return (g_strndup (str + 6, strlen(str) - 8));
  else
    return NULL;
}

static char *
crx_get_translation (const char *path, const char *key, const char * default_locale)
{
  char ** lang;
  char *result = NULL;
  char *key_json_path;

  key_json_path = g_strconcat ("$.", key, ".message", NULL);

  for (lang = (char **) g_get_language_names (); ; lang++) {
    char * file_path;
    GFile *file;

    if (*lang == NULL && default_locale == NULL)
      break;

    file_path = g_build_filename (path, "_locales", *lang?*lang:default_locale, "messages.json", NULL);
    file = g_file_new_for_path (file_path);
    if (g_file_query_exists (file, NULL)) {
      JsonParser *parser;

      parser = json_parser_new ();
      if (json_parser_load_from_file (parser, file_path, NULL)) {
        JsonNode *root_node;
        JsonNode *node;

        root_node = json_parser_get_root (parser);
        node = json_path_query (key_json_path, root_node, NULL);
        if (node) {
          if (JSON_NODE_HOLDS_ARRAY (node)) {
            JsonArray *array = json_node_get_array (node);
            if (json_array_get_length (array) > 0) {
              result = g_strdup (json_array_get_string_element (array, 0));
            }
          }
          json_node_free (node);
        }

      }
    }
    g_object_unref (file);
    g_free (file_path);
    if (*lang == NULL || result != NULL)
      break;
  }
  g_free (key_json_path);

  return result;
}

static void
on_crx_extract (GObject *object,
                GAsyncResult *result,
                gpointer userdata)
{
  GError *error = NULL;
  ChromeWebstoreInstallData *install_data = (ChromeWebstoreInstallData *) userdata;

  if (crx_extract_finish (result, &error)) {
    char *key_id;

    char *crx_contents_path;

    crx_contents_path = ephy_web_application_get_settings_file_name (install_data->app, EPHY_WEB_APPLICATION_CHROME_CRX_CONTENTS);

    key_id = crx_extract_msg_id (ephy_web_application_get_name (EPHY_WEB_APPLICATION (install_data->app)));
    if (key_id != NULL) {
      char *name = crx_get_translation (crx_contents_path, key_id, install_data->default_locale);
      ephy_web_application_set_name (install_data->app, name);
      g_free (name);
      g_free (key_id);
    }

    key_id = crx_extract_msg_id (ephy_web_application_get_description (EPHY_WEB_APPLICATION (install_data->app)));
    if (key_id != NULL) {
      char *description = crx_get_translation (crx_contents_path, key_id, install_data->default_locale);
      ephy_web_application_set_description (install_data->app, description);
      g_free (description);
      g_free (key_id);
    }

    g_free (crx_contents_path);
    ephy_web_application_show_install_dialog
      (NULL,
       _("Install Chrome web store application"), _("Install"),
       install_data->app, install_data->icon_url, install_data->icon_pixbuf,
       chrome_webstore_install_cb, install_data);
  } else {
    g_propagate_error (&(install_data->error), error);
    finish_chrome_webstore_install_data (install_data);
  }
}

static void
crx_download_status_changed_cb (WebKitDownload *download,
                                GParamSpec *spec,
                                ChromeWebstoreInstallData *install_data)
{
  WebKitDownloadStatus status = webkit_download_get_status (download);

  switch (status) {
  case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    {
      char *crx_path, *crx_contents_path;
      GFile *origin_crx, *destination_crx;

      install_data->crx_file_path = g_filename_from_uri (webkit_download_get_destination_uri (download), NULL, NULL);

      origin_crx = g_file_new_for_path (install_data->crx_file_path);
      crx_path = ephy_web_application_get_settings_file_name (install_data->app, EPHY_WEB_APPLICATION_CHROME_CRX);
      crx_contents_path = ephy_web_application_get_settings_file_name (install_data->app, EPHY_WEB_APPLICATION_CHROME_CRX_CONTENTS);
      destination_crx = g_file_new_for_path (crx_path);
      crx_contents_path = ephy_web_application_get_settings_file_name (install_data->app, EPHY_WEB_APPLICATION_CHROME_CRX_CONTENTS);

      g_file_copy (origin_crx, destination_crx, 
                   G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
                   NULL, NULL, NULL, 
                   NULL);

      crx_extract (install_data->crx_file_path, crx_contents_path,
                   G_PRIORITY_DEFAULT_IDLE, NULL,
                   on_crx_extract, install_data);

      g_object_unref (origin_crx);
      g_object_unref (destination_crx);
      g_free (crx_path);

      break;
    }
  case WEBKIT_DOWNLOAD_STATUS_ERROR:
  case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
    ephy_web_application_show_install_dialog
      (NULL,
       _("Install Chrome web store application"), _("Install"),
       install_data->app, install_data->icon_url, install_data->icon_pixbuf,
       chrome_webstore_install_cb, install_data);
    break;
  default:
    break;
  }
}
static gboolean
chrome_retrieve_crx (char *crx_url, 
                     ChromeWebstoreInstallData *install_data)
{
  WebKitNetworkRequest *request;
  WebKitDownload *download;
  char *tmp_filename;
  char *destination;
  char *destination_uri;

  request = webkit_network_request_new (crx_url);
  if (request == NULL) {
    return FALSE;
  }

  download = webkit_download_new (request);
  g_object_unref (request);

  tmp_filename = ephy_file_tmp_filename ("ephy-download-XXXXXX", NULL);
  destination = g_build_filename (ephy_file_tmp_dir (), tmp_filename, NULL);
  destination_uri = g_filename_to_uri (destination, NULL, NULL);
  webkit_download_set_destination_uri (download, destination_uri);

  g_signal_connect (G_OBJECT (download), "notify::status",
                    G_CALLBACK (crx_download_status_changed_cb), install_data);
  webkit_download_start (download);

  return TRUE;
}

static void
crx_update_xml_download_status_changed_cb (WebKitDownload *download,
                                           GParamSpec *spec,
                                           ChromeWebstoreInstallData *install_data)
{
  WebKitDownloadStatus status = webkit_download_get_status (download);

  switch (status) {
  case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    {
      xmlDocPtr document;

      document = xmlParseFile (webkit_download_get_destination_uri (download));
      if (document) {
        xmlXPathContextPtr context;
        context = xmlXPathNewContext (document);
        if (context) {
          char *xpath;
          xmlXPathObjectPtr result;
          xmlXPathRegisterNs (context, (xmlChar *) "ur",
                              (xmlChar *) "http://www.google.com/update2/response");
          
          xpath = g_strdup_printf ("/ur:gupdate/ur:app[@appid='%s']/ur:updatecheck/@codebase", install_data->id);
          result = xmlXPathEvalExpression ((xmlChar *) xpath, context);
          if (result != NULL && xmlXPathNodeSetGetLength (result->nodesetval) == 1) {
            xmlAttrPtr idNode;
            idNode = (xmlAttrPtr) xmlXPathNodeSetItem (result->nodesetval, 0);
            if (idNode->children && idNode->children->type == XML_TEXT_NODE) {
              char *crx_url;

              crx_url = (char *) idNode->children->content;
              g_warning ("%s : retrieved crx url : %s", __FUNCTION__, crx_url);
              if (chrome_retrieve_crx (crx_url, install_data)) {
                break;
              }
            }
          }
          g_free (xpath);
          xmlXPathFreeContext (context);
        }
        xmlFreeDoc (document);
      }
    }
    // TODO: parse update xml and retrieve and install crx
  case WEBKIT_DOWNLOAD_STATUS_ERROR:
  case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
    ephy_web_application_show_install_dialog
      (NULL,
       _("Install Chrome web store application"), _("Install"),
       install_data->app, install_data->icon_url, install_data->icon_pixbuf,
       chrome_webstore_install_cb, install_data);
    break;
  default:
    break;
  }
}

static gboolean
chrome_retrieve_crx_update_xml (EphyWebApplication *app, 
                                ChromeWebstoreInstallData *install_data)
{
  char *full_uri;
  gboolean result = FALSE;

  {
    SoupURI *xml_uri;
    char *extension_value;

    xml_uri = soup_uri_new (install_data->update_url);
    extension_value = g_strdup_printf ("id=%s&uc", install_data->id);

    soup_uri_set_query_from_fields (xml_uri, "x", extension_value, NULL);
    g_free (extension_value);

    full_uri = soup_uri_to_string (xml_uri, FALSE);
    soup_uri_free (xml_uri);
  }

  {
    WebKitNetworkRequest *request;
    WebKitDownload *download;
    char *tmp_filename;
    char *destination;
    char *destination_uri;

    request = webkit_network_request_new (full_uri);
    if (request == NULL) {
      goto finish;
    }

    download = webkit_download_new (request);
    g_object_unref (request);

    tmp_filename = ephy_file_tmp_filename ("ephy-download-XXXXXX", NULL);
    destination = g_build_filename (ephy_file_tmp_dir (), tmp_filename, NULL);
    destination_uri = g_filename_to_uri (destination, NULL, NULL);
    webkit_download_set_destination_uri (download, destination_uri);

    g_signal_connect (G_OBJECT (download), "notify::status",
                      G_CALLBACK (crx_update_xml_download_status_changed_cb), install_data);
    webkit_download_start (download);
    result = TRUE;

  }

 finish:
  g_free (full_uri);

  return result;
}

static JSValueRef
chrome_webstore_private_begin_install_with_manifest (JSContextRef context,
                                                     JSObjectRef function,
                                                     JSObjectRef thisObject,
                                                     size_t argumentCount,
                                                     const JSValueRef arguments[],
                                                     JSValueRef *exception)
{
  JSObjectRef details_obj;
  JSStringRef prop_name;
  JSValueRef prop_value;
  char *id = NULL;
  char *default_locale = NULL;
  char *name = NULL;
  char *description = NULL;
  char *manifest = NULL;
  char *icon_url = NULL;
  char *icon_data = NULL;
  char *localized_name = NULL;
  char *web_url = NULL;
  char *update_url = NULL;
  JSObjectRef callback_function = NULL;

  if (argumentCount > 2 || 
      !JSValueIsObject (context, arguments[0])) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  } else if (argumentCount == 2) {
    callback_function = JSValueToObject (context, arguments[1], exception);
    if (*exception) return JSValueMakeNull (context);
  }

  details_obj = JSValueToObject (context, arguments[0], exception);
  if (*exception) return JSValueMakeNull (context);

  prop_name = JSStringCreateWithUTF8CString ("id");
  prop_value = JSObjectGetProperty (context, details_obj, prop_name, exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef id_string;
    id_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    id = js_string_to_utf8 (id_string);
  }
  g_warning ("%s : retrieved id: %s", __FUNCTION__, id?id:"(null)");

  prop_name = JSStringCreateWithUTF8CString ("manifest");
  prop_value = JSObjectGetProperty (context, details_obj, prop_name, exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef manifest_string;

    manifest_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception == NULL) {
      JsonParser *parser;
      GError *error = NULL;

      manifest = js_string_to_utf8 (manifest_string);
      parser = json_parser_new ();


      if (json_parser_load_from_data (parser, manifest, -1, &error)) {
        JsonNode *root_node;
        JsonNode *node;

        root_node = json_parser_get_root (parser);
        node = json_path_query ("$.name", root_node, NULL);
        if (node) {
          if (JSON_NODE_HOLDS_ARRAY (node)) {
            JsonArray *array = json_node_get_array (node);
            if (json_array_get_length (array) > 0) {
              name = g_strdup (json_array_get_string_element (array, 0));
            }
          }
          json_node_free (node);
        }

        node = json_path_query ("$.description", root_node, NULL);
        if (node) {
          if (JSON_NODE_HOLDS_ARRAY (node)) {
            JsonArray *array = json_node_get_array (node);
            if (json_array_get_length (array) > 0) {
              description = g_strdup (json_array_get_string_element (array, 0));
            }
          }
          json_node_free (node);
        }

        node = json_path_query ("$.app.launch.web_url", root_node, NULL);
        if (node) {
          if (JSON_NODE_HOLDS_ARRAY (node)) {
            JsonArray *array = json_node_get_array (node);
            if (json_array_get_length (array) > 0) {
              web_url = g_strdup (json_array_get_string_element (array, 0));
            }
          }
          json_node_free (node);
        }

        node = json_path_query ("$.update_url", root_node, NULL);
        if (node) {
          if (JSON_NODE_HOLDS_ARRAY (node)) {
            JsonArray *array = json_node_get_array (node);
            if (json_array_get_length (array) > 0) {
              update_url = g_strdup (json_array_get_string_element (array, 0));
            }
          }
          json_node_free (node);
        }

      } else if (error != NULL) {
        g_warning ("%s : failed parsing manifest json: %s", __FUNCTION__, error->message);
      }
    }
  }
  g_warning ("%s : retrieved manifest: %s", __FUNCTION__, manifest?manifest:"(null)");

  prop_name = JSStringCreateWithUTF8CString ("iconUrl");
  prop_value = JSObjectGetProperty (context, details_obj, prop_name, exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef icon_url_string;
    icon_url_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    icon_url = js_string_to_utf8 (icon_url_string);
  }
  g_warning ("%s : retrieved iconUrl: %s", __FUNCTION__, icon_url?icon_url:"(null)");

  prop_name = JSStringCreateWithUTF8CString ("iconData");
  prop_value = JSObjectGetProperty (context, details_obj, prop_name, exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef icon_data_string;
    icon_data_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    icon_data = js_string_to_utf8 (icon_data_string);
  }
  g_warning ("%s : retrieved iconData: %s", __FUNCTION__, icon_data?icon_data:"(null)");

  prop_name = JSStringCreateWithUTF8CString ("localizedName");
  prop_value = JSObjectGetProperty (context, details_obj, prop_name, exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef localized_name_string;
    localized_name_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    localized_name = js_string_to_utf8 (localized_name_string);
  }
  g_warning ("%s : retrieved localizedName: %s", __FUNCTION__, localized_name?localized_name:"(null)");

  prop_name = JSStringCreateWithUTF8CString ("default_locale");
  prop_value = JSObjectGetProperty (context, details_obj, prop_name, exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef default_locale_string;
    default_locale_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    default_locale = js_string_to_utf8 (default_locale_string);
  }
  g_warning ("%s : retrieved localizedName: %s", __FUNCTION__, localized_name?localized_name:"(null)");

  if (name && manifest && web_url) {
    EphyWebApplication *app;
    char *used_icon_url = NULL;
    GdkPixbuf *icon_pixbuf = NULL;
    ChromeWebstoreInstallData *install_data;

    app = ephy_web_application_new ();

    ephy_web_application_set_name (app, localized_name?localized_name:name);
    if (description) ephy_web_application_set_description (app, description);
    ephy_web_application_set_full_uri (app, web_url);

    ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_TEMPORARY);

    if (icon_data) {
      GdkPixbufLoader *loader;
      guchar *icon_data_decoded;
      gsize length = 0;
      GError *error = NULL;

      icon_data_decoded = g_base64_decode (icon_data, &length);

      loader = gdk_pixbuf_loader_new ();
      if (gdk_pixbuf_loader_write (loader, icon_data_decoded, length, &error)) {
        icon_pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
      } else {
        g_warning ("%s: %s", __FUNCTION__, error->message);
      }
      g_free (icon_data_decoded);

      g_object_unref (loader);
    }

    if (icon_url && !icon_pixbuf) {
      if (g_str_has_prefix (icon_url, "//")) {
        used_icon_url = g_strconcat ("http:", icon_url, NULL);
      } else {
        used_icon_url = g_strdup (icon_url);
      }
    }

    install_data = g_slice_new0 (ChromeWebstoreInstallData);
    install_data->id = g_strdup (id);
    install_data->default_locale = g_strdup (default_locale);
    install_data->app = g_object_ref (app);
    install_data->error = NULL;
    install_data->manifest_data = g_strdup (manifest);
    install_data->update_url = update_url?g_strdup(update_url):DEFAULT_CHROME_WEBSTORE_CRX_UPDATE_PATH;
    install_data->icon_url = g_strdup (used_icon_url);
    install_data->icon_pixbuf = icon_pixbuf?g_object_ref (icon_pixbuf):NULL;
    install_data->context = JSGlobalContextCreateInGroup (JSContextGetGroup (context), NULL);
    install_data->callback_function = callback_function;
    install_data->this_object = thisObject;
    if (thisObject)
      JSValueProtect (context, thisObject);
    if (callback_function)
      JSValueProtect (context, callback_function);

    if (!chrome_retrieve_crx_update_xml (app, install_data)) {

      ephy_web_application_show_install_dialog (NULL,
                                                _("Install Chrome web store application"), _("Install"),
                                                app, used_icon_url, icon_pixbuf,
                                                chrome_webstore_install_cb, install_data);
    }
    g_object_unref (app);
    g_free (used_icon_url);
    if (icon_pixbuf) g_object_unref (icon_pixbuf);

  } else if (argumentCount == 2) {
    JSStringRef result_string;
    JSValueRef parameters[1];

    result_string = JSStringCreateWithUTF8CString ("manifest_error");
    parameters[0] = JSValueMakeString (context, result_string);
    JSStringRelease (result_string);

    if (JSObjectIsFunction (context, callback_function)) {
      JSObjectCallAsFunction (context, callback_function, NULL, 1, parameters, exception);
    }
  }

 finish:

  g_free (id);
  g_free (name);
  g_free (description);
  g_free (manifest);
  g_free (icon_url);
  g_free (icon_data);
  g_free (localized_name);
  g_free (web_url);
  if (*exception) return JSValueMakeNull (context);
  return JSValueMakeUndefined (context);
}

static JSValueRef
chrome_webstore_private_complete_install (JSContextRef context,
                                          JSObjectRef function,
                                          JSObjectRef thisObject,
                                          size_t argumentCount,
                                          const JSValueRef arguments[],
                                          JSValueRef *exception)
{
  if (argumentCount > 2) {
    *exception = JSValueMakeNumber (context, 1);
    goto finish;
  }

  if (argumentCount == 2) {
    JSObjectRef callback_function;
    callback_function = JSValueToObject (context, arguments[1], exception);
    if (*exception) goto finish;

    if (JSObjectIsFunction (context, callback_function)) {
      JSObjectCallAsFunction (context, callback_function, NULL, 0, NULL, exception);
    }
    
  }

 finish:
  if (*exception) {
    return JSValueMakeNull (context);
  } else {
    return JSValueMakeUndefined (context);
  }
}

static JSValueRef
chrome_webstore_private_silently_install (JSContextRef context,
                                          JSObjectRef function,
                                          JSObjectRef thisObject,
                                          size_t argumentCount,
                                          const JSValueRef arguments[],
                                          JSValueRef *exception)
{
  return JSValueMakeNull (context);
}

static JSValueRef
chrome_webstore_private_get_browser_login (JSContextRef context,
                                          JSObjectRef function,
                                          JSObjectRef thisObject,
                                          size_t argumentCount,
                                          const JSValueRef arguments[],
                                          JSValueRef *exception)
{
  return JSValueMakeNull (context);
}

static JSValueRef
chrome_webstore_private_get_store_login (JSContextRef context,
                                         JSObjectRef function,
                                         JSObjectRef thisObject,
                                         size_t argumentCount,
                                         const JSValueRef arguments[],
                                         JSValueRef *exception)
{
  return JSValueMakeNull (context);
}

static JSValueRef
chrome_webstore_private_set_store_login (JSContextRef context,
                                         JSObjectRef function,
                                         JSObjectRef thisObject,
                                         size_t argumentCount,
                                         const JSValueRef arguments[],
                                         JSValueRef *exception)
{
  return JSValueMakeNull (context);
}

static JSValueRef
chrome_webstore_private_prompt_browser_login (JSContextRef context,
                                              JSObjectRef function,
                                              JSObjectRef thisObject,
                                              size_t argumentCount,
                                              const JSValueRef arguments[],
                                              JSValueRef *exception)
{
  return JSValueMakeNull (context);
}

static JSValueRef
chrome_webstore_private_get_webgl_status (JSContextRef context,
                                          JSObjectRef function,
                                          JSObjectRef thisObject,
                                          size_t argumentCount,
                                          const JSValueRef arguments[],
                                          JSValueRef *exception)
{
  JSObjectRef callback_function;
  JSStringRef detect_script_string;
  JSObjectRef detect_script;
  JSValueRef script_result_value;
  gboolean result = FALSE;

  if (argumentCount != 1 || !JSValueIsObject(context, arguments[0])) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  }

  callback_function = JSValueToObject (context, arguments[0], exception);
  if (!*exception && !JSObjectIsFunction (context, callback_function)) {
    *exception = JSValueMakeNumber (context, 1);
  }
  if (*exception) return JSValueMakeNull (context);

  detect_script_string = JSStringCreateWithUTF8CString
    ("var canvas = document.createElement('canvas');"
     "return !!(window.WebGLRenderingContext && (canvas.getContext('webgl') || canvas.getContext('experimental-webgl')));");
  detect_script = JSObjectMakeFunction (context, NULL, 0, NULL, detect_script_string, NULL, 1, exception);
  JSStringRelease (detect_script_string);
  if (*exception) return JSValueMakeNull (context);

  script_result_value = JSObjectCallAsFunction (context, detect_script, NULL, 0, NULL, NULL);
  if (JSValueIsBoolean (context, script_result_value)) {
    result = JSValueToBoolean (context, script_result_value);
  }

  {
    JSStringRef result_string;
    JSValueRef cb_arguments[1];

    result_string = JSStringCreateWithUTF8CString (result?"webgl_allowed":"webgl_blocked");
    cb_arguments[0] = JSValueMakeString (context, result_string);
    JSStringRelease (result_string);

    return JSObjectCallAsFunction (context, callback_function, NULL, 1, cb_arguments, exception);
  }

}

static const JSStaticFunction chrome_webstore_private_class_staticfuncs[] =
{
{ "getWebGLStatus", chrome_webstore_private_get_webgl_status, kJSPropertyAttributeNone },
{ "install", chrome_webstore_private_install, kJSPropertyAttributeNone },
{ "beginInstallWithManifest3", chrome_webstore_private_begin_install_with_manifest, kJSPropertyAttributeNone },
{ "completeInstall", chrome_webstore_private_complete_install, kJSPropertyAttributeNone },
{ "silentlyInstall", chrome_webstore_private_silently_install, kJSPropertyAttributeNone },
{ "getBrowserLogin", chrome_webstore_private_get_browser_login, kJSPropertyAttributeNone },
{ "getStoreLogin", chrome_webstore_private_get_store_login, kJSPropertyAttributeNone },
{ "setStoreLogin", chrome_webstore_private_set_store_login, kJSPropertyAttributeNone },
{ "promptBrowserLogin", chrome_webstore_private_prompt_browser_login, kJSPropertyAttributeNone },
{ NULL, NULL, 0 }
};

static const JSClassDefinition chrome_webstore_private_class_def =
{
0,
kJSClassAttributeNone,
"EphyChromeWebstorePrivateClass",
NULL,

NULL,
chrome_webstore_private_class_staticfuncs,

NULL,
NULL,

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

static JSValueRef
chrome_management_get_all (JSContextRef context,
                           JSObjectRef function,
                           JSObjectRef thisObject,
                           size_t argumentCount,
                           const JSValueRef arguments[],
                           JSValueRef *exception)
{
  JSObjectRef callback_function = NULL;

  if (argumentCount > 1 || (argumentCount == 1 && !JSValueIsObject(context, arguments[0]))) {
    *exception = JSValueMakeNumber (context, 1);
    return JSValueMakeNull (context);
  }

  if (argumentCount == 1) {
    callback_function = JSValueToObject (context, arguments[0], exception);
    if (!*exception && !JSObjectIsFunction (context, callback_function)) {
      *exception = JSValueMakeNumber (context, 1);
    }
  }
  if (*exception) return JSValueMakeNull (context);

  {
    JSValueRef cb_arguments[1];

    cb_arguments[0] = JSObjectMakeArray (context, 0, NULL, exception);

    if (callback_function) {
      JSObjectCallAsFunction (context, callback_function, NULL, 1, cb_arguments, exception);
    }
    return cb_arguments[0];
  }
}

static JSValueRef
chrome_management_uninstall (JSContextRef context,
                             JSObjectRef function,
                             JSObjectRef thisObject,
                             size_t argumentCount,
                             const JSValueRef arguments[],
                             JSValueRef *exception)
{
  return JSValueMakeNull (context);
}

static JSValueRef
chrome_management_launch_app (JSContextRef context,
                              JSObjectRef function,
                              JSObjectRef thisObject,
                              size_t argumentCount,
                              const JSValueRef arguments[],
                              JSValueRef *exception)
{
  return JSValueMakeNull (context);
}

static const JSStaticFunction chrome_management_class_staticfuncs[] =
{
{ "getAll", chrome_management_get_all, kJSPropertyAttributeNone },
{ "uninstall", chrome_management_uninstall, kJSPropertyAttributeNone },
{ "launchApp", chrome_management_launch_app, kJSPropertyAttributeNone },
{ NULL, NULL, 0 }
};

static const JSClassDefinition chrome_management_class_def =
{
0,
kJSClassAttributeNone,
"EphyChromeManagementClass",
NULL,

NULL,
chrome_management_class_staticfuncs,

NULL,
NULL,

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
ephy_web_application_setup_chrome_api (JSGlobalContextRef context)
{
  JSObjectRef global_obj;
  JSStringRef prop_name;
  JSValueRef exception = NULL;

  JSObjectRef chrome_obj;

  JSClassRef chrome_app_class;
  JSObjectRef chrome_app_obj;

  JSClassRef chrome_webstore_private_class;
  JSObjectRef chrome_webstore_private_obj;

  JSClassRef chrome_management_class;
  JSObjectRef chrome_management_obj;

  JSStringRef on_installed_string;
  JSObjectRef on_installed_function;
  JSValueRef on_installed_value;
  JSStringRef on_uninstalled_string;
  JSObjectRef on_uninstalled_function;
  JSValueRef on_uninstalled_value;

  global_obj = JSContextGetGlobalObject(context);

  chrome_obj = JSObjectMake (context, NULL, NULL);
  prop_name = JSStringCreateWithUTF8CString ("chrome");
  JSObjectSetProperty (context, global_obj, prop_name, chrome_obj, kJSPropertyAttributeNone, &exception);
  JSStringRelease (prop_name);

  chrome_app_class = JSClassCreate (&chrome_app_class_def);
  chrome_app_obj = JSObjectMake (context, chrome_app_class, NULL);
  prop_name = JSStringCreateWithUTF8CString ("app");
  JSObjectSetProperty (context, chrome_obj, prop_name, chrome_app_obj, kJSPropertyAttributeNone, &exception);
  JSStringRelease (prop_name);

  chrome_webstore_private_class = JSClassCreate (&chrome_webstore_private_class_def);
  chrome_webstore_private_obj = JSObjectMake (context, chrome_webstore_private_class, context);
  JSObjectSetPrivate (chrome_webstore_private_obj, context);
  prop_name = JSStringCreateWithUTF8CString ("webstorePrivate");
  JSObjectSetProperty (context, chrome_obj, prop_name, chrome_webstore_private_obj, kJSPropertyAttributeNone, &exception);
  JSStringRelease (prop_name);

  chrome_management_class = JSClassCreate (&chrome_management_class_def);
  chrome_management_obj = JSObjectMake (context, chrome_management_class, NULL);
  prop_name = JSStringCreateWithUTF8CString ("management");
  JSObjectSetProperty (context, chrome_obj, prop_name, chrome_management_obj, kJSPropertyAttributeNone, &exception);
  JSStringRelease (prop_name);

  on_installed_string = JSStringCreateWithUTF8CString
    ("return {\n"
     "  _listeners: new Array (),\n"
     "  addListener: function (listener) {\n"
     "    this._listeners.push (listener);\n"
     "  },\n"
     "  removeListener: function (listener) {\n"
     "    var idx = this._listeners.indexOf(listener);\n"
     "    if (idx != -1) this._listeners.splice(idx, 1);\n"
     "  },\n"
     "  installEvent: function (info) {\n"
     "    for (var i = 0; i < this._listeners.length; i++) {\n"
     "      this._listeners[i](info);\n"
     "    }\n"
     "  }\n"
     "}\n");
  on_installed_function = JSObjectMakeFunction (context, NULL, 0, NULL, on_installed_string, NULL, 1, &exception);
  JSStringRelease (on_installed_string);
  on_installed_value = JSObjectCallAsFunction (context, on_installed_function, NULL, 0, NULL, &exception);
  prop_name = JSStringCreateWithUTF8CString ("onInstalled");
  JSObjectSetProperty (context, chrome_management_obj, prop_name, on_installed_value, kJSPropertyAttributeNone, &exception);
  JSStringRelease (prop_name);

  on_uninstalled_string = JSStringCreateWithUTF8CString
    ("return {\n"
     "  _listeners: new Array (),\n"
     "  addListener: function (listener) {\n"
     "    this._listeners.push (listener);\n"
     "  },\n"
     "  removeListener: function (listener) {\n"
     "    var idx = this._listeners.indexOf(listener);\n"
     "    if (idx != -1) this._listeners.splice(idx, 1);\n"
     "  },\n"
     "  installEvent: function (id) {\n"
     "    for (var i = 0; i < this._listeners.length; i++) {\n"
     "      this._listeners[i](id);\n"
     "    }\n"
     "  }\n"
     "}\n");
  on_uninstalled_function = JSObjectMakeFunction (context, NULL, 0, NULL, on_uninstalled_string, NULL, 1, &exception);
  JSStringRelease (on_uninstalled_string);
  on_uninstalled_value = JSObjectCallAsFunction (context, on_uninstalled_function, NULL, 0, NULL, &exception);
  prop_name = JSStringCreateWithUTF8CString ("onUninstalled");
  JSObjectSetProperty (context, chrome_management_obj, prop_name, on_uninstalled_value, kJSPropertyAttributeNone, &exception);
  JSStringRelease (prop_name);

}
