/*
 * Throughput micro-benchmark for socket bufferevents, with an opt-in
 * EVENT_BASE_FLAG_IO_URING toggle so the io_uring fast path can be
 * compared head-to-head with the synchronous read/write path on the
 * same workload and hardware.
 *
 * Two bufferevents on a unix socketpair exchange a payload R times
 * in a producer -> consumer round trip; the run prints elapsed time
 * and aggregate throughput.
 *
 * Copyright (c) 2026 Libevent contributors
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "event2/event-config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

#include "event2/event.h"
#include "event2/bufferevent.h"
#include "event2/buffer.h"
#include "event2/util.h"

#ifdef EVENT__HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include "event2/bufferevent_ssl.h"

static SSL_CTX *bench_server_ctx;
static SSL_CTX *bench_client_ctx;

static int
bench_ssl_setup(void)
{
	EVP_PKEY *key = EVP_RSA_gen(2048);
	X509 *cert = X509_new();
	X509_NAME *name;
	if (key == NULL || cert == NULL)
		return -1;
	X509_set_version(cert, 2);
	ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
	X509_gmtime_adj(X509_getm_notBefore(cert), 0);
	X509_gmtime_adj(X509_getm_notAfter(cert), 31536000L);
	X509_set_pubkey(cert, key);
	name = X509_get_subject_name(cert);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
	    (const unsigned char *)"bench", -1, -1, 0);
	X509_set_issuer_name(cert, name);
	if (!X509_sign(cert, key, EVP_sha256()))
		return -1;
	bench_server_ctx = SSL_CTX_new(TLS_server_method());
	bench_client_ctx = SSL_CTX_new(TLS_client_method());
	if (!bench_server_ctx || !bench_client_ctx)
		return -1;
	if (SSL_CTX_use_certificate(bench_server_ctx, cert) != 1 ||
	    SSL_CTX_use_PrivateKey(bench_server_ctx, key) != 1)
		return -1;
	SSL_CTX_set_verify(bench_client_ctx, SSL_VERIFY_NONE, NULL);
	X509_free(cert);
	EVP_PKEY_free(key);
	return 0;
}

static struct bufferevent *
bench_ssl_wrap(struct event_base *base, evutil_socket_t fd, int server,
    int extra_opts)
{
	SSL *ssl = SSL_new(server ? bench_server_ctx : bench_client_ctx);
	if (ssl == NULL)
		return NULL;
	return bufferevent_openssl_socket_new(base, fd, ssl,
	    server ? BUFFEREVENT_SSL_ACCEPTING : BUFFEREVENT_SSL_CONNECTING,
	    BEV_OPT_CLOSE_ON_FREE | extra_opts);
}
#endif /* EVENT__HAVE_OPENSSL */

/* ----------------------------------------------------------------------
 * Connection-close coverage.
 *
 * The streaming throughput loop never closes a connection mid-run, so it
 * cannot catch teardown/EOF bugs (e.g. an io_uring socket bufferevent whose
 * in-flight multishot recv blocks the fd from closing on free, hanging the
 * peer).  This check exercises exactly that: a producer writes one message,
 * the consumer echoes it, the producer is then freed, and we require the
 * consumer to observe the close (EOF or error) within a deadline rather than
 * hang.  It runs for plaintext and TLS, io_uring and syscall.
 * ---------------------------------------------------------------------- */
static struct event_base *cc_base;
static struct bufferevent *cc_prod;
static int cc_closed; /* 0 = none, 1 = EOF, 2 = error */

static void
cc_consumer_readcb(struct bufferevent *b, void *arg)
{
	char buf[64];
	int n = bufferevent_read(b, buf, sizeof(buf));
	if (n > 0)
		bufferevent_write(b, buf, (size_t)n);
}
static void
cc_consumer_eventcb(struct bufferevent *b, short ev, void *arg)
{
	if (ev & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		cc_closed = (ev & BEV_EVENT_EOF) ? 1 : 2;
		event_base_loopexit(cc_base, NULL);
	}
}
static void
cc_producer_readcb(struct bufferevent *b, void *arg)
{
	struct evbuffer *in = bufferevent_get_input(b);
	evbuffer_drain(in, evbuffer_get_length(in));
	/* Got the echo: free the producer so its fd closes and the consumer
	 * must detect the close. */
	if (cc_prod) {
		bufferevent_free(cc_prod);
		cc_prod = NULL;
	}
}
static void
cc_timeout(evutil_socket_t f, short w, void *arg)
{
	event_base_loopexit(cc_base, NULL);
}

/* Returns 0 if the peer detected the close, 1 if it hung. */
static int
bench_close_check(struct event_base *base, int use_ssl, int use_uring)
{
	evutil_socket_t sv[2];
	struct bufferevent *cons = NULL;
	struct event *to;
	struct timeval tv = { 5, 0 };

	cc_base = base;
	cc_prod = NULL;
	cc_closed = 0;
	(void)use_uring;
#ifndef _WIN32
	/* The consumer may echo into a socket the just-freed producer is
	 * closing; don't die from SIGPIPE while probing the close path. */
	signal(SIGPIPE, SIG_IGN);
#endif
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		return 1;
	if (evutil_make_socket_nonblocking(sv[0]) < 0 ||
	    evutil_make_socket_nonblocking(sv[1]) < 0) {
		close(sv[0]); close(sv[1]);
		return 1;
	}
#ifdef EVENT__HAVE_OPENSSL
	if (use_ssl) {
		int o = use_uring ? BEV_OPT_IO_URING_TLS : 0;
		cc_prod = bench_ssl_wrap(base, sv[0], 0, o);
		cons = bench_ssl_wrap(base, sv[1], 1, o);
	} else
#endif
	{
		cc_prod = bufferevent_socket_new(base, sv[0],
		    BEV_OPT_CLOSE_ON_FREE);
		cons = bufferevent_socket_new(base, sv[1],
		    BEV_OPT_CLOSE_ON_FREE);
	}
	if (cc_prod == NULL || cons == NULL) {
		close(sv[0]); close(sv[1]);
		return 1;
	}
	bufferevent_setcb(cc_prod, cc_producer_readcb, NULL, NULL, NULL);
	bufferevent_setcb(cons, cc_consumer_readcb, NULL, cc_consumer_eventcb,
	    NULL);
	bufferevent_enable(cc_prod, EV_READ | EV_WRITE);
	bufferevent_enable(cons, EV_READ | EV_WRITE);
	bufferevent_write(cc_prod, "x", 1);

	to = evtimer_new(base, cc_timeout, NULL);
	evtimer_add(to, &tv);
	event_base_dispatch(base);
	event_free(to);

	printf("close-check: mode=%s%s -> %s\n",
	    use_uring ? "io_uring" : "syscall", use_ssl ? "+TLS" : "",
	    cc_closed == 1 ? "CLOSE-DETECTED(EOF)" :
	    cc_closed == 2 ? "CLOSE-DETECTED(ERROR)" : "HANG-NO-CLOSE");
	if (cons)
		bufferevent_free(cons);
	return cc_closed ? 0 : 1;
}

struct bench_pair {
	struct bufferevent *producer;
	struct bufferevent *consumer;
	size_t received_this_round;
	int rounds_left;
	struct bench_state *owner;
};

struct bench_state {
	struct event_base *base;
	size_t payload_len;
	int rounds_per_pair;
	int npairs;
	int pairs_finished;
	struct bench_pair *pairs;
	char *payload;
};

static double
now_seconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void
consumer_read_cb(struct bufferevent *bev, void *arg)
{
	struct bench_pair *p = arg;
	struct bench_state *s = p->owner;
	struct evbuffer *in = bufferevent_get_input(bev);
	size_t n = evbuffer_get_length(in);

	if (n == 0)
		return;
	evbuffer_drain(in, n);
	p->received_this_round += n;
	if (p->received_this_round < s->payload_len)
		return;

	p->received_this_round = 0;
	if (--p->rounds_left == 0) {
		if (++s->pairs_finished == s->npairs)
			event_base_loopbreak(s->base);
		return;
	}
	bufferevent_write(p->producer, s->payload, s->payload_len);
}

static void
event_cb(struct bufferevent *bev, short events, void *arg)
{
	struct bench_pair *p = arg;
	struct bench_state *s = p->owner;
	(void)bev;
	if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
		fprintf(stderr, "bench: connection event 0x%x\n", events);
		event_base_loopbreak(s->base);
	}
}

static void
usage(const char *prog)
{
	fprintf(stderr,
	    "usage: %s [--uring] [--bytes N] [--rounds R] [--pairs P]\n"
	    "  --uring           enable EVENT_BASE_FLAG_IO_URING (default off)\n"
	    "  --ssl             wrap each pair in TLS (bufferevent_openssl_socket_new)\n"
	    "  --close-check     verify the peer detects connection close (no throughput run)\n"
	    "  --bytes N         payload bytes per round (default 65536)\n"
	    "  --rounds R        round trips per pair (default 10000)\n"
	    "  --pairs P         number of concurrent socket pairs (default 1)\n",
	    prog);
}

int
main(int argc, char **argv)
{
	int use_uring = 0;
	int use_ssl = 0;
	int close_check = 0;
	size_t payload_len = 65536;
	int rounds = 10000;
	int npairs = 1;
	int i, j;
	struct event_config *cfg = NULL;
	struct event_base *base = NULL;
	char *payload = NULL;
	struct bench_state state;
	double t0, t1, elapsed;
	double total_bytes;

	memset(&state, 0, sizeof(state));

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--uring")) {
			use_uring = 1;
		} else if (!strcmp(argv[i], "--ssl")) {
			use_ssl = 1;
		} else if (!strcmp(argv[i], "--close-check")) {
			close_check = 1;
		} else if (!strcmp(argv[i], "--bytes") && i + 1 < argc) {
			payload_len = (size_t)strtoull(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "--rounds") && i + 1 < argc) {
			rounds = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--pairs") && i + 1 < argc) {
			npairs = atoi(argv[++i]);
		} else {
			usage(argv[0]);
			return 2;
		}
	}
	if (payload_len == 0 || rounds <= 0 || npairs <= 0) {
		usage(argv[0]);
		return 2;
	}

	cfg = event_config_new();
	if (cfg == NULL) {
		fprintf(stderr, "bench: event_config_new failed\n");
		return 1;
	}
	if (use_uring &&
	    event_config_set_flag(cfg, EVENT_BASE_FLAG_IO_URING) < 0) {
		fprintf(stderr, "bench: event_config_set_flag failed\n");
		goto fail;
	}
	base = event_base_new_with_config(cfg);
	event_config_free(cfg);
	cfg = NULL;
	if (base == NULL) {
		fprintf(stderr, "bench: event_base_new_with_config failed\n");
		return 1;
	}

	if (use_ssl) {
#ifdef EVENT__HAVE_OPENSSL
		if (bench_ssl_setup() < 0) {
			fprintf(stderr, "bench: TLS setup failed\n");
			goto fail;
		}
#else
		fprintf(stderr, "bench: built without OpenSSL; --ssl unavailable\n");
		goto fail;
#endif
	}

	if (close_check) {
		int rc = bench_close_check(base, use_ssl, use_uring);
		event_base_free(base);
		return rc;
	}

	payload = malloc(payload_len);
	if (payload == NULL) {
		fprintf(stderr, "bench: malloc payload\n");
		goto fail;
	}
	for (size_t k = 0; k < payload_len; ++k)
		payload[k] = (char)(k & 0xff);

	state.base = base;
	state.payload_len = payload_len;
	state.rounds_per_pair = rounds;
	state.npairs = npairs;
	state.payload = payload;
	state.pairs = calloc(npairs, sizeof(*state.pairs));
	if (state.pairs == NULL) {
		fprintf(stderr, "bench: calloc pairs\n");
		goto fail;
	}

	for (j = 0; j < npairs; ++j) {
		int sv[2] = { -1, -1 };
		struct bench_pair *p = &state.pairs[j];

		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
			perror("bench: socketpair");
			goto fail;
		}
		if (evutil_make_socket_nonblocking(sv[0]) < 0 ||
		    evutil_make_socket_nonblocking(sv[1]) < 0) {
			fprintf(stderr, "bench: make_nonblocking failed\n");
			close(sv[0]); close(sv[1]);
			goto fail;
		}

		p->owner = &state;
		p->rounds_left = rounds;
#ifdef EVENT__HAVE_OPENSSL
		if (use_ssl) {
			int ssl_opts = use_uring ? BEV_OPT_IO_URING_TLS : 0;
			p->producer = bench_ssl_wrap(base, sv[0], 0, ssl_opts);
			p->consumer = bench_ssl_wrap(base, sv[1], 1, ssl_opts);
		} else
#endif
		{
			p->producer = bufferevent_socket_new(base, sv[0],
			    BEV_OPT_CLOSE_ON_FREE);
			p->consumer = bufferevent_socket_new(base, sv[1],
			    BEV_OPT_CLOSE_ON_FREE);
		}
		if (p->producer == NULL || p->consumer == NULL) {
			fprintf(stderr, "bench: bufferevent_socket_new failed\n");
			close(sv[0]); close(sv[1]);
			goto fail;
		}

		bufferevent_setcb(p->consumer, consumer_read_cb, NULL,
		    event_cb, p);
		bufferevent_setcb(p->producer, NULL, NULL, event_cb, p);
		if (bufferevent_enable(p->consumer, EV_READ) < 0 ||
		    bufferevent_enable(p->producer, EV_WRITE) < 0) {
			fprintf(stderr, "bench: bufferevent_enable failed\n");
			goto fail;
		}

		if (bufferevent_write(p->producer, payload, payload_len) < 0) {
			fprintf(stderr, "bench: initial write failed\n");
			goto fail;
		}
	}

	printf("bench_bufferevent_io: mode=%s%s payload=%zu rounds=%d pairs=%d\n",
	    use_uring ? "io_uring" : "syscall", use_ssl ? "+TLS" : "",
	    payload_len, rounds, npairs);

	t0 = now_seconds();
	if (event_base_dispatch(base) < 0) {
		fprintf(stderr, "bench: dispatch failed\n");
		goto fail;
	}
	t1 = now_seconds();

	if (state.pairs_finished != npairs) {
		fprintf(stderr, "bench: short run, %d/%d pairs finished\n",
		    state.pairs_finished, npairs);
		goto fail;
	}

	elapsed = t1 - t0;
	total_bytes = (double)payload_len * (double)rounds * (double)npairs;
	printf("elapsed: %.3f s\n", elapsed);
	printf("throughput: %.2f MiB/s (%.0f bytes/s)\n",
	    total_bytes / elapsed / (1024.0 * 1024.0),
	    total_bytes / elapsed);
	printf("per-round: %.3f us\n",
	    elapsed * 1e6 / ((double)rounds * (double)npairs));

	for (j = 0; j < npairs; ++j) {
		if (state.pairs[j].producer)
			bufferevent_free(state.pairs[j].producer);
		if (state.pairs[j].consumer)
			bufferevent_free(state.pairs[j].consumer);
	}
	free(state.pairs);
	event_base_free(base);
	free(payload);
	return 0;

fail:
	if (state.pairs) {
		for (j = 0; j < npairs; ++j) {
			if (state.pairs[j].producer)
				bufferevent_free(state.pairs[j].producer);
			if (state.pairs[j].consumer)
				bufferevent_free(state.pairs[j].consumer);
		}
		free(state.pairs);
	}
	if (cfg)
		event_config_free(cfg);
	if (base)
		event_base_free(base);
	free(payload);
	return 1;
}
