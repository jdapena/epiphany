/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-js-utils.c
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
#include "ephy-js-utils.h"

#include "ephy-embed-utils.h"

#include <stdlib.h>

char *
ephy_js_string_to_utf8 (JSStringRef js_string)
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

char *
ephy_js_context_get_location (JSContextRef context, JSValueRef *exception)
{
  JSStringRef location_script;
  JSValueRef href_value;
  char *result = NULL;
  
  location_script = JSStringCreateWithUTF8CString ("window.location.href");
  href_value = JSEvaluateScript (context, location_script, NULL, NULL, 0, exception);
  JSStringRelease (location_script);

  if (JSValueIsString (context, href_value)) {
    JSStringRef href_string;

    href_string = JSValueToStringCopy (context, href_value, exception);
    if (href_string) {
      result = ephy_js_string_to_utf8 (href_string);
      JSStringRelease (href_string);
    }
  }

  return result;
}

gboolean
ephy_js_context_in_origin (JSContextRef context,
                           const char *origin,
                           JSValueRef *exception)
{
  gboolean result = FALSE;
  char *location;

  location = ephy_js_context_get_location (context, exception);
  if (location) {
    
    char *location_origin;

    location_origin = ephy_embed_utils_url_get_origin (location);
    result = (g_strcmp0 (origin, location_origin) == 0);

    g_free (location_origin);
    g_free (location);
  }

  return result;
}

JSValueRef
ephy_js_context_eval_as_function (JSContextRef context,
                                  const char *script,
                                  JSValueRef *exception)
{
  JSStringRef script_string;
  JSObjectRef function_obj;
  JSValueRef result;

  result = JSValueMakeNull (context);

  script_string = JSStringCreateWithUTF8CString (script);
  function_obj = JSObjectMakeFunction (context, NULL, 0, NULL,
                                       script_string, NULL, 1, exception);
  if (*exception == NULL) {
    result = JSObjectCallAsFunction (context, function_obj, NULL, 0, NULL, exception);
  }
  JSStringRelease (script_string);

  return result;
}

JSValueRef
ephy_js_object_get_property (JSContextRef context,
                             JSObjectRef obj,
                             const char *name,
                             JSValueRef *exception)
{
  JSStringRef name_string;
  JSValueRef result;

  name_string = JSStringCreateWithUTF8CString (name);
  result = JSObjectGetProperty (context, obj, name_string, exception);
  JSStringRelease (name_string);

  return result;
}

void
ephy_js_object_set_property_from_string (JSContextRef context,
                                         JSObjectRef obj,
                                         const char *name,
                                         const char *value,
                                         JSValueRef *exception)
{
  JSStringRef name_string;
  JSStringRef value_string;

  name_string = JSStringCreateWithUTF8CString (name);
  value_string = JSStringCreateWithUTF8CString (value);
  JSObjectSetProperty (context, obj, 
                       name_string, JSValueMakeString (context, value_string),
                       kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                       exception);
  JSStringRelease (name_string);
  JSStringRelease (value_string);
}

void
ephy_js_object_set_property_from_uint64 (JSContextRef context,
                                         JSObjectRef obj,
                                         const char *name,
                                         guint64 value,
                                         JSValueRef *exception)
{
  JSStringRef name_string;

  name_string = JSStringCreateWithUTF8CString (name);
  JSObjectSetProperty (context, obj, 
                       name_string, JSValueMakeNumber (context, value),
                       kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                       exception);
  JSStringRelease (name_string);
}

void
ephy_js_object_set_property_from_boolean (JSContextRef context,
                                          JSObjectRef obj,
                                          const char *name,
                                          gboolean value,
                                          JSValueRef *exception)
{
  JSStringRef name_string;

  name_string = JSStringCreateWithUTF8CString (name);
  JSObjectSetProperty (context, obj, 
                       name_string, JSValueMakeBoolean (context, value),
                       kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                       exception);
  JSStringRelease (name_string);
}

void
ephy_js_object_set_property_from_json (JSContextRef context,
				       JSObjectRef obj,
                                       const char *name,
                                       const char *json_value,
                                       JSValueRef *exception)
{
  JSStringRef name_string;
  JSStringRef value_string;

  name_string = JSStringCreateWithUTF8CString (name);
  value_string = JSStringCreateWithUTF8CString (json_value);
  JSObjectSetProperty (context, obj, 
                       name_string, JSValueMakeFromJSONString (context, value_string),
                       kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                       exception);
  JSStringRelease (name_string);
  JSStringRelease (value_string);
}

void
ephy_js_object_set_property_from_value (JSContextRef context,
					JSObjectRef obj,
                                        const char *name,
                                        JSValueRef value,
                                        JSValueRef *exception)
{
  JSStringRef name_string;

  name_string = JSStringCreateWithUTF8CString (name);
  JSObjectSetProperty (context, obj, 
                       name_string, value,
                       kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete,
                       exception);
  JSStringRelease (name_string);
}

char *
ephy_json_path_query_string (const char *path_query,
			     JsonNode *node)
{
  JsonNode *found_node;
  char *result = NULL;

  found_node = json_path_query (path_query, node, NULL);
  if (found_node) {
    if (JSON_NODE_HOLDS_ARRAY (found_node)) {
      JsonArray *array = json_node_get_array (found_node);
      if (json_array_get_length (array) > 0) {
        result = g_strdup (json_array_get_string_element (array, 0));
      }
    }
    json_node_free (found_node);
  }
  return result;
}

GList *
ephy_json_path_query_string_list (const char *path_query,
				  JsonNode *node)
{
  JsonNode *found_node;
  GList *result = NULL;

  found_node = json_path_query (path_query, node, NULL);
  if (found_node) {
    if (JSON_NODE_HOLDS_ARRAY (found_node)) {
      JsonArray *array;
      int i;

      array = json_node_get_array (found_node);
      if (json_array_get_length (array) > 0 && JSON_NODE_HOLDS_ARRAY (json_array_get_element (array, 0))) {
	JsonArray *inner_array;

	inner_array = json_array_get_array_element (array, 0);

	for (i = 0; i < json_array_get_length (inner_array); i++) {
	  result = g_list_append (result, g_strdup (json_array_get_string_element (inner_array, i)));
	}
      }
    }
    json_node_free (found_node);
  }

  return result;
}

char *
ephy_json_path_query_best_icon (const char *path_query,
                                JsonNode *node)
{
  JsonNode *found_node;
  char *result = NULL;

  found_node = json_path_query ("$.icons", node, NULL);
  if (found_node) {
    if (JSON_NODE_HOLDS_ARRAY (found_node)) {
      JsonArray *array;

      array = json_node_get_array (found_node);
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
            result = g_strdup  (json_object_get_string_member (object, (char *) best_node->data));
          }
          g_list_free (members);
        }
      }
    }
    json_node_free (found_node);
  }

  return result;
}

void
_ephy_js_set_exception (JSContextRef context,
                        JSValueRef *exception,
                        const char *file,
                        const char *function,
                        int line,
                        const char *message)
{
  JSObjectRef exception_obj;

  exception_obj = JSObjectMake (context, NULL, NULL);

  if (file) {
    ephy_js_object_set_property_from_string (context, exception_obj,
                                             "sourceURL", file,
                                             exception);
  }

  if (function) {
    ephy_js_object_set_property_from_string (context, exception_obj,
                                             "name", function,
                                             exception);
  }

  if (line) {
    ephy_js_object_set_property_from_uint64 (context, exception_obj,
                                             "line", (guint64) line,
                                             exception);
  }

  if (message) {
    ephy_js_object_set_property_from_string (context, exception_obj,
                                             "message", message,
                                             exception);
  }

  if (*exception == NULL) {
    *exception = exception_obj;
  }
}
