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
