/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-open-web-apps.c
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
#include "ephy-open-web-apps.h"

#include "ephy-embed-utils.h"
#include "ephy-file-helpers.h"
#include "ephy-js-utils.h"
#include "ephy-settings.h"
#include "ephy-web-app-utils.h"
#include "ephy-web-application.h"

#include <glib/gi18n.h>
#include <json-glib/json-glib.h>
#include <webkit/webkit.h>

static gboolean
is_open_web_app (EphyWebApplication *app)
{
  char *manifest_path;
  GFile *file;
  gboolean result;

  manifest_path = ephy_web_application_get_settings_file_name (app, 
							       EPHY_WEB_APPLICATION_OPEN_WEB_APPS_MANIFEST);
  file = g_file_new_for_path (manifest_path);
  result = g_file_query_exists (file, NULL);
  g_object_unref (file);
  g_free (manifest_path);

  return result;
}



typedef struct {
  char *origin;
  char *manifest_path;
  char *receipt;
  GError *error;
  EphyOpenWebAppsInstallManifestCallback callback;
  gpointer userdata;
} InstallManifestData;

static void
finish_install_manifest_data (InstallManifestData *install_data)
{
  if (install_data->callback) {
    install_data->callback (install_data->origin, install_data->error, install_data->userdata);
  }
  g_free (install_data->origin);
  g_free (install_data->manifest_path);
  g_free (install_data->receipt);
  if  (install_data->error) {
    g_error_free (install_data->error);
  }
  g_slice_free (InstallManifestData, install_data);
}
    
static gboolean
install_manifest_cb (gint response,
		     EphyWebApplication *app,
		     gpointer userdata)
{
  gboolean result = TRUE;
  GError *err = NULL;
  InstallManifestData *install_manifest_data = (InstallManifestData *) userdata;

  if (response == GTK_RESPONSE_OK) {
    {
      GFile *origin_manifest, *destination_manifest;
      char *manifest_install_path;

      origin_manifest = g_file_new_for_path (install_manifest_data->manifest_path);
      manifest_install_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_OPEN_WEB_APPS_MANIFEST);
      destination_manifest = g_file_new_for_path (manifest_install_path);

      result = g_file_copy (origin_manifest, destination_manifest, 
                            G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS,
                            NULL, NULL, NULL, 
                            &err);

      g_object_unref (origin_manifest);
      g_object_unref (destination_manifest);
      g_free (manifest_install_path);
    }

    if (result && install_manifest_data->receipt) {
      char *receipt_path;

      receipt_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_OPEN_WEB_APPS_RECEIPT);

      result = g_file_set_contents (receipt_path, ephy_embed_utils_strip_bom_mark (install_manifest_data->receipt), -1, &err);

      g_free (receipt_path);
    }

  } else {
    g_set_error (&(install_manifest_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
                 EPHY_WEB_APPLICATION_CANCELLED, _("User cancelled installation."));
  }

  finish_install_manifest_data (install_manifest_data);

  return result;
}

void
ephy_open_web_apps_install_manifest (GtkWindow *window,
				     const char *origin,
				     const char *manifest_path,
				     const char *receipt,
				     const char *install_origin,
				     EphyOpenWebAppsInstallManifestCallback callback,
				     gpointer userdata)
{
  JsonParser *parser;
  GError *error = NULL;
  char *manifest_file_path;
  InstallManifestData *install_manifest_data;

  manifest_file_path = g_filename_from_uri (manifest_path, NULL, &error);
  if (!manifest_file_path) {
    if (callback) {
      g_set_error (&error, EPHY_WEB_APPLICATION_ERROR_QUARK, EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, _("Couldn't open manifest."));
      callback (origin, error, userdata);
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
    GList *allowed_install_origins, *node;
    gboolean allowed = FALSE;

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

    allowed_install_origins = ephy_json_path_query_string_list ("$.installs_allowed_from", root_node);
    allowed_install_origins = g_list_append (allowed_install_origins, g_strdup (origin));
    for (node = allowed_install_origins; !allowed && node != NULL; node = g_list_next (node)) {
      char *str = (char *) node->data;
      allowed = (g_strcmp0 (str, "*") == 0 ||
		 ephy_embed_utils_urls_match_origin (str, install_origin));
    }
    g_list_foreach (allowed_install_origins, (GFunc) g_free, NULL);
    g_list_free (allowed_install_origins);

    if (!allowed) {
      if (callback) {
	g_set_error (&error, EPHY_WEB_APPLICATION_ERROR_QUARK, EPHY_WEB_APPLICATION_FORBIDDEN, _("Application cannot be installed from here."));
	callback (origin, error, userdata);
	g_error_free (error);
      }
      return;
    } else {

      install_manifest_data = g_slice_new0 (InstallManifestData);
      install_manifest_data->origin = g_strdup (origin);
      install_manifest_data->manifest_path = g_strdup (manifest_file_path);
      install_manifest_data->receipt = g_strdup (receipt);
      install_manifest_data->error = NULL;
      install_manifest_data->callback = callback;
      install_manifest_data->userdata = userdata;

      ephy_web_application_set_status (app, EPHY_WEB_APPLICATION_TEMPORARY);

      ephy_web_application_show_install_dialog (window,
						_("Install web application"), _("Install"),
						app, icon_href, NULL,
						install_manifest_cb, (gpointer) install_manifest_data);
    }

    g_object_unref (app);
    g_free (icon_href);
    g_free (manifest_file_path);
    

  } else {
    error->domain = EPHY_WEB_APPLICATION_ERROR_QUARK;
    error->code = EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR;
    if (callback) {
      callback (origin, error, userdata);
      g_error_free (error);
    }
  }

  g_object_unref (parser);
}

typedef struct {
  char *install_origin;
  char *url;
  char *origin;
  char *local_path;
  char *receipt;
  EphyOpenWebAppsInstallManifestFromURICallback callback;
  gpointer userdata;
  GError *error;
} InstallManifestFromURIData;

static void
finish_install_manifest_from_uri_data (InstallManifestFromURIData *install_data)
{

  if (install_data->callback)
    install_data->callback (install_data->origin, install_data->error, install_data->userdata);

  g_free (install_data->origin);
  g_free (install_data->url);
  g_free (install_data->local_path);
  g_free (install_data->receipt);
  g_free (install_data->install_origin);
  if (install_data->error)
    g_error_free (install_data->error);
  g_slice_free (InstallManifestFromURIData, install_data);
}

static void
install_manifest_from_uri_install_manifest_cb (const char *origin, GError *error, gpointer userdata)
{
  InstallManifestFromURIData *install_data = (InstallManifestFromURIData *) userdata;

  if (install_data->error == NULL && error != NULL) {
    install_data->error = g_error_copy (error);
  }

  finish_install_manifest_from_uri_data (install_data);
}

static void
install_manifest_from_uri_download_status_changed_cb (WebKitDownload *download,
						      GParamSpec *spec,
						      InstallManifestFromURIData *install_data)
{
  WebKitDownloadStatus status = webkit_download_get_status (download);

  switch (status) {
  case WEBKIT_DOWNLOAD_STATUS_FINISHED:
    {
      install_data->origin = ephy_embed_utils_url_get_origin (install_data->url);
      ephy_open_web_apps_install_manifest (NULL,
					   install_data->origin,
					   install_data->local_path,
					   install_data->receipt,
					   install_data->install_origin,
					   install_manifest_from_uri_install_manifest_cb,
					   install_data);
      g_object_unref (download);
    }
    break;

  case WEBKIT_DOWNLOAD_STATUS_ERROR:
    g_set_error (&(install_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
		 EPHY_WEB_APPLICATION_NETWORK, _("Network error retrieving manifest."));
    finish_install_manifest_from_uri_data (install_data);
    g_object_unref (download);
    break;
  case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
    g_set_error (&(install_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
		 EPHY_WEB_APPLICATION_CANCELLED, _("Application retrieval cancelled."));
    finish_install_manifest_from_uri_data (install_data);
    g_object_unref (download);
    break;
  default:
    break;
  }
}

void
ephy_open_web_apps_install_manifest_from_uri (const char *url,
					      const char *receipt,
					      const char *install_origin,
					      EphyOpenWebAppsInstallManifestFromURICallback callback,
					      gpointer userdata)
{
  InstallManifestFromURIData* install_data;
  WebKitNetworkRequest *request;
  WebKitDownload *download;
  char *destination, *destination_uri, *tmp_filename;

  install_data = g_slice_new0 (InstallManifestFromURIData);
  install_data->origin = NULL;
  install_data->url = g_strdup (url);
  install_data->receipt = g_strdup (receipt);
  install_data->install_origin = g_strdup (install_origin);
  install_data->callback = callback;
  install_data->userdata = userdata;

  request = webkit_network_request_new (url);
  if (request == NULL) {
    g_set_error (&(install_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
                 EPHY_WEB_APPLICATION_MANIFEST_URL_ERROR, _("Manifest URL is invalid."));

    finish_install_manifest_from_uri_data (install_data);
  }

  download = webkit_download_new (request);
  g_object_unref (request);

  tmp_filename = ephy_file_tmp_filename ("ephy-download-XXXXXX", NULL);
  destination = g_build_filename (ephy_file_tmp_dir (), tmp_filename, NULL);
  destination_uri = g_filename_to_uri (destination, NULL, NULL);
  webkit_download_set_destination_uri (download, destination_uri);
  install_data->local_path = g_strdup (destination_uri);
  g_free (destination);
  g_free (destination_uri);
  g_free (tmp_filename);

  g_signal_connect (G_OBJECT (download), "notify::status",
                    G_CALLBACK (install_manifest_from_uri_download_status_changed_cb), install_data);
  
  webkit_download_start (download);
}

EphyWebApplication *
ephy_open_web_apps_get_application_from_origin (const char *origin)
{
  EphyWebApplication *app;

  app = ephy_web_application_get_self ();
  if (app && !is_open_web_app (app)) {
    g_object_unref  (app);
    app = NULL;
  }
  if (!app) {
    GList *origin_applications, *node;
    origin_applications = ephy_web_application_get_applications_from_origin (origin);
    for (node = origin_applications; node != NULL; node = g_list_next (node)) {
      if (is_open_web_app (EPHY_WEB_APPLICATION (node->data))) {
	app = EPHY_WEB_APPLICATION (node->data);
	g_object_ref (app);
	break;
      }
    }
    ephy_web_application_free_applications_list (origin_applications);
  }

  return app;
}

GList *
ephy_open_web_apps_get_applications_from_install_origin (const char *install_origin)
{
  GList *origin_applications, *node;
  GList *result = NULL;

  origin_applications = ephy_web_application_get_applications_from_install_origin (install_origin);
  for (node = origin_applications; node != NULL; node = g_list_next (node)) {
    EphyWebApplication *app = (EphyWebApplication *) node->data;
    if (is_open_web_app (app)) {
      g_object_ref (app);
      result = g_list_prepend (result, app);
    }
  }
  ephy_web_application_free_applications_list (origin_applications);

  return result;
}

