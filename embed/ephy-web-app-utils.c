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
#include "ephy-embed-utils.h"
#include "ephy-file-helpers.h"
#include "ephy-js-utils.h"
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

static JSValueRef chrome_app_object_from_application (JSContextRef context, 
                                                      EphyWebApplication *app, 
                                                      const char *filter_id, 
                                                      JSValueRef *exception);



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

      result = g_file_set_contents (receipt_path, ephy_embed_utils_strip_bom_mark (mozapp_install_data->receipt), -1, &err);

      g_free (receipt_path);
    }

  } else {
    g_set_error (&(mozapp_install_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_CANCELLED, _("User cancelled installation."));
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
      g_set_error (&error, ERROR_QUARK, EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, _("Couldn't open manifest."));
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
    EphyWebApplication *app;
    char *icon_href = NULL;
    char *query_result;

    app = ephy_web_application_new ();
    ephy_web_application_set_install_origin (app, install_origin);
    ephy_web_application_set_origin (app, origin);

    // TODO : free nodes
    root_node = json_parser_get_root (parser);

    query_result = ephy_json_path_query_string ("$.name", root_node);
    if (query_result) {
      ephy_web_application_set_name (app, query_result);
      g_free (query_result);
    }

    query_result = ephy_json_path_query_string ("$.description", root_node);
    if (query_result) {
      ephy_web_application_set_description (app, query_result);
      g_free (query_result);
    }

    query_result = ephy_json_path_query_string ("$.developer.name", root_node);
    if (query_result) {
      ephy_web_application_set_author (app, query_result);
      g_free (query_result);
    }

    query_result = ephy_json_path_query_string ("$.developer.url", root_node);
    if (query_result) {
      ephy_web_application_set_author_url (app, query_result);
      g_free (query_result);
    }

    query_result = ephy_json_path_query_string ("$.launch_path", root_node);
    if (query_result) {
      ephy_web_application_set_launch_path (app, query_result);
      g_free (query_result);
    }

    query_result = ephy_json_path_query_best_icon ("$.icons", root_node);
    if (query_result) {
      icon_href = g_strconcat (origin, query_result, NULL);
      g_free (query_result);
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

    ephy_js_object_set_property_from_json (context, result,
                                           "manifest", manifest_contents,
                                           exception);
    ephy_js_object_set_property_from_string (context, result,
                                             "origin", ephy_web_application_get_origin(app),
                                             exception);
    ephy_js_object_set_property_from_uint64 (context, result,
                                             "install_time", created,
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
        ephy_js_object_set_property_from_json (context, result,
                                               "receipt", receipt_contents,
                                               exception);
        is_ok = (*exception == NULL);
      }
    }
    g_object_unref (receipt_file);
  }

  if (is_ok && ephy_web_application_get_install_origin (app) != NULL) {
    ephy_js_object_set_property_from_string (context, result,
                                             "installOrigin", ephy_web_application_get_install_origin (app),
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
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }
  if (!JSValueIsObject (context, arguments[0])) {
    ephy_js_set_exception (context, exception, _("Parameter is not a callback."));
    return JSValueMakeNull (context);
  } else {
    JSObjectRef object_ref;
    char *location;
    char *origin;
    JSValueRef callback_parameter = NULL;
    JSValueRef callback_arguments[1];

    object_ref = JSValueToObject (context, arguments[0], exception);
    if (object_ref == NULL || !JSObjectIsFunction (context, object_ref)) {
      ephy_js_set_exception (context, exception, _("Parameter is not a callback."));
      return JSValueMakeNull (context);
    }

    location = ephy_js_context_get_location (context, exception);
    if (location == NULL || *exception != NULL) {
      g_free (location);
      ephy_js_set_exception (context, exception, _("Couldn't fetch context location."));
      goto amInstalledFinish;
    }

    origin = ephy_embed_utils_url_get_origin (location);
    g_free (location);

    if (origin == NULL) {
      ephy_js_set_exception (context, exception, _("Couldn't get context origin."));
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
  int array_count = 0;
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
  if (*exception) {
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
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull(context);
  }
  if (!JSValueIsObject (context, arguments[0])) {
    ephy_js_set_exception (context, exception, _("Parameter is not a callback."));
    return JSValueMakeNull(context);
  } else {
    JSObjectRef object_ref;
    char *location;
    char *origin;
    JSValueRef callback_parameter = NULL;
    JSValueRef callback_arguments[1];

    object_ref = JSValueToObject (context, arguments[0], exception);
    if (object_ref == NULL || !JSObjectIsFunction (context, object_ref)) {
      ephy_js_set_exception (context, exception, _("Parameter is not a callback."));
      return JSValueMakeNull(context);
    }

    location = ephy_js_context_get_location (context, exception);
    if (location == NULL) {
      ephy_js_set_exception (context, exception, _("Couldn't fetch context location."));
      goto getInstalledByFinish;
    }

    origin = ephy_embed_utils_url_get_origin (location);
    g_free (location);

    if (origin == NULL) {
      ephy_js_set_exception (context, exception, _("Couldn't extract context origin."));
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
    const char *code = NULL;

    errorValue[0] = JSObjectMakeError (context, 0, 0, exception);

    if (*exception == NULL) {
      if (manifest_data->error->domain == ERROR_QUARK) {
        switch (manifest_data->error->code) {
        case EPHY_WEB_APPLICATION_FORBIDDEN:
          code = "permissionDenied"; break;
        case EPHY_WEB_APPLICATION_MANIFEST_URL_ERROR:
          code = "manifestURLError"; break;
        case EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR:
          code = "manifestParseError"; break;
        case EPHY_WEB_APPLICATION_MANIFEST_INVALID:
          code = "invalidManifest"; break;
        case EPHY_WEB_APPLICATION_NETWORK:
          code = "networkError"; break;
        case EPHY_WEB_APPLICATION_CANCELLED:
        default:
          code = "denied"; break;
        }
      } else {
        code = "denied";
      }

      ephy_js_object_set_property_from_string (context,
                                               JSValueToObject (context, errorValue[0], NULL),
                                               "code", code,
                                               exception);
    }

    if (*exception == NULL) {
      ephy_js_object_set_property_from_string (context, JSValueToObject (context, errorValue[0], NULL),
                                               "message", manifest_data->error->message,
                                               exception);
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

  if (manifest_data->thisObject)
    JSValueUnprotect (context, manifest_data->thisObject);
  if (manifest_data->onSuccessCallback)
    JSValueUnprotect (context, manifest_data->onSuccessCallback);
  if (manifest_data->onErrorCallback)
    JSValueUnprotect (context, manifest_data->onErrorCallback);
  g_slice_free (EphyMozAppInstallManifestData, manifest_data);

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

      origin = ephy_embed_utils_url_get_origin (manifest_data->url);
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
                 EPHY_WEB_APPLICATION_NETWORK, _("Network error retrieving manifest."));
    finish_install_manifest (manifest_data, &exception);
    g_object_unref (download);
    break;
	case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
    g_set_error (&(manifest_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_CANCELLED, _("Application retrieval cancelled."));
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
  WebKitNetworkRequest *request;
  WebKitDownload *download;
  EphyMozAppInstallManifestData *install_manifest_data;
	char *destination, *destination_uri, *tmp_filename;

  if (argumentCount < 1 || argumentCount > 4) {
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }
  if (!JSValueIsString (context, arguments[0])) {
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }
  url_str = JSValueToStringCopy (context, arguments[0], exception);
  if (url_str == NULL) {
    ephy_js_set_exception (context, exception, _("URL parameter is not a string."));
    return JSValueMakeNull (context);
  }

  url = ephy_js_string_to_utf8 (url_str);
  JSStringRelease (url_str);

  if (argumentCount > 1) {
    JSStringRef json_str_receipt;

    json_str_receipt = JSValueCreateJSONString (context, arguments[1], 2, exception);
    if (*exception == NULL && json_str_receipt) {
      receipt = ephy_js_string_to_utf8 (json_str_receipt);
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
    char *location;

    location = ephy_js_context_get_location (context, exception);
    if (location && *exception == NULL) {
      install_manifest_data->install_origin = ephy_embed_utils_url_get_origin (location);
    } else {
      install_manifest_data->install_origin = NULL;
    }
    g_free (location);
  }

  if (*exception != NULL) {
    return finish_install_manifest (install_manifest_data, exception);
  }

  request = webkit_network_request_new (url);
  if (request == NULL) {
    /* URL is invalid */
    g_set_error (&(install_manifest_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_MANIFEST_URL_ERROR, _("Manifest URL is invalid."));

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
  JSValueRef navigatorRef;
  JSObjectRef navigatorObj;
  JSValueRef exception = NULL;

  globalObj = JSContextGetGlobalObject(context);
  navigatorRef = ephy_js_object_get_property (context, globalObj, "navigator", &exception);
  navigatorObj = JSValueToObject (context, navigatorRef, &exception);

  mozAppsClassDef = JSClassCreate (&mozapps_class_def);
  mozAppsClassObj = JSObjectMake (context, mozAppsClassDef, context);
  JSObjectSetPrivate (mozAppsClassObj, context);
  ephy_js_object_set_property_from_value (context, navigatorObj, 
                                          "mozApps", mozAppsClassObj, &exception);
  JSClassRelease (mozAppsClassDef);
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
    char *query_result;
    EphyWebApplication *app;
    char *icon_href = NULL;

    app = ephy_web_application_new ();
    ephy_web_application_set_install_origin (app, origin);
    ephy_web_application_set_origin (app, origin);

    // TODO : free nodes
    root_node = json_parser_get_root (parser);
    query_result = ephy_json_path_query_string ("$.name", root_node);
    if (query_result) {
      ephy_web_application_set_name (app, query_result);
      g_free (query_result);
    }

    query_result = ephy_json_path_query_string ("$.description", root_node);
    if (query_result) {
      ephy_web_application_set_description (app, query_result);
      g_free (query_result);
    }

    query_result = ephy_json_path_query_string ("$.launch_url", root_node);
    if (query_result) {
      SoupURI *launch_uri;

      launch_uri = soup_uri_new_with_base (manifest_uri, query_result);
      if (launch_uri) {
        char *launch_path;

        launch_path = soup_uri_to_string (launch_uri, TRUE);
        ephy_web_application_set_launch_path (app, launch_path);
        soup_uri_free (launch_uri);
      }
      g_free (query_result);
    }

    query_result = ephy_json_path_query_best_icon ("$.icons", root_node);
    if (query_result) {
      SoupURI *icon_uri;

      icon_uri = soup_uri_new_with_base (manifest_uri, query_result);
      if (icon_uri) {
        icon_href = soup_uri_to_string (icon_uri, FALSE);
        soup_uri_free (icon_uri);
      }
      g_free (query_result);
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
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  app = ephy_web_application_new();
  if (ephy_web_application_load (app, ephy_dot_dir (), NULL)) {
    char *manifest_path;

    manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_MANIFEST);
    manifest_file = g_file_new_for_path (manifest_path);
    g_free (manifest_path);
    
    is_ok = g_file_query_exists (manifest_file, NULL);

    if (is_ok) {
      is_ok = g_file_load_contents (manifest_file, NULL, 
                                    &manifest_contents, NULL, NULL, NULL);
    }
  } else {
    is_ok = FALSE;
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
  JSValueRef href_value;
  JSStringRef href_string;
  char *window_href = NULL;
  char *href = NULL;

  if (argumentCount != 0) {
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  href_value = ephy_js_context_eval_as_function
    (context,
     "links = document.getElementsByTagName(\"link\");\n"
     "for (var i = 0; i < links.length; i++) {\n"
     "  if (links[i].rel == 'chrome-application-definition' && links[i].href != null) {\n"
     "    return links[i].href;\n"
     "    break;\n"
     "  }\n"
     "}\n"
     "return null;",
     exception);
  if (*exception) return JSValueMakeNull (context);

  if (!JSValueIsString (context, href_value)) {
    return JSValueMakeUndefined (context);
  }
  href_string = JSValueToStringCopy (context, href_value, exception);
  if (*exception) return JSValueMakeNull (context);

  href = ephy_js_string_to_utf8 (href_string);
  JSStringRelease (href_string);

  window_href = ephy_js_context_get_location (context, exception);
  if (*exception == NULL && window_href == NULL) {
    ephy_js_set_exception (context, exception, _("Couldn't retrieve context location."));
  }

  if (window_href && href) {
    char *window_origin;
    char *manifest_origin;

    window_origin = ephy_embed_utils_url_get_origin (window_href);
    manifest_origin = ephy_embed_utils_url_get_origin (href);

    if (window_origin && manifest_origin && g_strcmp0 (window_origin, manifest_origin) == 0) {
      WebKitNetworkRequest *request;

      request = webkit_network_request_new (href);
      if (request == NULL) {
        ephy_js_set_exception (context, exception, _("Invalid request."));
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
      ephy_js_set_exception (context, exception, _("Context and manifest origin do not match."));
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
  char *best_icon_path;
  GdkPixbuf *icon_pixbuf;
  char *crx_file_path;
  char *crx_contents_path;
  GError *error;
  JSGlobalContextRef context;
  JSObjectRef this_object;
  JSObjectRef callback_function;
  JSObjectRef on_installed;
} ChromeWebstoreInstallData;

static void
finish_chrome_webstore_install_data (ChromeWebstoreInstallData *install_data)
{
  JSValueRef exception = NULL;
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
      JSObjectCallAsFunction (install_data->context, install_data->callback_function, install_data->this_object, 1, parameters, &exception);
    }
  }

  if (install_data->on_installed) {
    JSValueRef app_object;

    app_object = chrome_app_object_from_application (install_data->context, install_data->app, NULL, &exception);
    if (app_object && ! JSValueIsNull (install_data->context, app_object)) {
      JSValueRef launch_event_value;

      launch_event_value = ephy_js_object_get_property (install_data->context,
                                                        install_data->on_installed,
                                                        "dispatch", &exception);

      if (launch_event_value && JSValueIsObject (install_data->context, launch_event_value)) {
        JSObjectRef launch_event_function;

        launch_event_function = JSValueToObject (install_data->context, launch_event_value, &exception);
        if (launch_event_function && JSObjectIsFunction (install_data->context, launch_event_function)) {
          JSValueRef callback_arguments[1];

          callback_arguments[0] = app_object;
          JSObjectCallAsFunction (install_data->context, launch_event_function, install_data->on_installed, 1, callback_arguments, &exception);
        }
      }

    }
  }

  if (install_data->callback_function)
    JSValueUnprotect (install_data->context, install_data->callback_function);
  if (install_data->this_object)
    JSValueUnprotect (install_data->context, install_data->this_object);
  if (install_data->context)
    JSGlobalContextRelease (install_data->context);

  g_object_unref (install_data->app);
  g_free (install_data->crx_file_path);
  g_free (install_data->crx_contents_path);
  g_free (install_data->manifest_data);
  g_free (install_data->update_url);
  g_free (install_data->icon_url);
  g_free (install_data->id);
  g_free (install_data->default_locale);
  g_free (install_data->best_icon_path);
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
      g_set_error (&error, ERROR_QUARK, EPHY_WEB_APPLICATION_CRX_EXTRACT_FAILED, _("CRX header not found."));
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
        archive_set_error (zip_archive, ARCHIVE_OK, _("No error."));
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
                 _("Couldn't create destination folder."));
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
                                    ephy_embed_utils_strip_bom_mark (install_data->manifest_data),
                                    -1,
                                    &(install_data->error));

      g_free (manifest_install_path);

      if (result) {
        char *crx_path;
        GFile *tmp_crx_file, *crx_file;

        tmp_crx_file = g_file_new_for_path (install_data->crx_file_path);
        crx_path = ephy_web_application_get_settings_file_name (install_data->app, EPHY_WEB_APPLICATION_CHROME_CRX);
        crx_file = g_file_new_for_path (crx_path);
        g_free (crx_path);

        result = g_file_copy (tmp_crx_file, crx_file,
                              G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
                              NULL, NULL, NULL, 
                              &(install_data->error));
        g_object_unref (tmp_crx_file);
        g_object_unref (crx_file);
      }

      if (result) {
        char *crx_contents_path;
        GFile *tmp_crx_contents_file, *crx_contents_file;

        tmp_crx_contents_file = g_file_new_for_path (install_data->crx_contents_path);
        crx_contents_path = ephy_web_application_get_settings_file_name (install_data->app, EPHY_WEB_APPLICATION_CHROME_CRX_CONTENTS);
        crx_contents_file = g_file_new_for_path (crx_contents_path);
        g_free (crx_contents_path);

        result = g_file_move (tmp_crx_contents_file, crx_contents_file,
                              G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
                              NULL, NULL, NULL, &(install_data->error));
        g_object_unref (crx_contents_file);
        g_object_unref (tmp_crx_contents_file);
      }

    }

  } else {
    g_set_error (&(install_data->error), ERROR_QUARK,
                 EPHY_WEB_APPLICATION_CANCELLED, _("User cancelled installation."));
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

        root_node = json_parser_get_root (parser);
        result = ephy_json_path_query_string (key_json_path, root_node);
      }
      g_object_unref (parser);
    }
    g_object_unref (file);
    g_free (file_path);
    if (*lang == NULL || result != NULL)
      break;
  }
  g_free (key_json_path);

  return result;
}

static gboolean
parse_crx_manifest (const char *manifest_data,
                    char **name,
                    char **web_url,
                    char **description,
                    char **update_url,
                    char **best_icon_path,
                    GError **error)
{
  JsonParser *parser;
  char *_name = NULL;
  char *_web_url = NULL;
  char *_description = NULL;
  char *_update_url = NULL;
  char *_best_icon_path = NULL;
  GError *_error = NULL;

  parser = json_parser_new ();

  if (json_parser_load_from_data (parser, ephy_embed_utils_strip_bom_mark (manifest_data), -1, error)) {
    JsonNode *root_node;

    root_node = json_parser_get_root (parser);
    _name = ephy_json_path_query_string ("$.name", root_node);
    if (_name == NULL)
      g_set_error (&_error, ERROR_QUARK,
                   EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, _("No name on manifest."));

    if (_error == NULL) {
      _web_url = ephy_json_path_query_string ("$.app.launch.web_url", root_node);
      if (_web_url == NULL)
        g_set_error (&_error, ERROR_QUARK,
                     EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, _("No web url on manifest."));
    }
      
    if (_error == NULL) {
      _description = ephy_json_path_query_string ("$.description", root_node);
      _update_url = ephy_json_path_query_string ("$.update_url", root_node);
      _best_icon_path = ephy_json_path_query_best_icon ("$.icons", root_node);
    }
  }

  g_object_unref (parser);

  if (name)
    *name = _name;
  else
    g_free (_name);

  if (description)
    *description = _description;
  else
    g_free (_description);
  
  if (web_url)
    *web_url = _web_url;
  else
    g_free (_web_url);

  if (update_url)
    *update_url = _update_url;
  else
    g_free (_update_url);

  if (best_icon_path)
    *best_icon_path = _best_icon_path;
  else
    g_free (_best_icon_path);

  if (error)
    *error = _error;
  else if (_error)
    g_error_free (_error);

  return _error == NULL;
}

static void
on_crx_extract (GObject *object,
                GAsyncResult *result,
                gpointer userdata)
{
  GError *error = NULL;
  ChromeWebstoreInstallData *install_data = (ChromeWebstoreInstallData *) userdata;
  gboolean is_ok = TRUE;

  if (crx_extract_finish (result, &error)) {
    char *key_id;

    if (install_data->manifest_data == NULL) {
      /* We're opening directly the CRX so we don't have the manifest.
       * Load it and parse */
      char *manifest_path;
      GFile *manifest_file;

      manifest_path = g_build_filename (install_data->crx_contents_path, "manifest.json", NULL);
      manifest_file = g_file_new_for_path (manifest_path);

      is_ok = g_file_load_contents (manifest_file, NULL, &(install_data->manifest_data), NULL, NULL, &error);
      g_object_unref (manifest_file);
      g_free (manifest_path);

      if (is_ok) {
        char *name = NULL;
        char *description = NULL;
        char *web_url = NULL;
        char *best_icon_path = NULL;
        is_ok = parse_crx_manifest (install_data->manifest_data, &name, &web_url, &description, NULL, &best_icon_path, &error);
        if (is_ok) {
          ephy_web_application_set_name (install_data->app, name);
          ephy_web_application_set_description (install_data->app, description);
          ephy_web_application_set_full_uri (install_data->app, web_url);
          install_data->best_icon_path = best_icon_path;
        }
        g_free (name);
        g_free (description);
        g_free (web_url);
      }
    }

    if (is_ok && install_data->best_icon_path && install_data->icon_url == NULL && install_data->icon_pixbuf == NULL) {
      char *best_icon_full_path;

      best_icon_full_path = g_build_filename (install_data->crx_contents_path,
                                              install_data->best_icon_path,
                                              NULL);
      install_data->icon_pixbuf = gdk_pixbuf_new_from_file (best_icon_full_path, NULL);
      g_free (best_icon_full_path);
    }

    if (is_ok) {

      key_id = crx_extract_msg_id (ephy_web_application_get_name (EPHY_WEB_APPLICATION (install_data->app)));
      if (key_id != NULL) {
        char *name = crx_get_translation (install_data->crx_contents_path, key_id, install_data->default_locale);
        ephy_web_application_set_name (install_data->app, name);
        g_free (name);
        g_free (key_id);
      }

      key_id = crx_extract_msg_id (ephy_web_application_get_description (EPHY_WEB_APPLICATION (install_data->app)));
      if (key_id != NULL) {
        char *description = crx_get_translation (install_data->crx_contents_path, key_id, install_data->default_locale);
        ephy_web_application_set_description (install_data->app, description);
        g_free (description);
        g_free (key_id);
      }

      ephy_web_application_set_status (install_data->app, EPHY_WEB_APPLICATION_TEMPORARY);

      ephy_web_application_show_install_dialog
        (NULL,
         _("Install Chrome web store application"), _("Install"),
         install_data->app, install_data->icon_url, install_data->icon_pixbuf,
         chrome_webstore_install_cb, install_data);
    }
  }

  if (!is_ok) {
    if (error)
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
      install_data->crx_file_path = g_filename_from_uri (webkit_download_get_destination_uri (download), NULL, NULL);
      install_data->crx_contents_path = g_build_filename (ephy_file_tmp_dir (), "ephy-download-XXXXXX", NULL);
      g_mkdtemp (install_data->crx_contents_path);

      crx_extract (install_data->crx_file_path, install_data->crx_contents_path,
                   G_PRIORITY_DEFAULT_IDLE, NULL,
                   on_crx_extract, install_data);

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

void
ephy_web_application_install_crx_extension (const char *origin,
                                            const char *crx_path)
{
  ChromeWebstoreInstallData *install_data;

  install_data = g_slice_new0 (ChromeWebstoreInstallData);
  install_data->crx_file_path = g_filename_from_uri (crx_path, NULL, NULL);
  install_data->crx_contents_path = g_build_filename (ephy_file_tmp_dir (), "ephy-download-XXXXXX", NULL);
  g_mkdtemp (install_data->crx_contents_path);
  install_data->app = ephy_web_application_new ();
  ephy_web_application_set_install_origin (install_data->app, origin);
  crx_extract (install_data->crx_file_path,
               install_data->crx_contents_path,
               G_PRIORITY_DEFAULT_IDLE,
               NULL,
               on_crx_extract, install_data);
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
  char *best_icon_path = NULL;
  JSObjectRef callback_function = NULL;

  if (argumentCount > 2 || 
      !JSValueIsObject (context, arguments[0])) {
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  } else if (argumentCount == 2) {
    callback_function = JSValueToObject (context, arguments[1], exception);
    if (*exception) return JSValueMakeNull (context);
  }

  details_obj = JSValueToObject (context, arguments[0], exception);
  if (*exception) return JSValueMakeNull (context);

  prop_value = ephy_js_object_get_property (context, details_obj, "id", exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef id_string;
    id_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    id = ephy_js_string_to_utf8 (id_string);
  }

  prop_value = ephy_js_object_get_property (context, details_obj, "manifest", exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef manifest_string;

    manifest_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception == NULL) {
      manifest = ephy_js_string_to_utf8 (manifest_string);

      parse_crx_manifest (manifest, &name, &web_url, &description, &update_url, &best_icon_path, NULL);
    }
  }

  prop_value = ephy_js_object_get_property (context, details_obj, "iconUrl", exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef icon_url_string;
    icon_url_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    icon_url = ephy_js_string_to_utf8 (icon_url_string);
  }

  prop_value = ephy_js_object_get_property (context, details_obj, "iconData", exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef icon_data_string;
    icon_data_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    icon_data = ephy_js_string_to_utf8 (icon_data_string);
  }

  prop_value = ephy_js_object_get_property (context, details_obj, "localizedName", exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef localized_name_string;
    localized_name_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    localized_name = ephy_js_string_to_utf8 (localized_name_string);
  }

  prop_value = ephy_js_object_get_property (context, details_obj, "default_locale", exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef default_locale_string;
    default_locale_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;
    default_locale = ephy_js_string_to_utf8 (default_locale_string);
  }

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
    if (id) {
      ephy_web_application_set_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID, id);
    }
    install_data->error = NULL;
    install_data->manifest_data = g_strdup (manifest);
    install_data->update_url = update_url?g_strdup(update_url):DEFAULT_CHROME_WEBSTORE_CRX_UPDATE_PATH;
    install_data->icon_url = g_strdup (used_icon_url);
    install_data->icon_pixbuf = icon_pixbuf?g_object_ref (icon_pixbuf):NULL;
    install_data->context = JSGlobalContextCreateInGroup (JSContextGetGroup (context), NULL);
    install_data->on_installed = NULL;
    install_data->best_icon_path = g_strdup (best_icon_path);
    {
      JSStringRef script_ref;
      JSValueRef on_installed_value;

      script_ref = JSStringCreateWithUTF8CString ("chrome.management.onInstalled");
      on_installed_value = JSEvaluateScript (context, script_ref, NULL, NULL, 0, NULL);
      JSStringRelease (script_ref);
      if (on_installed_value) {
        JSValueProtect (context, on_installed_value);
        install_data->on_installed = JSValueToObject (context, on_installed_value, NULL);
      }
    }

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

    if (manifest && !web_url) {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
                                       GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                       _("Currently Epiphany only support installing hosted apps, not extensions nor packages apps"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      result_string = JSStringCreateWithUTF8CString ("user_cancelled");
      
    } else {
      result_string = JSStringCreateWithUTF8CString ("manifest_error");
    }
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
  g_free (best_icon_path);
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
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
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
  JSValueRef script_result_value;
  gboolean result = FALSE;

  if (argumentCount != 1 || !JSValueIsObject(context, arguments[0])) {
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  callback_function = JSValueToObject (context, arguments[0], exception);
  if (!*exception && !JSObjectIsFunction (context, callback_function)) {
    ephy_js_set_exception (context, exception, _("Parameter is not a callback."));
  }
  if (*exception) return JSValueMakeNull (context);

  script_result_value = ephy_js_context_eval_as_function
    (context,
     "var canvas = document.createElement('canvas');"
     "return !!(window.WebGLRenderingContext && (canvas.getContext('webgl') || canvas.getContext('experimental-webgl')));",
     exception);
  if (*exception) return JSValueMakeNull (context);
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
chrome_app_object_from_application (JSContextRef context, EphyWebApplication *app, const char *filter_id, JSValueRef *exception)
{
  GFile *manifest_file = NULL;
  gboolean is_ok = TRUE;
  JSObjectRef result = NULL;
  char *manifest_path;
  gboolean crx_less = FALSE;
  JsonParser *parser = NULL;

  manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_WEBSTORE_MANIFEST);
  manifest_file = g_file_new_for_path (manifest_path);
    
  is_ok = g_file_query_exists (manifest_file, NULL);
  g_object_unref (manifest_file);
  if (!is_ok) {
    g_free (manifest_path);

    manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_MANIFEST);
    manifest_file = g_file_new_for_path (manifest_path);
    
    is_ok = g_file_query_exists (manifest_file, NULL);
    g_object_unref (manifest_file);
    if (is_ok) crx_less = TRUE;
  }
  if (is_ok) {
    parser = json_parser_new ();
    is_ok = json_parser_load_from_file (parser, manifest_path, NULL);
  }

  if (is_ok) {
    result = JSObjectMake (context, NULL, NULL);
    
    {
      const char *id;
      id = ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID);
      is_ok = crx_less || id;
      if (is_ok && id) {
        ephy_js_object_set_property_from_string (context, result,
                                                 "id", id, exception);
        is_ok = (*exception == NULL);
      }
    }

    is_ok = is_ok && ephy_web_application_get_name (app);
    if (is_ok) {
      ephy_js_object_set_property_from_string (context, result,
                                               "name", ephy_web_application_get_name (app),
                                               exception);
      is_ok = (*exception == NULL);
    }

    if (is_ok && ephy_web_application_get_description (app)) {
      ephy_js_object_set_property_from_string (context, result,
                                               "description", ephy_web_application_get_description (app),
                                               exception);
      is_ok = (*exception == NULL);
    }
    
    if (is_ok) {
      JsonNode *root_node;
      char *query_result;

      is_ok = crx_less;
      root_node = json_parser_get_root (parser);

      query_result = ephy_json_path_query_string ("$.version", root_node);
      is_ok = crx_less || query_result;
      if (query_result) {
        ephy_js_object_set_property_from_string (context, result,
                                                 "version", query_result,
                                                 exception);
        if (is_ok && query_result) {
          is_ok = (*exception == NULL);
        }
        g_free (query_result);
      }
    }
    
    if (is_ok) {
      ephy_js_object_set_property_from_boolean (context, result,
                                                "mayDisabled", TRUE, exception);
      is_ok = (*exception == NULL);
    }

    if (is_ok) {
      ephy_js_object_set_property_from_boolean (context, result,
                                                "enabled", TRUE, exception);
      is_ok = (*exception == NULL);
    }

    if (is_ok) {
      ephy_js_object_set_property_from_boolean (context, result,
                                                "isApp", TRUE, exception);
      is_ok = (*exception == NULL);
    }

    if (is_ok) {
      char *full_uri;
      
      full_uri = ephy_web_application_get_full_uri (app);
      if (full_uri) {
        ephy_js_object_set_property_from_string (context, result,
                                                 "appLaunchUrl", full_uri,
                                                 exception);
        g_free (full_uri);
        is_ok = (*exception == NULL);
      }
    }
  }
  g_free (manifest_path);
  if (parser)
    g_object_unref (parser);

  if (*exception || !is_ok) {
    return JSValueMakeNull (context);
  } else {
    return result;
  }
}

static JSValueRef
chrome_app_objects_from_id (JSContextRef context,
                            const char * id,
                            JSValueRef *exception)
{
  GList *applications, *node;
  GList *js_objects_list = NULL;
  int array_count;
  JSValueRef *array_arguments = NULL;

  applications = ephy_web_application_get_applications ();
  for (node = applications; node != NULL; node = g_list_next (node)) {
    EphyWebApplication *app = (EphyWebApplication *) node->data;
    JSValueRef app_object;

    app_object = chrome_app_object_from_application (context, app, id, exception);
    if (app_object != NULL && ! JSValueIsNull (context, app_object)) {
      js_objects_list = g_list_append (js_objects_list, JSValueToObject (context, app_object, exception));
    }
    if (*exception) {
      break;
    }
  }
  ephy_web_application_free_applications_list (applications);

  array_count = g_list_length (js_objects_list);
  if (array_count > 0) {
    int i = 0;
    array_arguments = g_malloc0 (sizeof(JSValueRef *) * array_count);
    for (node = js_objects_list; node != NULL; node = g_list_next (node)) {
      array_arguments[i] = (JSValueRef) node->data;
      i++;
    }
  }
  if (*exception) {
    return JSValueMakeNull (context);
  } else {
    return JSObjectMakeArray (context, array_count, array_arguments, exception);
  }
}

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
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  if (argumentCount == 1) {
    callback_function = JSValueToObject (context, arguments[0], exception);
    if (!*exception && !JSObjectIsFunction (context, callback_function)) {
      ephy_js_set_exception (context, exception, _("Parameter is not a callback."));
    }
  }
  if (*exception) return JSValueMakeNull (context);

  {
    JSValueRef cb_arguments[1];

    cb_arguments[0] = chrome_app_objects_from_id (context, NULL, exception);

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
  JSObjectRef callback_function = NULL;
  gboolean uninstalled = FALSE;
  JSStringRef id_string = NULL;

  if (argumentCount > 2 || (argumentCount == 0) || !JSValueIsString(context, arguments[0])){
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  if (argumentCount == 2) {
    callback_function = JSValueToObject (context, arguments[0], exception);
    if (!*exception && !JSObjectIsFunction (context, callback_function)) {
      ephy_js_set_exception (context, exception, _("Callback parameter is not a function."));
    }
  }
  if (*exception) return JSValueMakeNull (context);

  {
    id_string = JSValueToStringCopy (context, arguments[0], exception);
    if (id_string) {
      char *id;
      GList *apps, *node;

      id = ephy_js_string_to_utf8 (id_string);
      apps = ephy_web_application_get_applications ();
      for (node = apps; node != NULL; node = g_list_next (node)) {
        EphyWebApplication *app = (EphyWebApplication *) node->data;
        const char *app_id = ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID);
        if (app_id && g_strcmp0 (id, app_id) == 0) {
          if (!ephy_web_application_delete (app, NULL)) {
            ephy_js_set_exception (context, exception, _("Failed to delete application."));
          } else {
            uninstalled = TRUE;
          }
          break;
        }
      }
      ephy_web_application_free_applications_list (apps);
    }
  }

  if (callback_function) {
    JSObjectCallAsFunction (context, callback_function, NULL, 0, NULL, exception);
  }

  if (uninstalled) {
    JSStringRef on_uninstalled_string;
    JSValueRef on_uninstalled_value;

    on_uninstalled_string = JSStringCreateWithUTF8CString ("chrome.management.onUninstalled");
    on_uninstalled_value = JSEvaluateScript (context, on_uninstalled_string, NULL, NULL, 0, NULL);
    JSStringRelease (on_uninstalled_string);
    if (on_uninstalled_value && JSValueIsObject (context, on_uninstalled_value)) {
      JSObjectRef on_uninstalled;

      on_uninstalled = JSValueToObject (context, on_uninstalled_value, exception);

      if (on_uninstalled) {
        JSValueRef launch_event_value;
        
        launch_event_value = ephy_js_object_get_property (context, on_uninstalled, "dispatch", exception);
        if (launch_event_value && JSValueIsObject (context, launch_event_value)) {
          JSObjectRef launch_event_function;

          launch_event_function = JSValueToObject (context, launch_event_value, exception);
          if (launch_event_function && JSObjectIsFunction (context, launch_event_function)) {
            JSValueRef callback_arguments[1];

            callback_arguments[0] = JSValueMakeString (context, id_string);
            JSObjectCallAsFunction (context, launch_event_function, on_uninstalled, 1, callback_arguments, exception);
          }
        }
      }
    }
  }
  if (id_string) JSStringRelease (id_string);
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
  JSObjectRef callback_function = NULL;

  if (argumentCount > 2 || (argumentCount == 0) || !JSValueIsString(context, arguments[0])){
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  if (argumentCount == 2) {
    callback_function = JSValueToObject (context, arguments[0], exception);
    if (!*exception && !JSObjectIsFunction (context, callback_function)) {
      ephy_js_set_exception (context, exception, _("Callback parameter is not a function."));
    }
  }
  if (*exception) return JSValueMakeNull (context);

  {
    JSStringRef id_string;

    id_string = JSValueToStringCopy (context, arguments[0], exception);
    if (id_string) {
      char *id;
      GList *apps, *node;

      id = ephy_js_string_to_utf8 (id_string);
      apps = ephy_web_application_get_applications ();
      for (node = apps; node != NULL; node = g_list_next (node)) {
        EphyWebApplication *app = (EphyWebApplication *) node->data;
        const char *app_id = ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID);
        if (app_id && g_strcmp0 (id, app_id) == 0) {
          if (!ephy_web_application_launch (app)) {
            ephy_js_set_exception (context, exception, _("Failed to launch application."));
          }
          break;
        }
      }
      ephy_web_application_free_applications_list (apps);
    }
  }

  if (callback_function) {
    JSObjectCallAsFunction (context, callback_function, NULL, 0, NULL, exception);
  }

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
  JSValueRef exception = NULL;

  JSObjectRef chrome_obj;

  JSClassRef chrome_app_class;
  JSObjectRef chrome_app_obj;

  JSClassRef chrome_webstore_private_class;
  JSObjectRef chrome_webstore_private_obj;

  JSClassRef chrome_management_class;
  JSObjectRef chrome_management_obj;

  const char *event_definition_script =
    "return {\n"
    "  _listeners: new Array (),\n"
    "  addListener: function (listener) {\n"
    "    this._listeners.push (listener);\n"
    "  },\n"
    "  removeListener: function (listener) {\n"
    "    var idx = this._listeners.indexOf(listener);\n"
    "    if (idx != -1) this._listeners.splice(idx, 1);\n"
    "  },\n"
    "  dispatch: function () {\n"
    "    for (var i in this._listeners) {\n"
    "      this._listeners[i].apply(this, arguments);\n"
    "    }\n"
    "  }\n"
    "}\n";
  JSValueRef on_installed_value;
  JSValueRef on_uninstalled_value;

  global_obj = JSContextGetGlobalObject(context);

  chrome_obj = JSObjectMake (context, NULL, NULL);
  ephy_js_object_set_property_from_value (context, global_obj,
                                          "chrome", chrome_obj, &exception);

  chrome_app_class = JSClassCreate (&chrome_app_class_def);
  chrome_app_obj = JSObjectMake (context, chrome_app_class, NULL);
  ephy_js_object_set_property_from_value (context, chrome_obj,
                                          "app", chrome_app_obj, &exception);

  /* Currently we don't support permissions management for applications.
   * The permissions needed for Chrome Webstore are webstorePrivate and management.
   * First we'll only check origin for this. In the future extensions management
   * should check app permissions.
   */

  if (ephy_js_context_in_origin (context, "https://chrome.google.com", &exception)) {

    /* only accessible from webstore */
    chrome_webstore_private_class = JSClassCreate (&chrome_webstore_private_class_def);
    chrome_webstore_private_obj = JSObjectMake (context, chrome_webstore_private_class, context);
    JSObjectSetPrivate (chrome_webstore_private_obj, context);
    ephy_js_object_set_property_from_value (context, chrome_obj,
                                            "webstorePrivate", chrome_webstore_private_obj,
                                            &exception);

    /* only accessible from webstore */
    chrome_management_class = JSClassCreate (&chrome_management_class_def);
    chrome_management_obj = JSObjectMake (context, chrome_management_class, NULL);
    ephy_js_object_set_property_from_value (context, chrome_obj,
                                            "management", chrome_management_obj,
                                            &exception);

    on_installed_value = ephy_js_context_eval_as_function (context,
                                                           event_definition_script,
                                                           &exception);
    ephy_js_object_set_property_from_value (context, chrome_management_obj,
                                            "onInstalled", on_installed_value,
                                            &exception);
    
    on_uninstalled_value = ephy_js_context_eval_as_function (context,
                                                             event_definition_script,
                                                             &exception);
    ephy_js_object_set_property_from_value (context, chrome_management_obj,
                                            "onUninstalled", on_uninstalled_value,
                                            &exception);
  }
}
