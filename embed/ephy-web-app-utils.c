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
#include <libnotify/notify.h>
#include <stdlib.h>
#include <webkit/webkit.h>

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
  char *destination;

  switch (status)
  {
	case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    destination = g_filename_from_uri (webkit_download_get_destination_uri (download),
                                       NULL, NULL);
    gtk_image_set_from_file (GTK_IMAGE (data->image), destination);
    g_free (destination);
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
  g_free (destination_uri);
  g_free (destination);
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
  EphyWebApplication *app = (EphyWebApplication *) user_data;

  ephy_web_application_launch (app);
  g_object_unref (app);
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

  }

  if (data->callback) {
    if (created) {
      if (!data->callback (response, data->app, data->userdata)) {
      }
    } else {
      data->callback (GTK_RESPONSE_CANCEL, data->app, data->userdata);
    }
  }

  if (response == GTK_RESPONSE_OK) {
    char *message;
    NotifyNotification *notification;

    if (created) {
      message = g_strdup_printf (_("The application '%s' is ready to be used"),
                                 ephy_web_application_get_name (data->app));
    } else {
      message = g_strdup_printf (_("The application '%s' could not be created"),
                                 ephy_web_application_get_name (data->app));
      response = GTK_RESPONSE_CANCEL;
      ephy_web_application_delete (data->app, NULL);
    }

    notification = notify_notification_new (message, NULL, NULL);
    g_free (message);

    if (created) {
      notify_notification_add_action (notification, "launch", _("Launch"),
                                      (NotifyActionCallback)notify_launch_cb,
                                      g_object_ref (data->app),
                                      NULL);
      notify_notification_set_icon_from_pixbuf (notification,
                                                gtk_image_get_pixbuf (GTK_IMAGE (data->image)));
    }

    notify_notification_set_timeout (notification, NOTIFY_EXPIRES_DEFAULT);
    notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
    notify_notification_set_hint (notification, "transient", g_variant_new_boolean (TRUE));
    notify_notification_show (notification, NULL);
  }

  gtk_widget_destroy (GTK_WIDGET (dialog));
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
  GtkWidget *dialog, *hbox, *vbox, *image, *entry, *content_area;
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
  gtk_box_pack_start (GTK_BOX (content_area), hbox, TRUE, TRUE, 0);

  image = gtk_image_new ();
  gtk_misc_set_alignment (GTK_MISC (image), 1.0, 0.0);
  gtk_widget_set_size_request (image, 128, 128);
  gtk_box_pack_start (GTK_BOX (hbox), image, TRUE, TRUE, 0);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

  entry = gtk_entry_new ();
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_container_add (GTK_CONTAINER (vbox), entry);

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

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  g_signal_connect (dialog, "response",
                    G_CALLBACK (dialog_application_install_response_cb),
                    data);
  gtk_widget_show_all (dialog);
}

