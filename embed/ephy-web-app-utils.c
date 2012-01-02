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

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libnotify/notify.h>
#include <libsoup/soup-gnome.h>
#include <webkit/webkit.h>
#include <stdlib.h>

#define EPHY_WEB_APP_DESKTOP_FILE_PREFIX "epiphany-"

/* This is necessary because of gnome-shell's guessing of a .desktop
   filename from WM_CLASS property. */
static char *
get_wm_class_from_app_title (const char *title)
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

/* Gets the proper .desktop filename from a WM_CLASS string,
   converting to the local charset when needed. */
static char *
desktop_filename_from_wm_class (char *wm_class)
{
  char *encoded;
  char *filename = NULL;
  GError *error = NULL;

  encoded = g_filename_from_utf8 (wm_class, -1, NULL, NULL, &error);
  if (error) {
    g_warning ("%s", error->message);
    g_error_free (error);
    return NULL;
  }
  filename = g_strconcat (encoded, ".desktop", NULL);
  g_free (encoded);

  return filename;
}

/**
 * ephy_web_application_get_profile_directory:
 * @name: the application name
 *
 * Gets the directory where the profile for @name is meant to be stored.
 *
 * Returns: (transfer full): A newly allocated string.
 **/
char *
ephy_web_application_get_profile_directory (const char *name)
{
  char *app_dir, *wm_class, *profile_dir, *encoded;
  GError *error = NULL;

  wm_class = get_wm_class_from_app_title (name);
  encoded = g_filename_from_utf8 (wm_class, -1, NULL, NULL, &error);
  g_free (wm_class);

  if (error) {
    g_warning ("%s", error->message);
    g_error_free (error);
    return NULL;
  }

  app_dir = g_strconcat (EPHY_WEB_APP_PREFIX, encoded, NULL);
  profile_dir = g_build_filename (ephy_dot_dir (), app_dir, NULL);
  g_free (encoded);
  g_free (app_dir);

  return profile_dir;
}

/**
 * ephy_web_application_delete:
 * @name: the name of the web application do delete
 * 
 * Deletes all the data associated with a Web Application created by
 * Epiphany.
 * 
 * Returns: %TRUE if the web app was succesfully deleted, %FALSE otherwise
 **/
gboolean
ephy_web_application_delete (const char *name)
{
  char *profile_dir = NULL;
  char *desktop_file = NULL, *desktop_path = NULL;
  char *wm_class;
  GFile *profile = NULL, *launcher = NULL;
  gboolean return_value = FALSE;

  g_return_val_if_fail (name, FALSE);

  profile_dir = ephy_web_application_get_profile_directory (name);
  if (!profile_dir)
    goto out;

  /* If there's no profile dir for this app, it means it does not
   * exist. */
  if (!g_file_test (profile_dir, G_FILE_TEST_IS_DIR)) {
    g_warning ("No application with name '%s' is installed.\n", name);
    goto out;
  }

  profile = g_file_new_for_path (profile_dir);
  if (!ephy_file_delete_dir_recursively (profile, NULL))
    goto out;
  LOG ("Deleted application profile.\n");

  wm_class = get_wm_class_from_app_title (name);
  desktop_file = desktop_filename_from_wm_class (wm_class);
  g_free (wm_class);
  if (!desktop_file)
    goto out;
  desktop_path = g_build_filename (g_get_user_data_dir (), "applications", desktop_file, NULL);
  launcher = g_file_new_for_path (desktop_path);
  if (!g_file_delete (launcher, NULL, NULL))
    goto out;
  LOG ("Deleted application launcher.\n");

  return_value = TRUE;

out:

  if (profile)
    g_object_unref (profile);
  g_free (profile_dir);

  if (launcher)
    g_object_unref (launcher);
  g_free (desktop_file);
  g_free (desktop_path);

  return return_value;
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
  char *filename, *apps_path, *file_path = NULL;
  char *link_path;
  char *wm_class;
  GFile *link;
  GError *error = NULL;

  g_return_val_if_fail (profile_dir, NULL);

  wm_class = get_wm_class_from_app_title (name);

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

    path = g_build_filename (profile_dir, EPHY_WEB_APP_ICON_NAME, NULL);
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
  filename = g_strconcat (wm_class, ".metadata", NULL);
  file_path = g_build_filename (profile_dir, filename, NULL);
  g_key_file_free (metadata_file);

  g_file_set_contents (file_path, data, -1, NULL);
  g_free (file_path);
  g_free (data);

  data = g_key_file_to_data (desktop_file, NULL, NULL);
  filename = g_strconcat (wm_class, ".desktop", NULL);
  g_free (wm_class);
  file_path = g_build_filename (profile_dir, filename, NULL);
  g_key_file_free (desktop_file);

  if (!g_file_set_contents (file_path, data, -1, NULL)) {
    g_free (file_path);
    file_path = NULL;
  }

  /* Create a symlink in XDG_DATA_DIR/applications for the Shell to
   * pick up this application. */
  apps_path = g_build_filename (g_get_user_data_dir (), "applications", NULL);
  if (ephy_ensure_dir_exists (apps_path, &error)) {
    link_path = g_build_filename (apps_path, filename, NULL);
    link = g_file_new_for_path (link_path);
    g_free (link_path);
    g_file_make_symbolic_link (link, file_path, NULL, NULL);
    g_object_unref (link);
  } else {
    g_warning ("Error creating application symlink: %s", error->message);
    g_error_free (error);
  }
  g_free (apps_path);
  g_free (filename);

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
  profile_dir = ephy_web_application_get_profile_directory (name);
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

/**
 * ephy_web_application_get_application_list:
 *
 * Gets a list of the currently installed web applications.
 * Free the returned GList with
 * ephy_web_application_free_application_list.
 *
 * Returns: (transfer-full): a #GList of #EphyWebApplication objects
 **/
GList *
ephy_web_application_get_application_list ()
{
  GFileEnumerator *children = NULL;
  GFileInfo *info;
  GList *applications = NULL;
  GFile *dot_dir;

  dot_dir = g_file_new_for_path (ephy_dot_dir ());
  children = g_file_enumerate_children (dot_dir,
                                        "standard::name",
                                        0, NULL, NULL);
  g_object_unref (dot_dir);

  info = g_file_enumerator_next_file (children, NULL, NULL);
  while (info) {
    EphyWebApplication *app;
    const char *name;
    glong prefix_length = g_utf8_strlen (EPHY_WEB_APP_PREFIX, -1);

    name = g_file_info_get_name (info);
    if (g_str_has_prefix (name, EPHY_WEB_APP_PREFIX)) {
      char *profile_dir;
      guint64 created;
      GDate *date;
      char *metadata_file, *metadata_file_path;
      char *contents;
      GFileInfo *metadata_info;
      GFile *file;

      app = g_slice_new0 (EphyWebApplication);

      profile_dir = g_build_filename (ephy_dot_dir (), name, NULL);
      app->icon_url = g_build_filename (profile_dir, EPHY_WEB_APP_ICON_NAME, NULL);

      metadata_file = g_strconcat (name + prefix_length, ".metadata", NULL);
      metadata_file_path = g_build_filename (profile_dir, metadata_file, NULL);
      if (g_file_get_contents (metadata_file_path, &contents, NULL, NULL)) {
        GKeyFile *key;

        key = g_key_file_new ();
        g_key_file_load_from_data (key, contents, -1, 0, NULL);

        app->name = g_key_file_get_string (key, "Application", "Name", NULL);
        app->origin = g_key_file_get_string (key, "Application", "Origin", NULL);
        app->launch_path = g_key_file_get_string (key, "Application", "LaunchPath", NULL);
        app->description = g_key_file_get_string (key, "Application", "Description", NULL);
        app->profile_dir = g_strdup (profile_dir);

        g_key_file_free (key);

        file = g_file_new_for_path (metadata_file_path);

        /* FIXME: this should use TIME_CREATED but it does not seem to be working. */
        metadata_info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, NULL);
        created = g_file_info_get_attribute_uint64 (metadata_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);

        date = g_date_new ();
        g_date_set_time_t (date, (time_t)created);
        g_date_strftime (app->install_date, 127, "%x", date);

        g_date_free (date);
        g_object_unref (file);
        g_object_unref (metadata_info);

        applications = g_list_append (applications, app);
      }

      g_free (contents);
      g_free (metadata_file);
      g_free (profile_dir);
      g_free (metadata_file_path);

    }

    g_object_unref (info);

    info = g_file_enumerator_next_file (children, NULL, NULL);
  }

  g_object_unref (children);

  return applications;
}

static void
ephy_web_application_free (EphyWebApplication *app)
{
  g_free (app->name);
  g_free (app->icon_url);
  g_free (app->origin);
  g_free (app->launch_path);
  g_free (app->profile_dir);
  g_slice_free (EphyWebApplication, app);
}

/**
 * ephy_web_application_free_application_list:
 * @list: an #EphyWebApplication GList
 *
 * Frees a @list as given by ephy_web_application_get_application_list.
 **/
void
ephy_web_application_free_application_list (GList *list)
{
  GList *p;

  for (p = list; p; p = p->next)
    ephy_web_application_free ((EphyWebApplication*)p->data);

  g_list_free (list);
}

/**
 * ephy_web_application_exists:
 * @name: the potential name of the web application
 *
 * Returns: whether an application with @name exists.
 **/
gboolean
ephy_web_application_exists (const char *name)
{
  char *profile_dir;
  gboolean profile_exists;

  profile_dir = ephy_web_application_get_profile_directory (name);
  profile_exists = g_file_test (profile_dir, G_FILE_TEST_IS_DIR);
  g_free (profile_dir);

  return profile_exists;
}

typedef struct {
  char *address;
  GtkWidget *image;
  GtkWidget *entry;
  GtkWidget *description_label;
  GtkWidget *spinner;
  GtkWidget *box;
  char *icon_href;
  EphyWebApplicationInstallCallback callback;
  gpointer userdata;
} EphyApplicationDialogData;

static void
ephy_application_dialog_data_free (EphyApplicationDialogData *data)
{
  g_free (data->address);
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
  char *profile_dir = NULL;
  char *desktop_file = NULL;
  char *message;
  NotifyNotification *notification;
  gboolean profile_exists;

  if (response == GTK_RESPONSE_OK) {
    profile_dir = ephy_web_application_get_profile_directory (gtk_entry_get_text (GTK_ENTRY (data->entry)));
    profile_exists = g_file_test (profile_dir, G_FILE_TEST_IS_DIR);

    if (profile_exists) {
      if (confirm_web_application_overwrite 
          (GTK_WINDOW (dialog),
           gtk_entry_get_text (GTK_ENTRY (data->entry)))) {
        ephy_web_application_delete (gtk_entry_get_text (GTK_ENTRY (data->entry)));
      } else {
        if (data->callback)
          data->callback (GTK_RESPONSE_CANCEL, NULL, NULL, data->userdata);
        return;
      }
    }

		/* Create Web Application, including a new profile and .desktop file. */
    desktop_file =
      ephy_web_application_create (data->address,
                                   gtk_entry_get_text (GTK_ENTRY (data->entry)),
                                   gtk_label_get_text (GTK_LABEL (data->description_label)),
                                   gtk_image_get_pixbuf (GTK_IMAGE (data->image)));

    if (desktop_file) {
      message = g_strdup_printf (_("The application '%s' is ready to be used"),
                                 gtk_entry_get_text (GTK_ENTRY (data->entry)));
    } else {
      message = g_strdup_printf (_("The application '%s' could not be created"),
                                 gtk_entry_get_text (GTK_ENTRY (data->entry)));
      response = GTK_RESPONSE_CANCEL;
    }

    notification = notify_notification_new (message, NULL, NULL);
    g_free (message);

    if (desktop_file) {
      notify_notification_add_action (notification, "launch", _("Launch"),
                                      (NotifyActionCallback)notify_launch_cb,
                                      g_path_get_basename (desktop_file),
                                      NULL);
      notify_notification_set_icon_from_pixbuf (notification, gtk_image_get_pixbuf (GTK_IMAGE (data->image)));
			g_free (desktop_file);
    }

    notify_notification_set_timeout (notification, NOTIFY_EXPIRES_DEFAULT);
    notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
    notify_notification_set_hint (notification, "transient", g_variant_new_boolean (TRUE));
    notify_notification_show (notification, NULL);
  }

  if (data->callback) {
    if (desktop_file && !data->callback (response,
                                         gtk_entry_get_text (GTK_ENTRY (data->entry)),
                                         profile_dir, data->userdata))
      ephy_web_application_delete (gtk_entry_get_text (GTK_ENTRY (data->entry)));
  }

  g_free (profile_dir);
  ephy_application_dialog_data_free (data);
  gtk_widget_destroy (GTK_WIDGET (dialog));
}


void
ephy_web_application_show_install_dialog (GtkWindow *window,
                                          const char *address,
                                          const char *dialog_title,
                                          const char *install_action,
                                          const char *app_name,
                                          const char *app_description,
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

  label = gtk_label_new (app_description);
  gtk_box_pack_end (GTK_BOX (box), label, FALSE, FALSE, 0);
  
  data = g_slice_new0 (EphyApplicationDialogData);
  data->address = g_strdup (address);
  data->image = image;
  data->entry = entry;
  data->description_label = label;
  data->callback = callback;
  data->userdata = userdata;

  fill_default_application_image (data, icon_href, icon_pixbuf);
  gtk_entry_set_text (GTK_ENTRY (entry), app_name);

  gtk_widget_show_all (dialog);
  if (app_description == NULL)
    gtk_widget_hide (label);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  g_signal_connect (dialog, "response",
                    G_CALLBACK (dialog_application_install_response_cb),
                    data);
  gtk_widget_show_all (dialog);
}

typedef struct {
  char *manifest_path;
  char *install_data;
} MozAppInstallData;
    
static gboolean
mozapp_install_cb (gint response,
                   const char *app_name,
                   const char *profile_dir,
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
      manifest_install_path = g_build_filename (profile_dir, "webapp.manifest", NULL);
      destination_manifest = g_file_new_for_path (manifest_install_path);

      result = g_file_copy (origin_manifest, destination_manifest, 
                            G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
                            NULL, NULL, NULL, 
                            &err);

      g_object_unref (origin_manifest);
      g_object_unref (destination_manifest);
      g_free (manifest_install_path);
    }

    if (result && mozapp_install_data->install_data) {
      char *install_data_path;

      install_data_path = g_build_filename (profile_dir, "webapp.install_data", NULL);

      result = g_file_set_contents (install_data_path, mozapp_install_data->install_data, -1, &err);

      g_free (install_data_path);
    }
  }

  if (!result) {
    if (err) g_error_free (err);
  }

  g_free (mozapp_install_data->manifest_path);
  g_free (mozapp_install_data->install_data);
  g_slice_free (MozAppInstallData, mozapp_install_data);

  return result;
}

void
ephy_web_application_install_manifest (GtkWindow *window,
                                       const char *origin,
                                       const char *manifest_path)
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
    char *name = NULL;
    char *icon_href = NULL;
    char *launch_path = NULL;
    char *full_path = NULL;
    char *description = NULL;

    // TODO : free nodes
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

    node = json_path_query ("$.launch_path", root_node, NULL);
    if (node) {
      if (JSON_NODE_HOLDS_ARRAY (node)) {
        JsonArray *array = json_node_get_array (node);
        if (json_array_get_length (array) > 0) {
          launch_path = g_strdup (json_array_get_string_element (array, 0));
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

    if (launch_path) {
      full_path = g_strconcat (origin, launch_path, NULL);
    } else {
      full_path = g_strdup (origin);
    }

    mozapp_install_data = g_slice_new0 (MozAppInstallData);
    mozapp_install_data->manifest_path = g_strdup (manifest_file_path);
    mozapp_install_data->install_data = NULL;

    ephy_web_application_show_install_dialog (window, full_path,
                                              _("Install web application"), _("Install"),
                                              name, description, icon_href, NULL,
                                              mozapp_install_cb, (gpointer) mozapp_install_data);

    g_free (full_path);
    g_free (name);
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

static JSObjectRef mozapps_app_object_from_origin (JSContextRef context, const char *origin)
{
  JSObjectRef result = NULL;
  char *manifest_path;
  GFile *manifest_file;
  char *manifest_contents = NULL;
  gboolean is_ok = TRUE;
  GError *err = NULL;

  manifest_path = g_build_filename (ephy_dot_dir (), "webapp.manifest", NULL);
  manifest_file = g_file_new_for_path (manifest_path);
  g_free (manifest_path);

  is_ok = g_file_query_exists (manifest_file, NULL);

  if (!is_ok) {
    GList *apps, *node;

    apps = ephy_web_application_get_application_list ();
    for (node = apps; node != NULL; node = g_list_next (node)) {
      EphyWebApplication *app = (EphyWebApplication *) node->data;
      if (g_strcmp0 (app->origin, origin) == 0) {
        manifest_path = g_build_filename (app->profile_dir, "webapp.manifest", NULL);
        manifest_file = g_file_new_for_path (manifest_path);

        is_ok = g_file_query_exists (manifest_file, NULL);
        break;
      }
    }
  }

  if (is_ok) is_ok = g_file_load_contents (manifest_file, NULL, &manifest_contents, NULL, NULL, &err);

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
                         JSValueMakeString (context, JSStringCreateWithUTF8CString (origin)), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         NULL);

    metadata_info = g_file_query_info (manifest_file, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, NULL);
    created = g_file_info_get_attribute_uint64 (metadata_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);

    JSObjectSetProperty (context, result, 
                         JSStringCreateWithUTF8CString ("install_time"),
                         JSValueMakeNumber (context, created), 
                         kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                         NULL);

  }

  g_free (manifest_contents);
  
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
      callback_parameter = mozapps_app_object_from_origin (context, origin);
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

static const JSStaticFunction mozapps_class_staticfuncs[] =
{
{ "amInstalled", mozapps_am_installed, kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontEnum },
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
