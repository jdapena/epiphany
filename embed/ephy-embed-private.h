/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2012 Igalia S.L.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__EPHY_EPIPHANY_H_INSIDE__) && !defined (EPIPHANY_COMPILATION)
#error "Only <epiphany/epiphany.h> can be included directly."
#endif

#ifndef EPHY_EMBED_PRIVATE_H
#define EPHY_EMBED_PRIVATE_H

/* EphyWebView */

#define EPHY_WEB_VIEW_NON_SEARCH_REGEX  "(" \
                                        "^localhost(\\.[^[:space:]]+)?(:\\d+)?(/.*)?$|" \
                                        "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]$|" \
                                        "^::[0-9a-f:]*$|" \
                                        "^[0-9a-f:]+:[0-9a-f:]*$|" \
                                        "^[^\\.[:space:]]+\\.[^\\.[:space:]]+.*$|" \
                                        "^https?://[^/\\.[:space:]]+.*$|" \
                                        "^about:.*$|" \
                                        "^data:.*$|" \
                                        "^file:.*$" \
                                        ")"


#endif
