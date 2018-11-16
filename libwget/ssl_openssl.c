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

#ifdef WITH_OPENSSL

#include <dirent.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include <wget.h>
#include "net.h"
#include "private.h"

static struct _config
{
	const char
		*secure_protocol,
		*ca_directory,
		*ca_file,
		*cert_file,
		*key_file,
		*crl_file,
		*ocsp_server,
		*alpn;
	wget_ocsp_db_t
		*ocsp_cert_cache,
		*ocsp_host_cache;
	wget_tls_session_db_t
		*tls_session_cache;
	wget_hpkp_db_t
		*hpkp_cache;
	char
		ca_type,
		cert_type,
		key_type;
	bool
		check_certificate :1,
		check_hostname :1,
		print_info :1,
		ocsp :1,
		ocsp_stapling :1;
} _config = {
	.check_certificate = 1,
	.check_hostname = 1,
#ifdef HAVE_GNUTLS_OCSP_H
	.ocsp = 1,
	.ocsp_stapling = 1,
#endif
	.ca_type = WGET_SSL_X509_FMT_PEM,
	.cert_type = WGET_SSL_X509_FMT_PEM,
	.key_type = WGET_SSL_X509_FMT_PEM,
	.secure_protocol = "AUTO",
	.ca_directory = "system",
#ifdef WITH_LIBNGHTTP2
	.alpn = "h2,http/1.1"
#endif
	};

static int _init;
static wget_thread_mutex_t _mutex;

static SSL_CTX *_ctx;

/*
 * Constructor & destructor
 */
static void __attribute__ ((constructor)) _wget_tls_init(void)
{
	if (!_mutex)
		wget_thread_mutex_init(&_mutex);
}

static void __attribute__ ((destructor)) _wget_tls_exit(void)
{
	if (_mutex)
		wget_thread_mutex_destroy(&_mutex);
}

/*
 * SSL/TLS configuration functions
 */

/**
 * \param[in] key An identifier for the config parameter (starting with `WGET_SSL_`) to set
 * \param[in] value The value for the config parameter (a NULL-terminated string)
 *
 * Set a configuration parameter, as a string.
 *
 * The following parameters accept a string as their value (\p key can have any of those values):
 *
 *  - WGET_SSL_SECURE_PROTOCOL: A string describing which SSL/TLS version should be used. It can have either
 *  an arbitrary value, or one of the following fixed values (case does not matter):
 *      - "SSL": SSLv3 will be used. Warning: this protocol is insecure and should be avoided.
 *      - "TLSv1": TLS 1.0 will be used.
 *      - "TLSv1_1": TLS 1.1 will be used.
 *      - "TLSv1_2": TLS 1.2 will be used.
 *      - "TLSv1_3": TLS 1.3 will be used.
 *      - "AUTO": Let the TLS library decide.
 *      - "PFS": Let the TLS library decide, but make sure only forward-secret ciphers are used.
 *
 *  An arbitrary string can also be supplied (an string that's different from any of the previous ones). If that's the case
 *  the string will be directly taken as the priority string and sent to the library. Priority strings provide the greatest flexibility,
 *  but have a library-specific syntax. A GnuTLS priority string will not work if your libwget has been compiled with OpenSSL, for instance.
 *  - WGET_SSL_CA_DIRECTORY: A path to the directory where the root certificates will be taken from
 *  for server cert validation. Every file of that directory is expected to contain an X.509 certificate,
 *  encoded in PEM format. If the string "system" is specified, the system's default directory will be used.
 *  The default value is "system". Certificates get loaded in wget_ssl_init().
 *  - WGET_SSL_CA_FILE: A path to a file containing a single root certificate. This will be used to validate
 *  the server's certificate chain. This option can be used together with `WGET_SSL_CA_DIRECTORY`. The certificate
 *  can be in either PEM or DER format. The format is specified in the `WGET_SSL_CA_TYPE` option (see
 *  wget_ssl_set_config_int()).
 *  - WGET_SSL_CERT_FILE: Set the client certificate. It will be used for client authentication if the server requests it.
 *  It can be in either PEM or DER format. The format is specified in the `WGET_SSL_CERT_TYPE` option (see
 *  wget_ssl_set_config_int()). The `WGET_SSL_KEY_FILE` option specifies the private key corresponding to the cert's
 *  public key. If `WGET_SSL_KEY_FILE` is not set, then the private key is expected to be in the same file as the certificate.
 *  - WGET_SSL_KEY_FILE: Set the private key corresponding to the client certificate specified in `WGET_SSL_CERT_FILE`.
 *  It can be in either PEM or DER format. The format is specified in the `WGET_SSL_KEY_TYPE` option (see
 *  wget_ssl_set_config_int()). IF `WGET_SSL_CERT_FILE` is not set, then the certificate is expected to be in the same file
 *  as the private key.
 *  - WGET_SSL_CRL_FILE: Sets a CRL (Certificate Revocation List) file which will be used to verify client and server certificates.
 *  A CRL file is a black list that contains the serial numbers of the certificates that should not be treated as valid. Whenever
 *  a client or a server presents a certificate in the TLS handshake whose serial number is contained in the CRL, the handshake
 *  will be immediately aborted. The CRL file must be in PEM format.
 *  - WGET_SSL_OCSP_SERVER: Set the URL of the OCSP server that will be used to validate certificates.
 *  OCSP is a protocol by which a server is queried to tell whether a given certificate is valid or not. It's an approach contrary
 *  to that used by CRLs. While CRLs are black lists, OCSP takes a white list approach where a certificate can be checked for validity.
 *  Whenever a client or server presents a certificate in a TLS handshake, the provided URL will be queried (using OCSP) to check whether
 *  that certifiacte is valid or not. If the server responds the certificate is not valid, the handshake will be immediately aborted.
 *  - WGET_SSL_ALPN: Sets the ALPN string to be sent to the remote host. ALPN is a TLS extension
 *  ([RFC 7301](https://tools.ietf.org/html/rfc7301))
 *  that allows both the server and the client to signal which application-layer protocols they support (HTTP/2, QUIC, etc.).
 *  That information can then be used for the server to ultimately decide which protocol will be used on top of TLS.
 *
 *  An invalid value for \p key will not harm the operation of TLS, but will cause
 *  a complain message to be printed to the error log stream.
 */
void wget_ssl_set_config_string(int key, const char *value)
{
	switch (key) {
	case WGET_SSL_SECURE_PROTOCOL:
		_config.secure_protocol = value;
		break;
	case WGET_SSL_CA_DIRECTORY:
		_config.ca_directory = value;
		break;
	case WGET_SSL_CA_FILE:
		_config.ca_file = value;
		break;
	case WGET_SSL_CERT_FILE:
		_config.cert_file = value;
		break;
	case WGET_SSL_KEY_FILE:
		_config.key_file = value;
		break;
	case WGET_SSL_CRL_FILE:
		_config.crl_file = value;
		break;
	case WGET_SSL_OCSP_SERVER:
		_config.ocsp_server = value;
		break;
	case WGET_SSL_ALPN:
		_config.alpn = value;
		break;
	default:
		error_printf(_("Unknown configuration key %d (maybe this config value should be of another type?)\n"), key);
	}
}

/**
 * \param[in] key An identifier for the config parameter (starting with `WGET_SSL_`) to set
 * \param[in] value The value for the config parameter (a pointer)
 *
 * Set a configuration parameter, as a libwget object.
 *
 * The following parameters expect an already initialized libwget object as their value.
 *
 * - WGET_SSL_OCSP_CACHE: This option takes a pointer to a \ref wget_ocsp_db_t
 *  structure as an argument. Such a pointer is returned when initializing the OCSP cache with wget_ocsp_db_init().
 *  The cache is used to store OCSP responses locally and avoid querying the OCSP server repeatedly for the same certificate.
 *  - WGET_SSL_SESSION_CACHE: This option takes a pointer to a \ref wget_tls_session_db_t structure.
 *  Such a pointer is returned when initializing the TLS session cache with wget_tls_session_db_init().
 *  This option thus sets the handle to the TLS session cache that will be used to store TLS sessions.
 *  The TLS session cache is used to support TLS session resumption. It stores the TLS session parameters derived from a previous TLS handshake
 *  (most importantly the session identifier and the master secret) so that there's no need to run the handshake again
 *  the next time we connect to the same host. This is useful as the handshake is an expensive process.
 *  - WGET_SSL_HPKP_CACHE: Set the HPKP cache to be used to verify known HPKP pinned hosts. This option takes a pointer
 *  to a \ref wget_hpkp_db_t structure. Such a pointer is returned when initializing the HPKP cache
 *  with wget_hpkp_db_init(). HPKP is a HTTP-level protocol that allows the server to "pin" its present and future X.509
 *  certificate fingerprints, to support rapid certificate change in the event that the higher level root CA
 *  gets compromised ([RFC 7469](https://tools.ietf.org/html/rfc7469)).
 */
void wget_ssl_set_config_object(int key, void *value)
{
	switch (key) {
	case WGET_SSL_OCSP_CACHE:
		_config.ocsp_cert_cache = (wget_ocsp_db_t *) value;
		break;
	case WGET_SSL_SESSION_CACHE:
		_config.tls_session_cache = (wget_tls_session_db_t *) value;
		break;
	case WGET_SSL_HPKP_CACHE:
		_config.hpkp_cache = (wget_hpkp_db_t *) value;
		break;
	default:
		error_printf(_("Unknown configuration key %d (maybe this config value should be of another type?)\n"), key);
	}
}

/**
 * \param[in] key An identifier for the config parameter (starting with `WGET_SSL_`)
 * \param[in] value The value for the config parameter
 *
 * Set a configuration parameter, as an integer.
 *
 * These are the parameters that can be set (\p key can have any of these values):
 *
 *  - WGET_SSL_CHECK_CERTIFICATE: whether certificates should be verified (1) or not (0)
 *  - WGET_SSL_CHECK_HOSTNAME: whether or not to check if the certificate's subject field
 *  matches the peer's hostname. This check is done according to the rules in [RFC 6125](https://tools.ietf.org/html/rfc6125)
 *  and typically involves checking whether the hostname and the common name (CN) field of the subject match.
 *  - WGET_SSL_PRINT_INFO: whether or not information should be printed about the established SSL/TLS handshake (negotiated
 *  ciphersuites, certificates, etc.). The default is no (0).
 *
 * The following three options all can take either `WGET_SSL_X509_FMT_PEM` (to specify the PEM format) or `WGET_SSL_X509_FMT_DER`
 * (for the DER format). The default in for all of them is `WGET_SSL_X509_FMT_PEM`.
 *
 *  - WGET_SSL_CA_TYPE: Specifies what's the format of the root CA certificate(s) supplied with either `WGET_SSL_CA_DIRECTORY`
 *  or `WGET_SSL_CA_FILE`.
 *  - WGET_SSL_CERT_TYPE: Specifies what's the format of the certificate file supplied with `WGET_SSL_CERT_FILE`. **The certificate
 *  and the private key supplied must both be of the same format.**
 *  - WGET_SSL_KEY_TYPE: Specifies what's the format of the private key file supplied with `WGET_SSL_KEY_FILE`. **The private key
 *  and the certificate supplied must both be of the same format.**
 *
 * The following two options control OCSP queries. These don't affect the CRL set with `WGET_SSL_CRL_FILE`, if any.
 * If both CRLs and OCSP are enabled, both will be used.
 *
 *  - WGET_SSL_OCSP: whether or not OCSP should be used. The default is yes (1).
 *  - WGET_SSL_OCSP_STAPLING: whether or not OCSP stapling should be used. The default is yes (1).
 */
void wget_ssl_set_config_int(int key, int value)
{
	switch (key) {
	case WGET_SSL_CHECK_CERTIFICATE:
		_config.check_certificate = value;
		break;
	case WGET_SSL_CHECK_HOSTNAME:
		_config.check_hostname = value;
		break;
	case WGET_SSL_PRINT_INFO:
		_config.print_info = value;
		break;
	case WGET_SSL_CA_TYPE:
		_config.ca_type = value;
		break;
	case WGET_SSL_CERT_TYPE:
		_config.cert_type = value;
		break;
	case WGET_SSL_KEY_TYPE:
		_config.key_type = value;
		break;
	case WGET_SSL_OCSP:
		_config.ocsp = value;
		break;
	case WGET_SSL_OCSP_STAPLING:
		_config.ocsp_stapling = value;
		break;
	default:
		error_printf(_("Unknown configuration key %d (maybe this config value should be of another type?)\n"), key);
	}
}

/*
 * SSL/TLS core public API
 */
static int openssl_load_crl(X509_STORE *store, const char *crl_file)
{
	X509_LOOKUP *lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());

	if (!X509_load_crl_file(lookup, crl_file, X509_FILETYPE_PEM))
		return WGET_E_UNKNOWN;
	if (!X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK_ALL | X509_V_FLAG_USE_DELTAS))
		return WGET_E_UNKNOWN;

	return 0;
}

#define SET_MIN_VERSION(ctx, ver) \
	if (!SSL_CTX_set_min_proto_version(ctx, ver)) \
		return WGET_E_UNKNOWN

static int openssl_set_priorities(SSL_CTX *ctx, const char *prio)
{
	/*
	 * Default ciphers. This is what will be used
	 * if 'auto' is specified as the priority (currently the default).
	 */
	const char *openssl_ciphers = "HIGH:!aNULL:!RC4:!MD5:!SRP:!PSK";

	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS_MAX_VERSION);

	if (!wget_strcasecmp_ascii(prio, "SSL")) {
		SET_MIN_VERSION(ctx, SSL3_VERSION);
	} else if (!wget_strcasecmp_ascii(prio, "TLSv1")) {
		SET_MIN_VERSION(ctx, TLS1_VERSION);
	} else if (!wget_strcasecmp_ascii(prio, "TLSv1_1")) {
		SET_MIN_VERSION(ctx, TLS1_1_VERSION);
	/*
	 * Skipping "TLSv1_2".
	 * Checking for "TLSv1_2" is totally redundant - we already set it as the minimum supported version by default
	 */
	} else if (!wget_strcasecmp_ascii(prio, "TLSv1_3")) {
		/* OpenSSL supports TLS 1.3 starting at 1.1.1-beta9 (0x10101009) */
#if OPENSSL_VERSION_NUMBER >= 0x10101009
		SET_MIN_VERSION(ctx, TLS1_3_VERSION);
#else
		info_printf(_("OpenSSL: TLS 1.3 is not supported by your OpenSSL version. Will use TLS 1.2 instead.\n"));
#endif
	} else if (!wget_strcasecmp_ascii(prio, "PFS")) {
		/* Forward-secrecy - Disable RSA key exchange! */
		openssl_ciphers = "HIGH:!aNULL:!RC4:!MD5:!SRP:!PSK:!kRSA";
	} else if (prio && wget_strcasecmp_ascii(prio, "AUTO") &&
			wget_strcasecmp_ascii(prio, "TLSv1_2")) {
		openssl_ciphers = prio;
	}

	if (!SSL_CTX_set_cipher_list(ctx, openssl_ciphers)) {
		error_printf(_("OpenSSL: Invalid priority string '%s'\n"), prio);
		return WGET_E_INVALID;
	}

	return 0;
}

static int openssl_load_trust_file(SSL_CTX *ctx,
		const char *dir, size_t dirlen,
		const char *file, size_t filelen)
{
	size_t len = dirlen + filelen + 1;
	char full_path[len];
	snprintf(full_path, len, "%s/%s", dir, file);
	return (SSL_CTX_load_verify_locations(ctx, full_path, NULL) ? 0 : -1);
}

static int openssl_load_trust_files_from_directory(SSL_CTX *ctx, const char *dirname)
{
	DIR *dir;
	struct dirent *dp;
	size_t dirlen, filelen;
	int loaded = 0;

	if ((dir = opendir(dirname))) {
		dirlen = strlen(dirname);

		while ((dp = readdir(dir))) {
			filelen = strlen(dp->d_name);
			if (filelen >= 4 && !wget_strncasecmp_ascii(dp->d_name, ".pem", 4) &&
					openssl_load_trust_file(ctx, dirname, dirlen, dp->d_name, filelen) == 0)
				loaded++;
		}

		closedir(dir);
	}

	return loaded;
}

static int openssl_load_trust_files(SSL_CTX *ctx, const char *dir)
{
	int retval;

	if (!strcmp(dir, "system")) {
		/*
		 * Load system-provided certificates.
		 * Either "/etc/ssl/certs" or OpenSSL's default (if provided).
		 */
		if (SSL_CTX_set_default_verify_paths(ctx)) {
			retval = 0;
			goto end;
		}

		dir = "/etc/ssl/certs";
		info_printf(_("OpenSSL: Could not load certificates from default paths. Falling back to '%s'."), dir);
	}

	retval = openssl_load_trust_files_from_directory(ctx, dir);
	if (retval == 0)
		error_printf(_("OpenSSL: No certificates could be loaded from directory '%s'\n"), dir);
	else if (retval > 0)
		debug_printf(_("OpenSSL: Loaded %d certificates\n"), retval);
	else
		error_printf(_("OpenSSL: Could not open directory '%s'. No certificates were loaded.\n"), dir);

end:
	return retval;
}

static int openssl_init(SSL_CTX *ctx)
{
	int retval = 0;
	X509_STORE *store;

	if (_config.ca_directory && *_config.ca_directory && _config.check_certificate) {
		retval = openssl_load_trust_files(ctx, _config.ca_directory);
		if (retval < 0)
			goto end;

		if (_config.crl_file) {
			/* Load CRL file in PEM format. */
			store = SSL_CTX_get_cert_store(ctx);
			if (!store) {
				error_printf(_("OpenSSL: Could not obtain cert store\n"));
				retval = WGET_E_UNKNOWN;
				goto end;
			}
			if ((retval = openssl_load_crl(store, _config.crl_file)) < 0) {
				error_printf(_("Could not load CRL from '%s' (%d)\n"),
						_config.crl_file,
						retval);
				goto end;
			}
		}

		retval = openssl_set_priorities(ctx, _config.secure_protocol);
	}

end:
	return retval;
}

static void openssl_deinit(SSL_CTX *ctx)
{
	SSL_CTX_free(ctx);
}

/**
 * Initialize the SSL/TLS engine as a client.
 *
 * This function assumes the caller is an SSL client connecting to a server.
 * The functions wget_ssl_open(), wget_ssl_close() and wget_ssl_deinit() can be called
 * after this.
 *
 * This is where the root certificates get loaded from the folder specified in the
 * `WGET_SSL_CA_DIRECTORY` parameter. If any of the files in that folder cannot be loaded
 * for whatever reason, that file will be silently skipped without harm (a message will be
 * printed to the debug log stream).
 *
 * CLRs and private keys and their certificates are also loaded here.
 *
 * On systems with automatic library constructors/destructors, this function
 * is thread-safe. On other systems it is not thread-safe.
 *
 * This function may be called several times. Only the first call really
 * takes action.
 */
void wget_ssl_init(void)
{
	wget_thread_mutex_lock(_mutex);

	if (!_init) {
		_ctx = SSL_CTX_new(TLS_client_method());
		if (_ctx && openssl_init(_ctx) == 0) {
			_init++;
			debug_printf(_("OpenSSL initialized\n"));
		} else {
			error_printf(_("Could not initialize OpenSSL\n"));
		}
	}

	wget_thread_mutex_unlock(_mutex);
}

/**
 * Deinitialize the SSL/TLS engine, after it has been initialized
 * with wget_ssl_init().
 *
 * This function unloads everything that was loaded in wget_ssl_init().
 *
 * On systems with automatic library constructors/destructors, this function
 * is thread-safe. On other systems it is not thread-safe.
 *
 * This function may be called several times. Only the last deinit really
 * takes action.
 */
void wget_ssl_deinit(void)
{
	wget_thread_mutex_lock(_mutex);

	if (_init == 1)
		openssl_deinit(_ctx);

	if (_init > 0)
		_init--;

	wget_thread_mutex_unlock(_mutex);
}

static int ssl_resume_session(SSL *ssl, const char *hostname)
{
	void *sess = NULL;
	unsigned long sesslen;
	SSL_SESSION *ssl_session;

	if (!_config.tls_session_cache)
		return 0;

	if (wget_tls_session_get(_config.tls_session_cache,
			hostname,
			&sess, &sesslen) &&
			sess) {
		ssl_session = d2i_SSL_SESSION(NULL,
				(const unsigned char **) &sess,
				sesslen);
		if (!ssl_session)
			return -1;
#if OPENSSL_VERSION_NUMBER >= 0x10101000
		if (!SSL_SESSION_is_resumable(ssl_session))
			return -1;
#endif
		if (!SSL_set_session(ssl, ssl_session))
			return -1;
		SSL_SESSION_free(ssl_session);

		return 1;
	}

	return 0;
}

static int ssl_save_session(const SSL *ssl, const char *hostname)
{
	void *sess = NULL;
	unsigned long sesslen;
	SSL_SESSION *ssl_session = SSL_get0_session(ssl);

	if (!ssl_session || !_config.tls_session_cache)
		return 0;

	sesslen = i2d_SSL_SESSION(ssl_session, (unsigned char **) &sess);
	if (sesslen) {
		wget_tls_session_db_add(_config.tls_session_cache,
				wget_tls_session_new(hostname,
						18 * 3600, /* session valid for 18 hours */
						sess, sesslen));
		SSL_free(sess);
		return 1;
	}

	return 0;
}

static int wait_2_read_and_write(int sockfd, int timeout)
{
	int retval = wget_ready_2_transfer(sockfd,
			timeout,
			WGET_IO_READABLE | WGET_IO_WRITABLE);

	if (retval == 0)
		retval = WGET_E_TIMEOUT;

	return retval;
}

/**
 * \param[in] tcp A TCP connection (see wget_tcp_init())
 * \return `WGET_E_SUCCESS` on success or an error code (`WGET_E_*`) on failure
 *
 * Run an SSL/TLS handshake.
 *
 * This functions establishes an SSL/TLS tunnel (performs an SSL/TLS handshake)
 * over an active TCP connection. A pointer to the (internal) SSL/TLS session context
 * can be found in `tcp->ssl_session` after successful execution of this function. This pointer
 * has to be passed to wget_ssl_close() to close the SSL/TLS tunnel.
 *
 * If the handshake cannot be completed in the specified timeout for the provided TCP connection
 * this function fails and returns `WGET_E_TIMEOUT`. You can set the timeout with wget_tcp_set_timeout().
 */
int wget_ssl_open(wget_tcp_t *tcp)
{
	SSL *ssl;
	int retval, error, resumed;

	if (!tcp || tcp->sockfd < 0)
		return WGET_E_INVALID;
	if (!_init)
		wget_ssl_init();

	/* Initiate a new TLS connection from an existing OpenSSL context */
	ssl = SSL_new(_ctx);
	if (!ssl || !SSL_set_fd(ssl, tcp->sockfd))
		return WGET_E_UNKNOWN;

	/* Resume from a previous SSL/TLS session, if available */
	if ((resumed = ssl_resume_session(ssl, tcp->ssl_hostname)) == 1)
		debug_printf(_("Resuming cached TLS session"));
	else if (resumed == 0)
		debug_printf(_("No cached TLS session available. Will run a full handshake."));
	else
		error_printf(_("Could not resume cached TLS session"));

	/* Send Server Name Indication (SNI) */
	if (tcp->ssl_hostname && !SSL_set_tlsext_host_name(ssl, tcp->ssl_hostname))
		error_printf(_("SNI could not be sent"));

	do {
		/* Wait for socket to become ready */
		if (tcp->connect_timeout &&
			(retval = wait_2_read_and_write(tcp->sockfd, tcp->connect_timeout)) < 0)
			goto bail;

		/* Run TLS handshake */
		retval = SSL_connect(ssl);
		if (retval > 0)
			break;

		error = SSL_get_error(ssl, retval);
	} while (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);

	if (retval <= 0) {
		SSL_free(ssl);
		retval = WGET_E_UNKNOWN;
		goto bail;
	}

	/* Success! */
	debug_printf("Handshake completed%s\n", resumed ? " (resumed session)" : "");

	/* Save the current TLS session */
	if (ssl_save_session(ssl, tcp->ssl_hostname))
		debug_printf(_("TLS session saved in cache"));
	else
		debug_printf(_("TLS session discarded"));

	tcp->ssl_session = ssl;
	return WGET_E_SUCCESS;

bail:
	SSL_free(ssl);
	return retval;
}

/**
 * \param[in] session The SSL/TLS session (a pointer to it), which is located at the `ssl_session` field
 * of the TCP connection (see wget_ssl_open()).
 *
 * Close an active SSL/TLS tunnel, which was opened with wget_ssl_open().
 *
 * The underlying TCP connection is kept open.
 */
void wget_ssl_close(void **session)
{
	SSL *ssl;
	int retval;

	if (session && *session) {
		ssl = *session;

		do
			retval = SSL_shutdown(ssl);
		while (retval == 0);

		SSL_free(ssl);
		*session = NULL;
	}
}

static int ssl_transfer(int want,
		void *session, int timeout,
		void *buf, int count)
{
	SSL *ssl;
	int fd, retval, error, attempt = 0, ops = want;

	if (count == 0)
		return 0;
	if ((ssl = session) == NULL)
		return WGET_E_INVALID;
	if ((fd = SSL_get_fd(ssl)) < 0)
		return WGET_E_UNKNOWN;

	/* SSL_read() and SSL_write() take ints, so we'd rather play safe here */
	if (count > INT_MAX)
		count = INT_MAX;

	if (timeout < -1)
		timeout = -1;

	do {
		if (timeout) {
			/* Wait until file descriptor becomes ready */
			retval = wget_ready_2_transfer(fd, timeout, ops);
			if (retval < 0)
				return retval;
			else if (retval == 0)
				return WGET_E_TIMEOUT;
		}

		/* We assume socket is non-blocking so neither of these should block */
		if (want == WGET_IO_READABLE)
			retval = SSL_read(ssl, buf, count);
		else
			retval = SSL_write(ssl, buf, count);

		if (retval < 0) {
			error = SSL_get_error(ssl, retval);

			if ((error == SSL_ERROR_WANT_READ && want == WGET_IO_READABLE) ||
					(error == SSL_ERROR_WANT_WRITE && want == WGET_IO_WRITABLE)) {
				ops = WGET_IO_WRITABLE | WGET_IO_READABLE;
				attempt++;
			} else if (error != SSL_ERROR_WANT_WRITE && error != SSL_ERROR_WANT_READ) {
				return WGET_E_UNKNOWN;
			} else {
				return 0;
			}
		}
	} while (retval < 0 && attempt < 2);

	if (retval < 0)
		return WGET_E_UNKNOWN;

	return retval;
}

/**
 * \param[in] session An opaque pointer to the SSL/TLS session (obtained with wget_ssl_open() or wget_ssl_server_open())
 * \param[in] buf Destination buffer where the read data will be placed
 * \param[in] count Length of the buffer \p buf
 * \param[in] timeout The amount of time to wait until data becomes available (in milliseconds)
 * \return The number of bytes read, or a negative value on error.
 *
 * Read data from the SSL/TLS tunnel.
 *
 * This function will read at most \p count bytes, which will be stored
 * in the buffer \p buf.
 *
 * The \p timeout parameter tells how long to wait until some data becomes
 * available to read. A \p timeout value of zero causes this function to return
 * immediately, whereas a negative value will cause it to wait indefinitely.
 * This function returns the number of bytes read, which may be zero if the timeout elapses
 * without any data having become available.
 *
 * If a rehandshake is needed, this function does it automatically and tries
 * to read again.
 */
ssize_t wget_ssl_read_timeout(void *session,
		char *buf, size_t count,
		int timeout)
{
	return ssl_transfer(WGET_IO_READABLE, session, timeout, buf, count);
}

/**
 * \param[in] session An opaque pointer to the SSL/TLS session (obtained with wget_ssl_open() or wget_ssl_server_open())
 * \param[in] buf Buffer with the data to be sent
 * \param[in] count Length of the buffer \p buf
 * \param[in] timeout The amount of time to wait until data can be sent to the wire (in milliseconds)
 * \return The number of bytes written, or a negative value on error.
 *
 * Send data through the SSL/TLS tunnel.
 *
 * This function will write \p count bytes from \p buf.
 *
 * The \p timeout parameter tells how long to wait until data can be finally sent
 * over the SSL/TLS tunnel. A \p timeout value of zero causes this function to return
 * immediately, whereas a negative value will cause it to wait indefinitely.
 * This function returns the number of bytes sent, which may be zero if the timeout elapses
 * before any data could be sent.
 *
 * If a rehandshake is needed, this function does it automatically and tries
 * to write again.
 */
ssize_t wget_ssl_write_timeout(void *session,
		const char *buf, size_t count,
		int timeout)
{
	return ssl_transfer(WGET_IO_WRITABLE, session, timeout, (void *) buf, count);
}

/*
 * SSL/TLS stats API
 */
void wget_tcp_set_stats_tls(wget_stats_callback_t fn)
{
	/* TODO implement this */
}

const void *wget_tcp_get_stats_tls(wget_tls_stats_t type, const void *stats)
{
	/* TODO implement this */
	return NULL;
}

void wget_tcp_set_stats_ocsp(wget_stats_callback_t fn)
{
	/* TODO implement this */
}

const void *wget_tcp_get_stats_ocsp(wget_ocsp_stats_t type, const void *stats)
{
	/* TODO implement this */
	return NULL;
}

#endif /* WITH_OPENSSL */