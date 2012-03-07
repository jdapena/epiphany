/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright (C) 2012, Igalia S.L.
 */

#ifndef EPHY_REQUEST_CHROME_EXTENSION_H
#define EPHY_REQUEST_CHROME_EXTENSION_H 1

#define LIBSOUP_USE_UNSTABLE_REQUEST_API
#include <libsoup/soup-request.h>

#define EPHY_TYPE_REQUEST_CHROME_EXTENSION            (ephy_request_chrome_extension_get_type ())
#define EPHY_REQUEST_CHROME_EXTENSION(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), EPHY_TYPE_REQUEST_CHROME_EXTENSION, EphyRequestChromeExtension))
#define EPHY_REQUEST_CHROME_EXTENSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EPHY_TYPE_REQUEST_CHROME_EXTENSION, EphyRequestChromeExtensionClass))
#define EPHY_IS_REQUEST_CHROME_EXTENSION(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), EPHY_TYPE_REQUEST_CHROME_EXTENSION))
#define EPHY_IS_REQUEST_CHROME_EXTENSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EPHY_TYPE_REQUEST_CHROME_EXTENSION))
#define EPHY_REQUEST_CHROME_EXTENSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), EPHY_TYPE_REQUEST_CHROME_EXTENSION, EphyRequestChromeExtensionClass))

#define EPHY_CHROME_EXTENSION_SCHEME "chrome-extension"
#define EPHY_CHROME_EXTENSION_SCHEME_LEN 16

typedef struct _EphyRequestChromeExtensionPrivate EphyRequestChromeExtensionPrivate;

typedef struct {
  SoupRequest parent;

  EphyRequestChromeExtensionPrivate *priv;
} EphyRequestChromeExtension;

typedef struct {
  SoupRequestClass parent;

} EphyRequestChromeExtensionClass;

GType ephy_request_chrome_extension_get_type (void);

#endif /* EPHY_REQUEST_CHROME_EXTENSION_H */
