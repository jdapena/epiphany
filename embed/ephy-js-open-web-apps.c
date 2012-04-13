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
#include "ephy-js-utils.h"
#include "ephy-open-web-apps.h"
#include "ephy-settings.h"
#include "ephy-web-app-utils.h"
#include "ephy-web-application.h"

#include <glib/gi18n.h>

static JSValueRef
mozapps_app_object_from_application (JSContextRef context, EphyWebApplication *app, JSValueRef *exception)
{
  GFile *manifest_file = NULL;
  gboolean is_ok = TRUE;
  char *manifest_contents = NULL;
  JSObjectRef result = NULL;

  if (is_ok) {
    char *manifest_path;
    manifest_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_OPEN_WEB_APPS_MANIFEST);
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
    char *origin;

    result = JSObjectMake (context, NULL, NULL);
    
    
    metadata_info = g_file_query_info (manifest_file, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, NULL);
    is_ok = (metadata_info != NULL);
    created = g_file_info_get_attribute_uint64 (metadata_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);

    ephy_js_object_set_property_from_json (context, result,
                                           "manifest", manifest_contents,
                                           exception);
    origin = g_strdup (ephy_web_application_get_origin (app));
    if  (g_str_has_suffix (origin, "/")) {
      /* The origin in mozApps does not contain the trailing bar */
      char *bar_position = g_strrstr (origin, "/");
      *bar_position = '\0';
    }
    ephy_js_object_set_property_from_string (context, result,
                                             "origin", origin,
                                             exception);
    g_free (origin);
    ephy_js_object_set_property_from_uint64 (context, result,
                                             "install_time", created,
                                             exception);
    is_ok = (*exception == NULL);
    
  }

  if (is_ok) {
    char *receipt_path;
    GFile *receipt_file;
    
    receipt_path = ephy_web_application_get_settings_file_name (app, EPHY_WEB_APPLICATION_OPEN_WEB_APPS_RECEIPT);
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
      EphyWebApplication *app;

      app = ephy_open_web_apps_get_application_from_origin (origin);
      if (app == NULL) {
	callback_parameter = JSValueMakeNull (context);
      } else {
	callback_parameter = mozapps_app_object_from_application (context, app, exception);
	g_object_unref (app);
      }
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
      GList *installed_apps, *node;
      int i = 0;
      int array_count = 0;
      JSValueRef *array_arguments = NULL;

      installed_apps = ephy_open_web_apps_get_applications_from_install_origin (origin);
      if (installed_apps) {
	array_count = g_list_length (installed_apps);
	array_arguments = g_malloc0 (sizeof(JSValueRef *) * g_list_length (installed_apps));
	i = 0;
	for (i = 0, node = installed_apps; node != NULL; i++, node = g_list_next (node)) {
	  EphyWebApplication *app = (EphyWebApplication *) node->data;
	  array_arguments[i] = mozapps_app_object_from_application (context,
								    app,
								    exception);
	  g_object_unref (app);
	}
      }
      callback_parameter = JSObjectMakeArray (context, 
					      array_count, array_arguments, 
					      exception);
      g_free (array_arguments);
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
  JSGlobalContextRef context;
  JSObjectRef thisObject;
  JSObjectRef pendingObject;
  JSValueRef onSuccessCallback;
  JSValueRef onErrorCallback;
  char *origin;
  GError *error;
} MozAppsInstallData;

static JSValueRef
finish_mozapps_install_data (MozAppsInstallData *install_data, JSValueRef *exception)
{
  JSGlobalContextRef context;
  JSValueRef result;
  context = install_data->context;

  if (*exception == NULL && install_data->error == NULL) {
    if (*exception == NULL) {
      EphyWebApplication *app;

      app = ephy_open_web_apps_get_application_from_origin (install_data->origin);
      if (app) {
	JSValueRef appObject;

	appObject = mozapps_app_object_from_application (context, app, exception);
	ephy_js_object_set_property_from_value (context,
						install_data->pendingObject,
						"result",
						appObject,
						exception);
	g_object_unref (app);
      }
    }

    if (*exception == NULL && install_data->onSuccessCallback)
      JSObjectCallAsFunction (context,
			      JSValueToObject (context, install_data->onSuccessCallback, NULL),
			      install_data->thisObject, 0, NULL, exception);

    if (*exception == NULL) {
      JSValueRef onSuccess;

      onSuccess = ephy_js_object_get_property (context,
					       install_data->pendingObject,
					       "onsuccess",
					       exception);
      if (*exception == NULL && JSValueIsObject (context, onSuccess)) {
	JSObjectRef onSuccessObj = JSValueToObject (context, onSuccess, exception);
	if (*exception == NULL && JSObjectIsFunction (context, onSuccessObj))
	  JSObjectCallAsFunction (context, onSuccessObj, install_data->pendingObject, 
				  0, NULL, exception);
      }
    }

  } else if (*exception == NULL && install_data->error != NULL) {
    JSValueRef errorValue[1];
    const char *code = NULL;

    errorValue[0] = JSObjectMakeError (context, 0, 0, exception);

    if (*exception == NULL) {
      if (install_data->error->domain == EPHY_WEB_APPLICATION_ERROR_QUARK) {
        switch (install_data->error->code) {
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
                                               "message", install_data->error->message,
                                               exception);
    }

    if (*exception == NULL) {
      ephy_js_object_set_property_from_value (context,
					      install_data->pendingObject,
					      "error",
					      errorValue[0],
					      exception);
    }

    if (*exception == NULL && install_data->onErrorCallback) {
      JSObjectCallAsFunction (context,
                              JSValueToObject (context, install_data->onErrorCallback, NULL),
                              install_data->thisObject, 1, errorValue, exception);
    }

    if (*exception == NULL) {
      JSValueRef onError;

      onError = ephy_js_object_get_property (context,
					     install_data->pendingObject,
					     "onerror",
					     exception);
      if (*exception == NULL && JSValueIsObject (context, onError)) {
	JSObjectRef onErrorObj = JSValueToObject (context, onError, exception);
	if (*exception == NULL && JSObjectIsFunction (context, onErrorObj))
	  JSObjectCallAsFunction (context, onErrorObj, install_data->pendingObject,
				  0, NULL, exception);
      }
    }
  }

  if (install_data->error)
    g_error_free (install_data->error);

  g_free (install_data->origin);

  if (install_data->thisObject)
    JSValueUnprotect (context, install_data->thisObject);
  if (install_data->onSuccessCallback)
    JSValueUnprotect (context, install_data->onSuccessCallback);
  if (install_data->onErrorCallback)
    JSValueUnprotect (context, install_data->onErrorCallback);
  if (install_data->pendingObject)
    JSValueUnprotect (context, install_data->pendingObject);
  g_slice_free (MozAppsInstallData, install_data);

  result = (*exception != NULL)?JSValueMakeNull(context):JSValueMakeUndefined(context);
  JSGlobalContextRelease (context);
  return result;
}

static void
mozapps_install_install_manifest_from_uri_cb (const char *origin,
					      GError *error,
					      gpointer userdata)
{
  MozAppsInstallData *install_data = (MozAppsInstallData *) userdata;
  JSValueRef exception = NULL;

  if (error) {
    install_data->error = g_error_copy (error);
  }

  finish_mozapps_install_data (install_data, &exception);
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
  char *install_origin = NULL;
  JSStringRef url_str;
  MozAppsInstallData *install_data;

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

  {
    char *location;

    location = ephy_js_context_get_location (context, exception);
    if (location && *exception == NULL) {
      install_origin = ephy_embed_utils_url_get_origin (location);
    }
    g_free (location);
  }

  if (*exception != NULL) {
    g_free (install_origin);
    return JSValueMakeNull (context);
  }

  install_data = g_slice_new0 (MozAppsInstallData);
  install_data->context = JSObjectGetPrivate (thisObject);
  JSGlobalContextRetain (install_data->context);
  install_data->thisObject = thisObject;
  install_data->pendingObject = JSObjectMake (context, NULL, NULL);
  JSValueProtect (context, install_data->pendingObject);
  if (thisObject != NULL)
    JSValueProtect (context, install_data->thisObject);

  if (argumentCount > 2) {
    install_data->onSuccessCallback = arguments[2];
    JSValueProtect (context, install_data->onSuccessCallback);
  }

  if (argumentCount > 3) {
    install_data->onErrorCallback = arguments[3];
    JSValueProtect (context, install_data->onErrorCallback);
  }

  ephy_open_web_apps_install_manifest_from_uri (url, receipt, install_origin,
						mozapps_install_install_manifest_from_uri_cb,
						install_data);

  g_free (url);
  g_free (install_origin);
  g_free (receipt);

  return install_data->pendingObject;
}

typedef struct {
  JSGlobalContextRef context;
  JSObjectRef pendingObject;
} MozAppsGenericData;

static gboolean
finish_mozapps_generic_data (gpointer userdata)
{
  MozAppsGenericData *data = (MozAppsGenericData *) userdata;
  JSGlobalContextRef context = data->context;
  JSObjectRef pendingObject = data->pendingObject;
  JSValueRef error;
  JSValueRef exception = NULL;

  error = ephy_js_object_get_property (context,
				       pendingObject,
				       "error",
				       &exception);

  if (exception == NULL) {
    if (JSValueIsObject (context, error)) {
      JSValueRef onError;

      onError = ephy_js_object_get_property (context,
					     pendingObject,
					     "onerror",
					     &exception);
      if (exception == NULL && JSValueIsObject (context, onError)) {
	JSObjectRef onErrorObj = JSValueToObject (context, onError, &exception);
	if (exception == NULL && JSObjectIsFunction (context, onErrorObj))
	  JSObjectCallAsFunction (context, onErrorObj, pendingObject,
				  0, NULL, &exception);
      }
    } else {
      JSValueRef onSuccess;

      onSuccess = ephy_js_object_get_property (context,
					       pendingObject,
					       "onsuccess",
					       &exception);
      if (exception == NULL && JSValueIsObject (context, onSuccess)) {
	JSObjectRef onSuccessObj = JSValueToObject (context, onSuccess, &exception);
	if (exception == NULL && JSObjectIsFunction (context, onSuccessObj))
	  JSObjectCallAsFunction (context, onSuccessObj, pendingObject,
				  0, NULL, &exception);
      }
    }
  }

  if (data->pendingObject)
    JSValueUnprotect (context, pendingObject);
  if (data->context)
    JSGlobalContextRelease (context);
  g_slice_free (MozAppsGenericData, data);

  return FALSE;
}

static JSValueRef
mozapps_get_self (JSContextRef context,
		  JSObjectRef function,
		  JSObjectRef thisObject,
		  size_t argumentCount,
		  const JSValueRef arguments[],
		  JSValueRef *exception)
{
  MozAppsGenericData *data;
  char *location = NULL;
  char *origin = NULL;

  if (argumentCount != 0) {
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  data = g_slice_new0 (MozAppsGenericData);
  data->context = JSGlobalContextCreateInGroup (JSContextGetGroup (context), NULL);
  data->pendingObject = JSObjectMake (context, NULL, NULL);
  JSValueProtect (context, data->pendingObject);

  location = ephy_js_context_get_location (context, exception);
  if (location == NULL || *exception != NULL) {
    g_free (location);
    ephy_js_set_exception (context, exception, _("Couldn't fetch context location."));
    goto getSelfFinish;
  }

  origin = ephy_embed_utils_url_get_origin (location);

  if (origin == NULL) {
    ephy_js_set_exception (context, exception, _("Couldn't get context origin."));
    goto getSelfFinish;
  } else {
    EphyWebApplication *app;

    app = ephy_open_web_apps_get_application_from_origin (origin);
    if (app != NULL) {
      JSValueRef appObject = mozapps_app_object_from_application (context, app, exception);
      ephy_js_object_set_property_from_value (context,
					      data->pendingObject,
					      "result",
					      appObject,
					      exception);
      g_object_unref (app);
    }
  }

  if (*exception) {
    JSValueRef error = JSObjectMakeError (context, 0, 0, exception);
    JSObjectRef errorObj = JSValueToObject (context, error, NULL);
    ephy_js_object_set_property_from_string (context, errorObj,
					     "code", "getSelfFailed",
					     exception);
    ephy_js_object_set_property_from_string (context, errorObj,
					     "message", "Error in mozApps.getSelf",
					     exception);
  }
  
 getSelfFinish:
  g_free (origin);
  g_free (location);

  g_idle_add ((GSourceFunc) finish_mozapps_generic_data, data);

  return data->pendingObject;
}

static JSValueRef
mozapps_get_installed (JSContextRef context,
		       JSObjectRef function,
		       JSObjectRef thisObject,
		       size_t argumentCount,
		       const JSValueRef arguments[],
		       JSValueRef *exception)
{
  MozAppsGenericData *data;
  char *location = NULL;
  char *origin = NULL;

  if (argumentCount != 0) {
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  data = g_slice_new0 (MozAppsGenericData);
  data->context = JSGlobalContextCreateInGroup (JSContextGetGroup (context), NULL);
  data->pendingObject = JSObjectMake (context, NULL, NULL);
  JSValueProtect (context, data->pendingObject);

  location = ephy_js_context_get_location (context, exception);
  if (location == NULL || *exception != NULL) {
    g_free (location);
    ephy_js_set_exception (context, exception, _("Couldn't fetch context location."));
    goto finish;
  }

  origin = ephy_embed_utils_url_get_origin (location);

  if (origin == NULL) {
    ephy_js_set_exception (context, exception, _("Couldn't get context origin."));
    goto finish;
  } else {
    GList *apps;
    int array_count = 0;
    JSValueRef *array_arguments = NULL;
    JSValueRef result;

    apps = ephy_open_web_apps_get_applications_from_install_origin (origin);
    array_count = g_list_length (apps);
    if (apps != NULL) {
      GList *node;
      int i;

      array_arguments = g_malloc0 (sizeof (JSValueRef *) * array_count);
      for (i = 0, node = apps; node != NULL; i++, node = g_list_next (node)) {
	array_arguments[i] = mozapps_app_object_from_application (context,
								  (EphyWebApplication *) node->data,
								  exception);
      }
      ephy_web_application_free_applications_list (apps);
    }
    result = JSObjectMakeArray (context, array_count, array_arguments, exception);
    g_free (array_arguments);

    ephy_js_object_set_property_from_value (context,
					    data->pendingObject,
					    "result",
					    result,
					    exception);
  }

  if (*exception) {
    JSValueRef error = JSObjectMakeError (context, 0, 0, exception);
    JSObjectRef errorObj = JSValueToObject (context, error, NULL);
    ephy_js_object_set_property_from_string (context, errorObj,
					     "code", "getInstalledFailed",
					     exception);
    ephy_js_object_set_property_from_string (context, errorObj,
					     "message", "Error in mozApps.getInstalled",
					     exception);
  }
  
 finish:
  g_free (origin);
  g_free (location);

  g_idle_add ((GSourceFunc) finish_mozapps_generic_data, data);

  return data->pendingObject;
}

static const JSStaticFunction mozapps_class_staticfuncs[] =
{
{ "amInstalled", mozapps_am_installed, kJSPropertyAttributeNone },
{ "install", mozapps_install, kJSPropertyAttributeNone },
{ "getInstalledBy", mozapps_get_installed_by, kJSPropertyAttributeNone },
{ "getSelf", mozapps_get_self, kJSPropertyAttributeNone },
{ "getInstalled", mozapps_get_installed, kJSPropertyAttributeNone },
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
ephy_open_web_apps_setup_js_api (JSGlobalContextRef context)
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

