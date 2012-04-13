/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-chrome-apps.c
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
#include "ephy-chrome-apps.h"

#include "ephy-embed-utils.h"
#include "ephy-file-helpers.h"
#include "ephy-js-utils.h"
#include "ephy-web-app-utils.h"

#include <archive.h>
#include <archive_entry.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/tree.h>
#include <stdlib.h>
#include <webkit/webkit.h>

#define DEFAULT_CHROME_WEBSTORE_CRX_UPDATE_PATH "http://clients2.google.com/service/update2/crx"


/* Common implementation:
 *
 * These methods belong to the shared implementation between CRX-less and Webstore
 * Chrome apps.
 */

gboolean
ephy_chrome_apps_is_self_installed ()
{
  EphyWebApplication *app;
  gboolean is_installed = FALSE;

  app = ephy_web_application_get_self ();
  if (app) {
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
    g_object_unref (app);
  }

  return is_installed;
}

char *
ephy_chrome_apps_get_manifest_path (EphyWebApplication *app, gboolean *is_crx_less)
{
  char *manifest_path;
  GFile *manifest_file;
  gboolean found = FALSE;
  gboolean _is_crx_less = FALSE;

  manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_WEBSTORE_MANIFEST);
  manifest_file = g_file_new_for_path (manifest_path);
    
  found = g_file_query_exists (manifest_file, NULL);
  g_object_unref (manifest_file);
  if (!found) {
    g_free (manifest_path);

    manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_MANIFEST);
    manifest_file = g_file_new_for_path (manifest_path);
    
    found = g_file_query_exists (manifest_file, NULL);
    g_object_unref (manifest_file);
    if (found) _is_crx_less = TRUE;
  }

  if (is_crx_less)
    *is_crx_less = _is_crx_less;

  return manifest_path;
}

static gboolean
ephy_chrome_apps_is_chrome_application (EphyWebApplication *app)
{
  char *manifest_path;

  manifest_path = ephy_chrome_apps_get_manifest_path (app, NULL);
  g_free (manifest_path);

  return manifest_path != NULL;
}

GList *
ephy_chrome_apps_get_chrome_applications ()
{
  GList *applications, *node;
  GList *result;

  result = NULL;

  applications = ephy_web_application_get_applications ();
  for (node = applications; node != NULL; node = g_list_next (node)) {
    EphyWebApplication *app = (EphyWebApplication *) node->data;
    if (ephy_chrome_apps_is_chrome_application (app)) {
      result = g_list_append (result, app);
      g_object_ref (app);
    }
  }
  ephy_web_application_free_applications_list (applications);

  return result;
}

EphyWebApplication *
ephy_chrome_apps_get_application_with_id (const char *id)
{
  GList *apps, *node;
  EphyWebApplication *result = NULL;

  apps = ephy_web_application_get_applications ();
  for (node = apps; node != NULL; node = g_list_next (node)) {
    EphyWebApplication *app = (EphyWebApplication *) node->data;
    const char *app_id = ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID);
    if (app_id && g_strcmp0 (id, app_id) == 0) {
      result = g_object_ref  (app);
      break;
    }
  }
  ephy_web_application_free_applications_list (apps);

  return result;
}



/* CRX-less implementation
 * 
 * This part implements methods specific to only CRX-less handling
 */

static gboolean
install_crx_less_manifest_install_dialog_cb (gint response,
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
ephy_chrome_apps_install_crx_less_manifest (const char *origin,
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
                                              install_crx_less_manifest_install_dialog_cb, 
					      (gpointer) manifest_file_path);

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
    ephy_chrome_apps_install_crx_less_manifest (manifest_data->install_origin,
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

void
ephy_chrome_apps_install_crx_less_manifest_from_uri (const char *manifest_uri,
						     const char *window_uri,
						     GError **error)
{
  char *window_origin;
  char *manifest_origin;
  GError *_error = NULL;

  window_origin = ephy_embed_utils_url_get_origin (window_uri);
  manifest_origin = ephy_embed_utils_url_get_origin (manifest_uri);

  if (window_origin && manifest_origin &&
      g_strcmp0 (window_origin, manifest_origin) == 0) {
    WebKitNetworkRequest *request;

    request = webkit_network_request_new (manifest_uri);
    if (request == NULL) {
      g_set_error (&_error, EPHY_WEB_APPLICATION_ERROR_QUARK,
		   EPHY_WEB_APPLICATION_FORBIDDEN, _("Invalid request."));
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
      install_manifest_data->manifest_url = g_strdup (manifest_uri);
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
      g_set_error (&_error, EPHY_WEB_APPLICATION_ERROR_QUARK,
		   EPHY_WEB_APPLICATION_FORBIDDEN,
		   _("Context and manifest origin do not match."));
  }
  g_free (window_origin);
  g_free (manifest_origin);

  if (error) {
    *error = _error;
  } else {
    g_error_free (_error);
  }
}

char *
ephy_chrome_apps_get_self_crx_less_manifest ()
{
  EphyWebApplication *app;
  char *manifest_contents = NULL;

  app = ephy_web_application_get_self ();
  if (app) {
    char *manifest_path;
    GFile *manifest_file;

    manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_CHROME_MANIFEST);
    manifest_file = g_file_new_for_path (manifest_path);
    g_free (manifest_path);
    
    if (g_file_query_exists (manifest_file, NULL))
      g_file_load_contents (manifest_file, NULL, 
			    &manifest_contents, NULL, NULL, NULL);

    g_object_unref (manifest_file);
    g_object_unref (app);
  }

  return manifest_contents;
}

/* Chrome Web Store implementation. CRX package format
 *
 * Implementation of the CRX package format. Extractor and other
 * helpers.
 */

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
ephy_chrome_apps_crx_extract (const char *crx_file_path,
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
                                      ephy_chrome_apps_crx_extract);

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
ephy_chrome_apps_crx_extract_finish (GAsyncResult *result,
				     GError **error)
{
  CRXExtractData *extract_data;
  extract_data = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
  if (extract_data->error)
    *error = g_error_copy (extract_data->error);
  return *error == NULL;
}

static char *
ephy_chrome_apps_crx_extract_msg_id (const char *str)
{
  if (g_str_has_prefix (str, "__MSG_") && g_str_has_suffix (str + 6, "__"))
    return (g_strndup (str + 6, strlen(str) - 8));
  else
    return NULL;
}

char *
ephy_chrome_apps_crx_get_translation (const char *path, const char *key, const char * default_locale)
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
ephy_chrome_apps_crx_parse_manifest (const char *manifest_data,
				     EphyWebApplication *app,
				     char **update_url,
				     char **best_icon_path,
				     GError **error)
{
  JsonParser *parser;
  char *web_url = NULL;
  char *local_path = NULL;
  char *_update_url = NULL;
  char *_best_icon_path = NULL;
  GError *_error = NULL;

  parser = json_parser_new ();

  if (json_parser_load_from_data (parser, ephy_embed_utils_strip_bom_mark (manifest_data), -1, &_error)) {
    JsonNode *root_node;

    root_node = json_parser_get_root (parser);

    {
      char *name;
      name = ephy_json_path_query_string ("$.name", root_node);
      if (name) {
	ephy_web_application_set_name (app, name);
	g_free (name);
      } else {
	g_set_error (&_error, EPHY_WEB_APPLICATION_ERROR_QUARK,
		     EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, _("No name on manifest."));
      }
    }

    if (_error == NULL) {
      web_url = ephy_json_path_query_string ("$.app.launch.web_url", root_node);
      local_path = ephy_json_path_query_string ("$.app.launch.local_path", root_node);
      if (web_url == NULL && local_path == NULL) {
        g_set_error (&_error, EPHY_WEB_APPLICATION_ERROR_QUARK,
                     EPHY_WEB_APPLICATION_CHROME_EXTENSIONS_UNSUPPORTED, _("Currently Epiphany only support installing hosted and packaged apps, not extensions"));
      } else {
	if (local_path && ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID)) {
            char *origin;

            origin = g_strconcat ("chrome-extension://",
				  ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID),
                                  "/",
                                  NULL);
            ephy_web_application_set_origin (app, origin);
            g_free (origin);
            ephy_web_application_set_launch_path (app, local_path);
	} else {
	  GList *url_list;

	  ephy_web_application_set_full_uri (app, web_url);

	  url_list = ephy_json_path_query_string_list ("$.app.urls", root_node);
	  if (url_list && web_url) {
	    char *url_regex;
	    url_regex = build_hosted_apps_url_regex (web_url, url_list);
	    ephy_web_application_set_uri_regex (app, url_regex);
	    g_free (url_regex);
	  }
	  g_list_foreach (url_list, (GFunc) g_free, NULL);
	  g_list_free (url_list);
	}
      }
    }

    if (_error == NULL) {
      char *description;
      char *options_path;
      GList *permissions;

      description = ephy_json_path_query_string ("$.description", root_node);
      ephy_web_application_set_description (app, description);
      g_free (description);

      options_path = ephy_json_path_query_string ("$.options_page", root_node);
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
	g_free (options_path);
      }

      permissions = ephy_json_path_query_string_list ("$.permissions", root_node);
      ephy_web_application_set_permissions (app, permissions);
      g_list_foreach (permissions, (GFunc) g_free, NULL);
      g_list_free (permissions);

      _update_url = ephy_json_path_query_string ("$.update_url", root_node);
      _best_icon_path = ephy_json_path_query_best_icon ("$.icons", root_node);
    }
  }

  g_object_unref (parser);

  g_free (web_url);
  g_free (local_path);

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

/* Chrome Web Store implementation. Install implementation
 *
 * Implementation of the process to install applications from web store or file.
 */

typedef void     (*EphyChromeAppsInstallCrxCallback) (EphyWebApplication *app,
						      GError *error,
						      gpointer userdata);
typedef struct {
  EphyWebApplication *app;
  char *manifest_data;
  char *update_url;
  char *icon_url;
  char *best_icon_path;
  GdkPixbuf *icon_pixbuf;
  char *crx_file_path;
  char *crx_contents_path;
  GError *error;
  EphyChromeAppsInstallCrxCallback callback;
  gpointer userdata;
} EphyChromeAppsInstallCrxData;

static void
finish_chrome_apps_install_crx_data (EphyChromeAppsInstallCrxData *install_data)
{
  if (install_data->callback)
    install_data->callback (install_data->app, install_data->error, install_data->userdata);

  if (install_data->error && install_data->error->domain == EPHY_WEB_APPLICATION_ERROR_QUARK) {
    switch (install_data->error->code) {
    case EPHY_WEB_APPLICATION_CHROME_EXTENSIONS_UNSUPPORTED:
    case EPHY_WEB_APPLICATION_UNSUPPORTED_PERMISSIONS:
      {
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
					 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
					 "%s", install_data->error->message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
      }
      break;
    }
  }

  if (install_data->app) g_object_unref (install_data->app);
  g_free (install_data->crx_file_path);
  g_free (install_data->crx_contents_path);
  g_free (install_data->manifest_data);
  g_free (install_data->update_url);
  g_free (install_data->icon_url);
  g_free (install_data->best_icon_path);
  if (install_data->icon_pixbuf)
    g_object_unref (install_data->icon_pixbuf);
  if (install_data->error) {
    g_error_free (install_data->error);
  }
  g_slice_free (EphyChromeAppsInstallCrxData, install_data);
}

static gboolean 
chrome_apps_install_crx_install_dialog_cb (gint response,
					   EphyWebApplication *app,
					   gpointer userdata)
{
  gboolean result = TRUE;
  EphyChromeAppsInstallCrxData *install_data = (EphyChromeAppsInstallCrxData *) userdata;

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

  finish_chrome_apps_install_crx_data (install_data);

  return result;
}

static void
on_crx_extract (GObject *object,
                GAsyncResult *result,
                gpointer userdata)
{
  GError *error = NULL;
  EphyChromeAppsInstallCrxData *install_data = (EphyChromeAppsInstallCrxData *) userdata;
  gboolean is_ok = TRUE;

  if (ephy_chrome_apps_crx_extract_finish (result, &error)) {
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
        char *best_icon_path = NULL;

        is_ok = ephy_chrome_apps_crx_parse_manifest (install_data->manifest_data, install_data->app, NULL, &best_icon_path, &error);
	install_data->best_icon_path = best_icon_path;
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

      key_id = ephy_chrome_apps_crx_extract_msg_id (ephy_web_application_get_name (EPHY_WEB_APPLICATION (install_data->app)));
      if (key_id != NULL) {
        char *name = ephy_chrome_apps_crx_get_translation (install_data->crx_contents_path, key_id, 
							   ephy_web_application_get_custom_key (install_data->app, EPHY_WEB_APPLICATION_CHROME_DEFAULT_LOCALE));
        ephy_web_application_set_name (install_data->app, name);
        g_free (name);
        g_free (key_id);
      }

      key_id = ephy_chrome_apps_crx_extract_msg_id (ephy_web_application_get_description (EPHY_WEB_APPLICATION (install_data->app)));
      if (key_id != NULL) {
        char *description = ephy_chrome_apps_crx_get_translation (install_data->crx_contents_path, key_id,
								  ephy_web_application_get_custom_key (install_data->app, EPHY_WEB_APPLICATION_CHROME_DEFAULT_LOCALE));
        ephy_web_application_set_description (install_data->app, description);
        g_free (description);
        g_free (key_id);
      }

      ephy_web_application_set_status (install_data->app, EPHY_WEB_APPLICATION_TEMPORARY);

      ephy_web_application_show_install_dialog
        (NULL,
         _("Install Chrome web store application"), _("Install"),
         install_data->app, install_data->icon_url, install_data->icon_pixbuf,
         chrome_apps_install_crx_install_dialog_cb, install_data);
    }
  }

  if (!is_ok) {
    if (error)
      g_propagate_error (&(install_data->error), error);
    finish_chrome_apps_install_crx_data (install_data);
  }
}

static void
crx_download_status_changed_cb (WebKitDownload *download,
                                GParamSpec *spec,
                                EphyChromeAppsInstallCrxData *install_data)
{
  WebKitDownloadStatus status = webkit_download_get_status (download);

  switch (status) {
  case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    {
      install_data->crx_file_path = g_filename_from_uri (webkit_download_get_destination_uri (download), NULL, NULL);
      install_data->crx_contents_path = g_build_filename (ephy_file_tmp_dir (), "ephy-download-XXXXXX", NULL);
      g_mkdtemp (install_data->crx_contents_path);

      ephy_chrome_apps_crx_extract (install_data->crx_file_path, install_data->crx_contents_path,
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
       chrome_apps_install_crx_install_dialog_cb, install_data);
    break;
  default:
    break;
  }
}

static gboolean
chrome_retrieve_crx (char *crx_url, 
                     EphyChromeAppsInstallCrxData *install_data)
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
ephy_chrome_apps_install_crx_from_file (const char *origin,
					const char *crx_path)
{
  EphyChromeAppsInstallCrxData *install_data;

  install_data = g_slice_new0 (EphyChromeAppsInstallCrxData);
  install_data->crx_file_path = g_filename_from_uri (crx_path, NULL, NULL);
  install_data->crx_contents_path = g_build_filename (ephy_file_tmp_dir (), "ephy-download-XXXXXX", NULL);
  g_mkdtemp (install_data->crx_contents_path);
  install_data->app = ephy_web_application_new ();
  ephy_web_application_set_install_origin (install_data->app, origin);
  ephy_chrome_apps_crx_extract (install_data->crx_file_path,
				install_data->crx_contents_path,
				G_PRIORITY_DEFAULT_IDLE,
				NULL,
				on_crx_extract, install_data);
}


static void
crx_update_xml_download_status_changed_cb (WebKitDownload *download,
                                           GParamSpec *spec,
                                           EphyChromeAppsInstallCrxData *install_data)
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
          
          xpath = g_strdup_printf ("/ur:gupdate/ur:app[@appid='%s']/ur:updatecheck/@codebase",
				   ephy_web_application_get_custom_key (install_data->app, EPHY_WEB_APPLICATION_CHROME_ID));
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
       chrome_apps_install_crx_install_dialog_cb, install_data);
    break;
  default:
    break;
  }
}

static gboolean
chrome_retrieve_crx_update_xml (EphyWebApplication *app, 
                                EphyChromeAppsInstallCrxData *install_data)
{
  char *full_uri;
  gboolean result = FALSE;

  {
    SoupURI *xml_uri;
    char *extension_value;

    xml_uri = soup_uri_new (install_data->update_url);
    extension_value = g_strdup_printf ("id=%s&uc",
				       ephy_web_application_get_custom_key (install_data->app, EPHY_WEB_APPLICATION_CHROME_ID));

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

void
ephy_chrome_apps_install_from_store (const char *app_id,
				     const char *manifest,
				     const char *icon_url,
				     const char *icon_data,
				     const char *localized_name,
				     const char *default_locale,
				     EphyChromeAppsInstallCrxCallback callback,
				     gpointer userdata)
{
  EphyWebApplication *app;
  char *update_url = NULL;
  char *best_icon_path = NULL;
  GdkPixbuf *icon_pixbuf = NULL;
  GError *error = NULL;
  char *used_icon_url = NULL;
  EphyChromeAppsInstallCrxData *install_data;

  app = ephy_web_application_new ();
  if (app_id)
    ephy_web_application_set_custom_key (app, EPHY_WEB_APPLICATION_CHROME_ID, app_id);

  ephy_chrome_apps_crx_parse_manifest (manifest, app, &update_url, &best_icon_path, &error);
  if (error == NULL) {
    GList *permissions;

    permissions = ephy_web_application_get_permissions (app);

    if (permissions) {
      const char **p;
      GList *forbidden_permissions_found = NULL;

      for (p = forbidden_permissions; *p != NULL; p++) {
	if (g_list_find_custom (permissions, *p, (GCompareFunc) g_strcmp0))
	  forbidden_permissions_found = g_list_prepend (forbidden_permissions_found, (gpointer) *p);
      }

      if (forbidden_permissions_found) {
	GString *permissions_list;
	GList *node;

	permissions_list = g_string_new (NULL);
	for (node = forbidden_permissions_found; node != NULL; node = g_list_next (node)) {
	  g_string_append (permissions_list, " ");
	  g_string_append (permissions_list, (char *) node->data);
	}

	g_set_error (&error, EPHY_WEB_APPLICATION_ERROR_QUARK,
		     EPHY_WEB_APPLICATION_UNSUPPORTED_PERMISSIONS,
		     _("This application requires these not supported extensions: %s. Epiphany support only a subset of Chrome extensions API."), permissions_list->str);
	g_string_free (permissions_list, TRUE);
	g_list_free (forbidden_permissions_found);
      }
    }
  }

  if (default_locale) {
    ephy_web_application_set_custom_key (app, EPHY_WEB_APPLICATION_CHROME_DEFAULT_LOCALE, default_locale);
  }

  ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_TEMPORARY);

  if (error == NULL) {
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
  }

  install_data = g_slice_new0 (EphyChromeAppsInstallCrxData);
  install_data->callback = callback;
  install_data->userdata = userdata;

  if (error == NULL) {

    install_data->app = g_object_ref (app);
    install_data->update_url = g_strdup (update_url);
    install_data->icon_url = g_strdup (used_icon_url);
    if (icon_pixbuf) install_data->icon_pixbuf = g_object_ref (icon_pixbuf);
    install_data->best_icon_path = g_strdup (best_icon_path);
    if (!chrome_retrieve_crx_update_xml (app, install_data)) {

      ephy_web_application_show_install_dialog (NULL,
                                                _("Install Chrome web store application"), _("Install"),
                                                app, used_icon_url, icon_pixbuf,
                                                chrome_apps_install_crx_install_dialog_cb, install_data);
    }
  } else {
    install_data->error = g_error_copy (error);
    finish_chrome_apps_install_crx_data (install_data);
  }

  if (error) g_error_free (error);
  g_object_unref (app);
  g_free (used_icon_url);
  if (icon_pixbuf) g_object_unref (icon_pixbuf);
}

