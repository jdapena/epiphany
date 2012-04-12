/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-js-open-web-apps.c
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
#include "ephy-js-open-web-apps.h"

#include "ephy-embed-utils.h"
#include "ephy-file-helpers.h"
#include "ephy-js-utils.h"
#include "ephy-settings.h"
#include "ephy-web-app-utils.h"
#include "ephy-web-application.h"

#include <glib/gi18n.h>
#include <json-glib/json-glib.h>

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
    g_set_error (&(mozapp_install_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
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
      g_set_error (&error, EPHY_WEB_APPLICATION_ERROR_QUARK, EPHY_WEB_APPLICATION_MANIFEST_PARSE_ERROR, _("Couldn't open manifest."));
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
    error->domain = EPHY_WEB_APPLICATION_ERROR_QUARK;
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
      if (manifest_data->error->domain == EPHY_WEB_APPLICATION_ERROR_QUARK) {
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
    g_set_error (&(manifest_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
                 EPHY_WEB_APPLICATION_NETWORK, _("Network error retrieving manifest."));
    finish_install_manifest (manifest_data, &exception);
    g_object_unref (download);
    break;
	case WEBKIT_DOWNLOAD_STATUS_CANCELLED:
    g_set_error (&(manifest_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
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
    g_set_error (&(install_manifest_data->error), EPHY_WEB_APPLICATION_ERROR_QUARK,
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

  if (!g_settings_get_boolean (EPHY_SETTINGS_WEB,
                               EPHY_PREFS_WEB_ENABLE_OPEN_WEB_APPS))
    return;

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

