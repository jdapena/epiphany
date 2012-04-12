/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * ephy-js-chrome-apps.h
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

#ifndef _EPHY_JS_CHROME_APPS_H
#define _EPHY_JS_CHROME_APPS_H

#include <glib.h>
#include <JavaScriptCore/JavaScript.h>

/* Files */
#define EPHY_WEB_APPLICATION_CHROME_MANIFEST "ephy-web-app.chrome-manifest.json"
#define EPHY_WEB_APPLICATION_CHROME_WEBSTORE_MANIFEST "ephy-web-app.chrome-webstore-manifest.json"
#define EPHY_WEB_APPLICATION_CHROME_CRX "ephy-web-app.chrome-webstore-extension.crx"
#define EPHY_WEB_APPLICATION_CHROME_CRX_CONTENTS "ephy-web-app.chrome-webstore-crx-contents"

/* Custom keys */
#define EPHY_WEB_APPLICATION_CHROME_ID "chrome-id"
#define EPHY_WEB_APPLICATION_CHROME_DEFAULT_LOCALE "chrome-default-locale"


G_BEGIN_DECLS

void     ephy_chrome_apps_install_crx_from_file (const char *origin,
						 const char *crx_path);

void     ephy_chrome_apps_setup_js_api  (JSGlobalContextRef context);

G_END_DECLS

#endif /* _EPHY_JS_CHROME_APPS_H */
