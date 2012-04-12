/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-js-chrome-apps.c
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
#include "ephy-js-chrome-apps.h"

#include "ephy-embed-utils.h"
#include "ephy-file-helpers.h"
#include "ephy-js-utils.h"
#include "ephy-settings.h"
#include "ephy-web-application.h"
#include "ephy-web-app-utils.h"

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup-gnome.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/tree.h>
#include <stdlib.h>
#include <webkit/webkit.h>

#define DEFAULT_CHROME_WEBSTORE_CRX_UPDATE_PATH "http://clients2.google.com/service/update2/crx"

static JSValueRef chrome_app_object_from_application (JSContextRef context, 
                                                      EphyWebApplication *app, 
                                                      const char *filter_id, 
                                                      JSValueRef *exception);
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
      if (install_data->error->domain == EPHY_WEB_APPLICATION_ERROR_QUARK) {
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
      g_set_error (&error, EPHY_WEB_APPLICATION_ERROR_QUARK, EPHY_WEB_APPLICATION_CRX_EXTRACT_FAILED, _("CRX header not found."));
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
    g_set_error (&(extract_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK, 
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

        result = ephy_file_move_dir_recursively (tmp_crx_contents_file, crx_contents_file,
                                                 &(install_data->error));
        g_object_unref (crx_contents_file);
        g_object_unref (tmp_crx_contents_file);
      }

    }

  } else {
    g_set_error (&(install_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
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

static gchar *
build_hosted_apps_url_regex (const char *web_url, GList *urls)
{
  GString *buffer;
  gchar *pattern;
  GList *node;

  buffer = g_string_new (NULL);
  pattern = g_regex_escape_string (web_url, -1);
  g_string_append (buffer, "(");
  g_string_append (buffer, pattern);
  g_string_append (buffer, ")");

  for (node = urls; node != NULL; node = g_list_next (node)) {
    SoupURI *uri;
    const gchar *scheme_pattern;
    gchar *host_pattern;

    if (g_str_has_prefix ((char *) node->data, "*://")) {
      char *uri_string;

      uri_string = g_strconcat ("ephy-match://", ((char *) node->data)+4, NULL);
      uri = soup_uri_new (uri_string);
      g_free (uri_string);
    } else {
      uri = soup_uri_new ((char *) node->data);
    }
    if (!uri->scheme)
      continue;
    if (g_strcmp0 (uri->scheme, "ephy-match") == 0) {
      scheme_pattern = "(http|https)";
    } else if (g_strcmp0 (uri->scheme, "http") == 0 || g_strcmp0 (uri->scheme, "https") == 0) {
      scheme_pattern = uri->scheme;
    } else {
      continue;
    }

    if (!uri->host)
      continue;
    if (g_str_has_prefix (uri->host, "*.")) {
      gchar *host_escaped;
      host_escaped = g_regex_escape_string ((uri->host)+2, -1);
      host_pattern = g_strdup_printf ("[a-zA-Z0-9\\-\\.]*%s", host_escaped);
      g_free (host_escaped);
    } else {
      host_pattern = g_regex_escape_string (uri->host, -1);
    }
    g_string_append (buffer, "|(");
    g_string_append (buffer, scheme_pattern);
    g_string_append (buffer, "\\:\\/\\/");
    g_string_append (buffer, host_pattern);
    if (uri->path) {
      gchar *path_escaped;

      path_escaped = g_regex_escape_string (uri->path, -1);
      g_string_append (buffer, path_escaped);
      g_free (path_escaped);
    } else {
      g_string_append (buffer, "\\/");
    }
    g_string_append (buffer, ".*)");
    g_free (host_pattern);
    soup_uri_free (uri);
  }
  
  return g_string_free (buffer, FALSE);
  
}

static gboolean
parse_crx_manifest (const char *manifest_data,
                    char **name,
                    char **web_url,
                    char **url_regex,
                    char **local_path,
                    char **options_path,
                    char **description,
                    char **update_url,
                    GList **permissions,
                    char **best_icon_path,
                    GError **error)
{
  JsonParser *parser;
  char *_name = NULL;
  char *_web_url = NULL;
  char *_url_regex = NULL;
  char *_local_path = NULL;
  char *_options_path = NULL;
  char *_description = NULL;
  char *_update_url = NULL;
  char *_best_icon_path = NULL;
  GList *_permissions = NULL;
  GError *_error = NULL;

  parser = json_parser_new ();

  if (json_parser_load_from_data (parser, ephy_embed_utils_strip_bom_mark (manifest_data), -1, error)) {
    JsonNode *root_node;

    root_node = json_parser_get_root (parser);
    _name = ephy_json_path_query_string ("$.name", root_node);
    if (_name == NULL)
      g_set_error (&_error, EPHY_WEB_APPLICATION_ERROR_QUARK,
                   EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, _("No name on manifest."));

    if (_error == NULL) {
      _web_url = ephy_json_path_query_string ("$.app.launch.web_url", root_node);
      _local_path = ephy_json_path_query_string ("$.app.launch.local_path", root_node);
      if (_web_url == NULL && _local_path == NULL)
        g_set_error (&_error, EPHY_WEB_APPLICATION_ERROR_QUARK,
                     EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, _("No launch url or path on manifest."));
    }

    if (_error == NULL) {
      GList *url_list;

      _options_path = ephy_json_path_query_string ("$.options_page", root_node);
      _description = ephy_json_path_query_string ("$.description", root_node);
      _update_url = ephy_json_path_query_string ("$.update_url", root_node);
      _best_icon_path = ephy_json_path_query_best_icon ("$.icons", root_node);
      url_list = ephy_json_path_query_string_list ("$.app.urls", root_node);
      if (url_list && _web_url) {
        _url_regex = build_hosted_apps_url_regex (_web_url, url_list);
      }
      _permissions = ephy_json_path_query_string_list ("$.permissions", root_node);
      g_list_foreach (url_list, (GFunc) g_free, NULL);
      g_list_free (url_list);
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

  if (url_regex)
    *url_regex = _url_regex;
  else
    g_free (_url_regex);

  if (local_path)
    *local_path = _local_path;
  else
    g_free (_local_path);

  if (options_path)
    *options_path = _options_path;
  else
    g_free (_options_path);

  if (update_url)
    *update_url = _update_url;
  else
    g_free (_update_url);

  if (permissions) {
    *permissions = _permissions;
  } else {
    g_list_foreach (_permissions, (GFunc) g_free, NULL);
    g_list_free (_permissions);
  }

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
        char *url_regex = NULL;
        char *local_path = NULL;
        char *options_path = NULL;
        char *best_icon_path = NULL;
        GList *permissions = NULL;
        is_ok = parse_crx_manifest (install_data->manifest_data, &name, &web_url, &url_regex, &local_path, &options_path, &description, NULL, &permissions, &best_icon_path, &error);
        if (is_ok) {
          ephy_web_application_set_name (install_data->app, name);
          ephy_web_application_set_description (install_data->app, description);
          if (local_path && 
              ephy_web_application_get_custom_key (install_data->app,
                                                   EPHY_WEB_APPLICATION_CHROME_ID)) {
            char *origin;

            origin = g_strconcat ("chrome-extension://",
                                  ephy_web_application_get_custom_key (install_data->app,
                                                                       EPHY_WEB_APPLICATION_CHROME_ID),
                                  "/",
                                  NULL);
            ephy_web_application_set_origin (install_data->app, origin);
            g_free (origin);
            ephy_web_application_set_launch_path (install_data->app, local_path);
          } else {
            ephy_web_application_set_full_uri (install_data->app, web_url);
            if (url_regex) {
              ephy_web_application_set_uri_regex (install_data->app, url_regex);
            }
          }
          if (options_path) {
            if (web_url) {
              SoupURI *base_uri, *options_uri;
              char *path;

              base_uri = soup_uri_new (web_url);
              options_uri = soup_uri_new_with_base (base_uri, options_path);

              path = soup_uri_to_string (options_uri, TRUE);

              if (soup_uri_get_fragment (options_uri)) {
                char *old_path = path;

                path = g_strconcat (path, "#", soup_uri_get_fragment (options_uri), NULL);
                g_free (old_path);
              }

              ephy_web_application_set_options_path (install_data->app, g_str_has_prefix (path, "/")?path+1:path);

              g_free (path);
              soup_uri_free (options_uri);
              soup_uri_free (base_uri);
            } else {
              ephy_web_application_set_options_path (install_data->app, options_path);
            }
          }
          ephy_web_application_set_permissions (install_data->app, permissions);
          install_data->best_icon_path = best_icon_path;
        }
        g_list_foreach (permissions, (GFunc) g_free, NULL);
        g_list_free (permissions);
        g_free (name);
        g_free (description);
        g_free (web_url);
        g_free (url_regex);
        g_free (local_path);
        g_free (options_path);
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
ephy_chrome_apps_install_crx_extension (const char *origin,
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

static const char *forbidden_permissions[] = {
  "background",
  "bookmarks",
  "chrome://favicon/",
  "contentSettings",
  "contextMenus",
  "cookies",
  "experimental",
  "fileBrowserHandler",
  "history",
  "idle",
  "management",
  "privacy",
  "proxy",
  "tabs",
  "tts",
  "ttsEngine",
  "webNavigation",
  "webRequest",
  "webRequestBlocking",
  NULL
};

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
  char *url_regex = NULL;
  char *local_path = NULL;
  char *options_path = NULL;
  char *update_url = NULL;
  GList *permissions = NULL;
  char *best_icon_path = NULL;
  JSObjectRef callback_function = NULL;
  GList *forbidden_permissions_found = NULL;

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

      parse_crx_manifest (manifest, &name, &web_url, &url_regex, &local_path, &options_path, &description, &update_url, &permissions, &best_icon_path, NULL);

      if (permissions) {
        const char **p;

        for (p = forbidden_permissions; *p != NULL; p++) {
          if (g_list_find_custom (permissions, *p, (GCompareFunc) g_strcmp0))
            forbidden_permissions_found = g_list_prepend (forbidden_permissions_found, (gpointer) *p);
        }
      }
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

  if (name && manifest && (web_url || (local_path && id)) && (forbidden_permissions_found == NULL)) {
    EphyWebApplication *app;
    char *used_icon_url = NULL;
    GdkPixbuf *icon_pixbuf = NULL;
    ChromeWebstoreInstallData *install_data;

    app = ephy_web_application_new ();

    ephy_web_application_set_name (app, localized_name?localized_name:name);
    if (description) ephy_web_application_set_description (app, description);
    if (local_path && id) {
      char *origin;

      origin = g_strconcat ("chrome-extension://",
                            id,
                            "/",
                            NULL);
      ephy_web_application_set_origin (app, origin);
      g_free (origin);
      ephy_web_application_set_launch_path (app, local_path);
    } else {
      ephy_web_application_set_full_uri (app, web_url);
      if (url_regex)
        ephy_web_application_set_uri_regex (app, url_regex);
    }
    if (options_path) {
      if (web_url) {
        SoupURI *base_uri, *options_uri;
        char *path;

        base_uri = soup_uri_new (web_url);
        options_uri = soup_uri_new_with_base (base_uri, options_path);

        path = soup_uri_to_string (options_uri, TRUE);

        if (soup_uri_get_fragment (options_uri)) {
          char *old_path = path;
          
          path = g_strconcat (path, "#", soup_uri_get_fragment (options_uri), NULL);
          g_free (old_path);
        }

        ephy_web_application_set_options_path (app, g_str_has_prefix (path, "/")?path+1:path);

        g_free (path);
        soup_uri_free (options_uri);
        soup_uri_free (base_uri);
      } else {
        ephy_web_application_set_options_path (app, options_path);
      }
    }

    if (permissions) {
      ephy_web_application_set_permissions (app, permissions);
    }

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
    if (default_locale) {
      ephy_web_application_set_custom_key (app, EPHY_WEB_APPLICATION_CHROME_DEFAULT_LOCALE, default_locale);
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

    if (manifest && !web_url && !local_path) {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
                                       GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                       _("Currently Epiphany only support installing hosted and packaged apps, not extensions"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      result_string = JSStringCreateWithUTF8CString ("user_cancelled");
      
    } else if (forbidden_permissions_found != NULL) {
      GtkWidget *dialog;
      GList *node;
      GString *permissions_list;

      permissions_list = g_string_new (NULL);
      for (node = forbidden_permissions_found; node != NULL; node = g_list_next (node)) {
        g_string_append (permissions_list, " ");
        g_string_append (permissions_list, (char *) node->data);
      }

      dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
                                       GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                       _("This application requires these not supported extensions: %s. Epiphany support only a subset of Chrome extensions API."), permissions_list->str);
      g_string_free (permissions_list, TRUE);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      result_string = JSStringCreateWithUTF8CString ("user_cancelled");
      
    } else {
      result_string = JSStringCreateWithUTF8CString ("manifest_error");
    }
    g_list_free (forbidden_permissions_found);
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
  g_free (url_regex);
  g_free (local_path);
  g_free (options_path);
  g_free (best_icon_path);
  g_list_foreach (permissions, (GFunc) g_free, NULL);
  g_list_free (permissions);
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
    if (JSValueIsObject (context, arguments[1])) {
      callback_function = JSValueToObject (context, arguments[1], exception);
      
      if (!*exception && !JSObjectIsFunction (context, callback_function)) {
        callback_function = NULL;
      }
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

static JSValueRef
chrome_i18n_get_message (JSContextRef context,
                         JSObjectRef function,
                         JSObjectRef thisObject,
                         size_t argumentCount,
                         const JSValueRef arguments[],
                         JSValueRef *exception)
{
  EphyWebApplication *app;
  char *key_id;
  char **substitutions = NULL;
  char *translation = NULL;

  app = ephy_web_application_new ();
  if (!ephy_web_application_load (app, ephy_dot_dir (), NULL)) {
    g_object_unref (app);
    return JSValueMakeNull (context);
  }
  
  if (argumentCount > 2 || (argumentCount == 0) || !JSValueIsString(context, arguments[0])){
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  } else {
    JSStringRef key_id_string;

    key_id_string = JSValueToStringCopy (context, arguments[0], exception);
    key_id = ephy_js_string_to_utf8 (key_id_string);
    JSStringRelease (key_id_string);
  }
  if (*exception) return JSValueMakeNull (context);

  if (argumentCount == 2) {
    if (JSValueIsString (context, arguments[1])) {
      JSStringRef js_string;
      
      js_string = JSValueToStringCopy (context, arguments[1], exception);
      substitutions = g_new0 (char *, 2);
      substitutions[0] = ephy_js_string_to_utf8 (js_string);
      substitutions[1] = NULL;
      JSStringRelease (js_string);
    } else if (JSValueIsObject (context, arguments[1])) {
      JSObjectRef array_object;
      JSValueRef length_value;
      array_object = JSValueToObject (context, arguments[1], exception);
      length_value = ephy_js_object_get_property (context, array_object, "length", exception);
      if (length_value && JSValueIsNumber (context, length_value)) {
        int i, length;
        
        length = JSValueToNumber (context, length_value, exception);
        substitutions = g_new0 (char *, length + 1);
        substitutions[length] = NULL;
        for (i = 0; i < length; i++) {
          JSValueRef i_value;
          substitutions[i] = NULL;
          
          i_value = JSObjectGetPropertyAtIndex (context, array_object, i, exception);
          if (JSValueIsString (context, i_value)) {
            JSStringRef i_str;
            
            i_str = JSValueToStringCopy (context, i_value, exception);
            substitutions[i] = ephy_js_string_to_utf8 (i_str);
            JSStringRelease (i_str);
          }
        }
      }
    }
  }
  if (*exception) return JSValueMakeNull (context);

  if (key_id) {
    char *contents_path;

    contents_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_CRX_CONTENTS);
    translation = crx_get_translation (contents_path, key_id, ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_DEFAULT_LOCALE));
    g_free (contents_path);
  }

  g_object_unref (app);
  {
    JSStringRef result_string;
    JSValueRef result_value;
    
    result_string = JSStringCreateWithUTF8CString (translation?translation:key_id);
    result_value = JSValueMakeString (context, result_string);
    JSStringRelease (result_string);

    return result_value;
  }

}

static const JSStaticFunction chrome_i18n_class_staticfuncs[] =
{
{ "getMessage", chrome_i18n_get_message, kJSPropertyAttributeNone },
/* { "getAcceptedLanguages", chrome_i18n_get_accepted_languages, kJSPropertyAttributeNone }, */
{ NULL, NULL, 0 }
};

static const JSClassDefinition chrome_i18n_class_def =
{
0,
kJSClassAttributeNone,
"EphyChromeI18nClass",
NULL,

NULL,
chrome_i18n_class_staticfuncs,

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
ephy_chrome_apps_setup_js_api (JSGlobalContextRef context)
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

  char *location;

  if (!g_settings_get_boolean (EPHY_SETTINGS_WEB,
                               EPHY_PREFS_WEB_ENABLE_CHROME_APPS))
    return;

  global_obj = JSContextGetGlobalObject(context);

  location = ephy_js_context_get_location (context, &exception);

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

  if (ephy_js_context_in_origin (context, "https://chrome.google.com/", &exception)) {

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

  if (g_str_has_prefix (location, "chrome-extension://")) {
    JSClassRef chrome_i18n_class;
    JSObjectRef chrome_i18n_obj;

    chrome_i18n_class = JSClassCreate (&chrome_i18n_class_def);
    chrome_i18n_obj = JSObjectMake (context, chrome_i18n_class, context);
    ephy_js_object_set_property_from_value (context, chrome_obj,
                                            "i18n", chrome_i18n_obj,
                                            &exception);
  }

}
