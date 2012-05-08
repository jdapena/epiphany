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

#include "ephy-chrome-apps.h"
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

/* JS common code */
static JSValueRef
chrome_app_object_from_application (JSContextRef context, EphyWebApplication *app, JSValueRef *exception)
{
  gboolean is_ok = TRUE;
  JSObjectRef result = NULL;
  char *manifest_path;
  gboolean crx_less = FALSE;
  JsonParser *parser = NULL;

  manifest_path = ephy_chrome_apps_get_manifest_path (app, &crx_less);
  is_ok = (manifest_path != NULL);

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
chrome_app_objects_from_list (JSContextRef context,
			      GList *applications_list,
			      JSValueRef *exception)
{
  int array_count;
  JSValueRef *array_arguments = NULL;
  JSValueRef result;

  array_count = g_list_length (applications_list);

  if (array_count > 0) {
    int i;
    GList *node;

    array_arguments = g_malloc0 (sizeof(JSValueRef *) * array_count);

    for (i = 0, node = applications_list; 
	 node != NULL && *exception == NULL; 
	 i++, node = g_list_next (node)) {
      array_arguments[i] = chrome_app_object_from_application (context,
							       (EphyWebApplication *) node->data,
							       exception);
    }
  }

  if (*exception) {
    result = JSValueMakeNull (context);
  } else {
    result = JSObjectMakeArray (context, array_count, array_arguments, exception);
  }
  g_free (array_arguments);

  return result;
}

/* chrome.app.isInstalled: common method */
static JSValueRef
chrome_app_get_is_installed (JSContextRef context,
                             JSObjectRef object,
                             JSStringRef propertyName,
                             JSValueRef *exception)
{
  bool is_installed = FALSE;

  is_installed = ephy_chrome_apps_is_self_installed ();

  return is_installed?JSValueMakeBoolean (context, TRUE):JSValueMakeUndefined (context);
}

/* chrome.app.getDetails: crx-less API */
static JSValueRef
chrome_app_get_details (JSContextRef context,
                        JSObjectRef function,
                        JSObjectRef thisObject,
                        size_t argumentCount,
                        const JSValueRef arguments[],
                        JSValueRef *exception)
{
  char *manifest_contents = NULL;
  JSValueRef result = NULL;

  if (argumentCount != 0) {
    ephy_js_set_exception (context, exception, _("Invalid arguments."));
    return JSValueMakeNull (context);
  }

  manifest_contents = ephy_chrome_apps_get_self_crx_less_manifest ();
  if (manifest_contents) {
    JSStringRef manifest_string;

    manifest_string = JSStringCreateWithUTF8CString (manifest_contents);
    result = JSValueMakeFromJSONString (context, manifest_string);
    JSStringRelease (manifest_string);

    g_free (manifest_contents);
  }

  return result?result:JSValueMakeUndefined (context);
}

/* chrome.app.install: crx-less API */
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
    GError *error = NULL;

    ephy_chrome_apps_install_crx_less_manifest_from_uri (href, window_href, &error);

    if (error) {
      ephy_js_set_exception (context, exception, error->message);
      g_error_free (error);
    }
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

/* chrome.webstorePrivate.install: webstore only */
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

/* chrome.webstorePrivate.beginInstallWithManifest: webstore only */
typedef struct {
  JSGlobalContextRef context;
  JSObjectRef this_object;
  JSObjectRef callback_function;
  JSObjectRef on_installed;
} ChromeWebstoreInstallData;

static void
chrome_webstore_install_cb (EphyWebApplication *app, GError *error,
			    gpointer userdata)
{
  JSValueRef exception = NULL;
  ChromeWebstoreInstallData *install_data = (ChromeWebstoreInstallData *) userdata;

  if (install_data->callback_function && 
      JSObjectIsFunction (install_data->context, install_data->callback_function)) {
    JSStringRef result_string;
    JSValueRef parameters[1];

    if (error) {
      if (error->domain == EPHY_WEB_APPLICATION_ERROR_QUARK) {
        switch (error->code) {
        case EPHY_WEB_APPLICATION_FORBIDDEN:
          result_string = JSStringCreateWithUTF8CString ("permission_denied"); break;
        case EPHY_WEB_APPLICATION_CANCELLED:
	case EPHY_WEB_APPLICATION_UNSUPPORTED_PERMISSIONS:
	case EPHY_WEB_APPLICATION_CHROME_EXTENSIONS_UNSUPPORTED:
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
  
  if (!error && app && install_data->on_installed) {
    JSValueRef app_object;

    app_object = chrome_app_object_from_application (install_data->context, app, &exception);
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

  g_slice_free (ChromeWebstoreInstallData, install_data);
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
  JSObjectRef callback_function = NULL;
  char *app_id = NULL;
  char *manifest = NULL;
  char *icon_url = NULL;
  char *icon_data = NULL;
  char *localized_name = NULL;
  char *default_locale = NULL;
  ChromeWebstoreInstallData *install_data;


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

    app_id = ephy_js_string_to_utf8 (id_string);
  }

  prop_value = ephy_js_object_get_property (context, details_obj, "manifest", exception);
  if (*exception) goto finish;
  if (JSValueIsString (context, prop_value)) {
    JSStringRef manifest_string;

    manifest_string = JSValueToStringCopy (context, prop_value, exception);
    if (*exception) goto finish;

    manifest = ephy_js_string_to_utf8 (manifest_string);
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

  install_data = g_slice_new0 (ChromeWebstoreInstallData);
  install_data->context = JSGlobalContextCreateInGroup (JSContextGetGroup (context), NULL);
  install_data->on_installed = NULL;
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

  ephy_chrome_apps_install_from_store (app_id, manifest, icon_url, icon_data, localized_name, default_locale, chrome_webstore_install_cb, install_data);


 finish:

  g_free (app_id);
  g_free (manifest);
  g_free (icon_url);
  g_free (icon_data);
  g_free (localized_name);

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
    GList *chrome_apps;

    chrome_apps = ephy_chrome_apps_get_chrome_applications ();
    cb_arguments[0] = chrome_app_objects_from_list (context, chrome_apps, exception);
    ephy_web_application_free_applications_list (chrome_apps);

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
      EphyWebApplication *app;

      id = ephy_js_string_to_utf8 (id_string);
      app = ephy_chrome_apps_get_application_with_id (id);
      if (app) {
	if (!ephy_web_application_delete (app, NULL)) {
	  ephy_js_set_exception (context, exception, _("Failed to delete application."));
	} else {
	  uninstalled = TRUE;
	}
	g_object_unref (app);
      }
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
      EphyWebApplication *app;

      id = ephy_js_string_to_utf8 (id_string);
      app = ephy_chrome_apps_get_application_with_id (id);
      if (app) {
	if (!ephy_web_application_launch (app)) {
	  ephy_js_set_exception (context, exception, _("Failed to launch application."));
	}
	g_object_unref (app);
      }
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
    translation = ephy_chrome_apps_crx_get_translation (contents_path, key_id, ephy_web_application_get_custom_key (app, EPHY_WEB_APPLICATION_CHROME_DEFAULT_LOCALE));
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

typedef struct _ChromePrivateData {
  WebKitWebFrame *frame;
  WebKitWebNavigationReason navigation_reason;
  gdouble load_provisional_time;
  gdouble load_commited_time;
  gdouble load_finished_time;
  gdouble load_first_visually_non_empty_layout_time;
  gdouble request_time;
  gulong load_status_id;
  gulong request_starting_id;
  gulong navigation_requested_id;
} ChromePrivateData;

static JSValueRef
chrome_csi (JSContextRef ctx,
	    JSObjectRef function,
	    JSObjectRef thisObject,
	    size_t argumentCount,
	    const JSValueRef arguments[],
	    JSValueRef *exception)
{
  ChromePrivateData *priv = (ChromePrivateData *) JSObjectGetPrivate (thisObject);
  JSObjectRef result = NULL;
  GTimeVal real_time;
  gdouble time;
  gint tran;

  g_get_current_time (&real_time);
  time = real_time.tv_sec*G_USEC_PER_SEC + real_time.tv_usec;

  result = JSObjectMake (ctx, NULL, NULL);

  ephy_js_object_set_property_from_double (ctx, result, "startE",
					   priv->request_time, exception);
  ephy_js_object_set_property_from_double (ctx, result, "onLoadT",
					   priv->load_finished_time, exception);
  ephy_js_object_set_property_from_double (ctx, result, "pageT",
					   time - priv->request_time, exception);
  switch (priv->navigation_reason) {
  case WEBKIT_WEB_NAVIGATION_REASON_LINK_CLICKED:
  case WEBKIT_WEB_NAVIGATION_REASON_FORM_SUBMITTED:
  case WEBKIT_WEB_NAVIGATION_REASON_FORM_RESUBMITTED:
    tran = 0; /* link */
    break;
  case WEBKIT_WEB_NAVIGATION_REASON_BACK_FORWARD:
    tran = 6; /* back forward */
    break;
  case WEBKIT_WEB_NAVIGATION_REASON_RELOAD:
    tran = 16; /* reload */
    break;
  case WEBKIT_WEB_NAVIGATION_REASON_OTHER:
  default:
    tran = 15; /* other */
  }

  ephy_js_object_set_property_from_uint64 (ctx, result, "tran",
					   tran, exception);

  return result;
}

static JSValueRef
chrome_load_times (JSContextRef ctx,
		   JSObjectRef function,
		   JSObjectRef thisObject,
		   size_t argumentCount,
		   const JSValueRef arguments[],
		   JSValueRef *exception)
{
  ChromePrivateData *priv = (ChromePrivateData *) JSObjectGetPrivate (thisObject);
  JSObjectRef result = NULL;
  GTimeVal real_time;
  gdouble time;
  const char *navigation;

  g_get_current_time (&real_time);
  time = real_time.tv_sec*G_USEC_PER_SEC + real_time.tv_usec;

  result = JSObjectMake (ctx, NULL, NULL);

  ephy_js_object_set_property_from_double (ctx, result, "requestTime",
					   priv->request_time, exception);
  ephy_js_object_set_property_from_double (ctx, result, "startLoadTime",
					   priv->load_provisional_time, exception);
  ephy_js_object_set_property_from_double (ctx, result, "commitLoadTime",
					   priv->load_commited_time, exception);
  ephy_js_object_set_property_from_double (ctx, result, "finishDocumentLoadTime",
					   priv->load_finished_time, exception);
  ephy_js_object_set_property_from_double (ctx, result, "finishLoadTime",
					   priv->load_finished_time, exception);
  ephy_js_object_set_property_from_double (ctx, result, "firstPaintTime",
					   priv->load_first_visually_non_empty_layout_time, exception);
  ephy_js_object_set_property_from_double (ctx, result, "firstPaintAfterLoadTime",
					   priv->load_first_visually_non_empty_layout_time, exception);
  switch (priv->navigation_reason) {
  case WEBKIT_WEB_NAVIGATION_REASON_LINK_CLICKED:
    navigation = "LinkClicked";
    break;
  case WEBKIT_WEB_NAVIGATION_REASON_FORM_SUBMITTED:
    navigation = "FormSubmitted";
    break;
  case WEBKIT_WEB_NAVIGATION_REASON_FORM_RESUBMITTED:
    navigation = "FormResubmitted";
    break;
  case WEBKIT_WEB_NAVIGATION_REASON_BACK_FORWARD:
    navigation = "BackForward";
    break;
  case WEBKIT_WEB_NAVIGATION_REASON_RELOAD:
    navigation = "Reload";
    break;
  case WEBKIT_WEB_NAVIGATION_REASON_OTHER:
    navigation = "Other";
  default:
    navigation = "";
  }
  ephy_js_object_set_property_from_string (ctx, result, "navigationType",
					   navigation, exception);

  ephy_js_object_set_property_from_boolean (ctx, result, "wasFetchedViaSpdy",
					    FALSE, exception);
  ephy_js_object_set_property_from_boolean (ctx, result, "wasNpnNegociated",
					    FALSE, exception);
  ephy_js_object_set_property_from_boolean (ctx, result, "wasAlternateProtocolAvailable",
					    FALSE, exception);

  return result;
}

static void
on_chrome_frame_notify_load_status_cb (GObject    *gobject,
				       GParamSpec *pspec,
				       gpointer    user_data)
{
  ChromePrivateData *priv = (ChromePrivateData *) user_data;
  WebKitLoadStatus status;
  GTimeVal real_time;
  gdouble time;

  g_get_current_time (&real_time);
  time = real_time.tv_sec*G_USEC_PER_SEC + real_time.tv_usec;

  status = webkit_web_frame_get_load_status (priv->frame);

  switch (status) {
  case WEBKIT_LOAD_PROVISIONAL:
    priv->load_provisional_time = time;
    break;
  case WEBKIT_LOAD_COMMITTED:
    priv->load_commited_time = time;
    break;
  case WEBKIT_LOAD_FINISHED:
    priv->load_finished_time = time;
    break;
  case WEBKIT_LOAD_FIRST_VISUALLY_NON_EMPTY_LAYOUT:
    priv->load_first_visually_non_empty_layout_time = time;
    break;
  default:
    break;
  }
}

static void
on_chrome_frame_resource_request_starting_cb (WebKitWebFrame *frame,
					      WebKitWebResource *resource,
					      WebKitNetworkRequest *request,
					      WebKitNetworkResponse *response,
					      gpointer user_data)
{
  ChromePrivateData *priv = (ChromePrivateData *) user_data;
  WebKitWebDataSource *data_source;
  GTimeVal real_time;
  gdouble time;

  data_source = webkit_web_frame_get_data_source (frame);
  if (!data_source || resource != webkit_web_data_source_get_main_resource (data_source))
    return;

  g_get_current_time (&real_time);
  time = real_time.tv_sec*G_USEC_PER_SEC + real_time.tv_usec;
  priv->request_time = time;
}

static void
on_chrome_web_view_navigation_policy_decision_requested_cb (WebKitWebView *web_view,
							    WebKitWebFrame *frame,
							    WebKitNetworkRequest *request,
							    WebKitWebNavigationAction *navigation_action,
							    WebKitWebPolicyDecision *policy_decision,
							    gpointer user_data)
{
  ChromePrivateData *priv = (ChromePrivateData *) user_data;

  priv->navigation_reason = webkit_web_navigation_action_get_reason (navigation_action);
}

static void
on_frame_finalized (gpointer data,
		    GObject *obj)
{
  ChromePrivateData *priv = (ChromePrivateData *) data;

  priv->frame = NULL;
  priv->load_status_id = 0;
  priv->request_starting_id = 0;
  priv->navigation_requested_id = 0;
}		    

static void
chrome_class_finalize (JSObjectRef object)
{
  ChromePrivateData *priv = (ChromePrivateData *) JSObjectGetPrivate (object);

  if (priv->frame) {
    g_signal_handler_disconnect (priv->frame, priv->load_status_id);
    g_signal_handler_disconnect (priv->frame, priv->request_starting_id);
    g_signal_handler_disconnect (webkit_web_frame_get_web_view (priv->frame), priv->navigation_requested_id);
    g_object_weak_unref (G_OBJECT (priv->frame), on_frame_finalized, priv);
    priv->frame = NULL;
  }
}


static const JSStaticFunction chrome_class_staticfuncs[] =
{
{ "csi", chrome_csi, kJSPropertyAttributeNone },
{ "loadTimes", chrome_load_times, kJSPropertyAttributeNone },
{ NULL, NULL, 0 }
};

static const JSClassDefinition chrome_class_def =
{
0,
kJSClassAttributeNone,
"EphyChromeClass",
NULL,

NULL,
chrome_class_staticfuncs,

NULL,
chrome_class_finalize,

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
ephy_chrome_apps_setup_js_api (JSGlobalContextRef context,
			       WebKitWebFrame *frame)
{
  WebKitWebView *web_view;
  JSObjectRef global_obj;
  JSValueRef exception = NULL;

  JSClassRef chrome_class;
  JSObjectRef chrome_obj;
  ChromePrivateData *chrome_private;

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

  web_view = webkit_web_frame_get_web_view (frame);
  global_obj = JSContextGetGlobalObject(context);

  location = ephy_js_context_get_location (context, &exception);

  chrome_class = JSClassCreate (&chrome_class_def);
  chrome_obj = JSObjectMake (context, chrome_class, NULL);
  chrome_private = g_new0 (ChromePrivateData, 1);
  JSObjectSetPrivate (chrome_obj, chrome_private);
  chrome_private->load_status_id = 
    g_signal_connect (frame, "notify::load-status",
		      G_CALLBACK (on_chrome_frame_notify_load_status_cb), chrome_private);
  chrome_private->request_starting_id =
    g_signal_connect (frame, "resource-request-starting",
		      G_CALLBACK (on_chrome_frame_resource_request_starting_cb), chrome_private);
  chrome_private->navigation_requested_id =
    g_signal_connect (web_view, "navigation-policy-decision-requested",
		      G_CALLBACK (on_chrome_web_view_navigation_policy_decision_requested_cb), chrome_private);
  chrome_private->frame = frame;
  g_object_weak_ref (G_OBJECT (frame), on_frame_finalized, chrome_private);
  
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
