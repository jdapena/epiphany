/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-js-utils.h
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

#if !defined (__EPHY_EPIPHANY_H_INSIDE__) && !defined (EPIPHANY_COMPILATION)
#error "Only <epiphany/epiphany.h> can be included directly."
#endif

#ifndef _EPHY_JS_UTILS_H
#define _EPHY_JS_UTILS_H

#include <glib.h>
#include <JavaScriptCore/JavaScript.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

char *        ephy_js_string_to_utf8                   (JSStringRef js_string);

/* JSContextRef helpers */

char *        ephy_js_context_get_location             (JSContextRef context,
                                                        JSValueRef *exception);
gboolean      ephy_js_context_in_origin                (JSContextRef context,
                                                        const char *origin,
                                                        JSValueRef *exception);
JSValueRef    ephy_js_context_eval_as_function         (JSContextRef context,
                                                        const char *script,
                                                        JSValueRef *exception);

/* JSObjectRef helpers */

JSValueRef    ephy_js_object_get_property              (JSContextRef context,
                                                        JSObjectRef obj,
                                                        const char *name,
                                                        JSValueRef *exception);
void          ephy_js_object_set_property_from_string  (JSContextRef context,
                                                        JSObjectRef obj,
                                                        const char *name,
                                                        const char *value,
                                                        JSValueRef *exception);
void          ephy_js_object_set_property_from_uint64  (JSContextRef context,
                                                        JSObjectRef obj,
                                                        const char *name,
                                                        guint64 value,
                                                        JSValueRef *exception);
void          ephy_js_object_set_property_from_boolean (JSContextRef context,
                                                        JSObjectRef obj,
                                                        const char *name,
                                                        gboolean value,
                                                        JSValueRef *exception);
void          ephy_js_object_set_property_from_json    (JSContextRef context,
                                                        JSObjectRef obj,
                                                        const char *name,
                                                        const char *json_value,
                                                        JSValueRef *exception);
void          ephy_js_object_set_property_from_value   (JSContextRef context,
                                                        JSObjectRef obj,
                                                        const char *name,
                                                        JSValueRef value,
                                                        JSValueRef *exception);

/* json-glib helpers */

char *        ephy_json_path_query_string          (const char *path_query,
                                                    JsonNode *node);
char *        ephy_json_path_query_best_icon       (const char *path_query,
                                                    JsonNode *node);
						    
G_END_DECLS

#endif /* _EPHY_JS_UTILS_H */
