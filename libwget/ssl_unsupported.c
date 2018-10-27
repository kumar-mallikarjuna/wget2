/*
 * Copyright(c) 2015-2018 Free Software Foundation, Inc.
 *
 * This file is part of libwget.
 *
 * Libwget is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Libwget is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libwget.  If not, see <https://www.gnu.org/licenses/>.
 *
 *
 */
#include <config.h>

#if !defined(WITH_OPENSSL) && !defined(WITH_GNUTLS)

#include <stddef.h>

#include <wget.h>
#include "private.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

void wget_ssl_set_config_string(int key, const char *value) { }
void wget_ssl_set_config_object(int key, void *value) { }
void wget_ssl_set_config_int(int key, int value) { }

void wget_ssl_init(void) { }
void wget_ssl_deinit(void) { }

int wget_ssl_open(wget_tcp_t *tcp) { return WGET_E_TLS_DISABLED; }
void wget_ssl_close(void **session) { }

ssize_t wget_ssl_read_timeout(void *session, char *buf, size_t count, int timeout) { return 0; }
ssize_t wget_ssl_write_timeout(void *session, const char *buf, size_t count, int timeout) { return 0; }

void wget_tcp_set_stats_tls(const wget_stats_callback_t fn) { }
const void *wget_tcp_get_stats_tls(const wget_tls_stats_t type, const void *stats) { return NULL;}
void wget_tcp_set_stats_ocsp(const wget_stats_callback_t fn) { }
const void *wget_tcp_get_stats_ocsp(const wget_ocsp_stats_t type, const void *stats) { return NULL;}

#endif
