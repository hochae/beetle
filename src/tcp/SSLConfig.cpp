/*
 * SSLConfig.cpp
 *
 *  Created on: May 1, 2016
 *      Author: james
 */

#include "tcp/SSLConfig.h"

#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cassert>

bool SSLConfig::initialized = false;
std::mutex SSLConfig::initMutex;

inline bool fileExists(const std::string& name) {
    return (access(name.c_str(), F_OK) != -1);
}

SSLConfig::SSLConfig(bool verifyPeers, bool isServer, std::string cert, std::string key) {
	initMutex.lock();
	if (!initialized) {
		SSL_load_error_strings();
		SSL_library_init();
		initialized = true;
	}
	initMutex.unlock();

	const SSL_METHOD *method;
	if (isServer) {
		method = SSLv23_server_method();
	} else {
		method = SSLv23_client_method();
	}
	ctx = SSL_CTX_new(method);
	SSL_CTX_set_default_verify_paths(ctx);
	if (verifyPeers) {
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
	} else {
		SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
	}
	SSL_CTX_set_verify_depth(ctx, 3);
	SSL_CTX_set_cipher_list(ctx, "HIGH");
	SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);

	if (isServer) {
		assert(fileExists(cert));
		assert(fileExists(key));

	    /* Set the key and cert */
	    if (SSL_CTX_use_certificate_file(ctx, cert.c_str(), SSL_FILETYPE_PEM) < 0) {
	    	ERR_print_errors_fp(stderr);
	    	exit(EXIT_FAILURE);
	    }

	    if (SSL_CTX_use_PrivateKey_file(ctx, key.c_str(), SSL_FILETYPE_PEM) < 0 ) {
	        ERR_print_errors_fp(stderr);
	        exit(EXIT_FAILURE);
	    }
	}
}

SSLConfig::~SSLConfig() {
    SSL_CTX_free(ctx);
    ERR_free_strings();
    EVP_cleanup();
}

SSL_CTX *SSLConfig::getCtx() {
	return ctx;
}

