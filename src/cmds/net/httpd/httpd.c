/**
 * @file
 * @brief Simple HTTP server
 * @date 16.04.12
 * @author Ilia Vaprol
 * @author Anton Kozlov
 * 	- CGI related changes
 * @author Andrey Golikov
 * 	- Linux adaptation
 */

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <util/log.h>

#include "httpd.h"

#ifdef __EMBUILD_MOD__
#	include <framework/mod/options.h>
#	define USE_IP_VER       OPTION_GET(NUMBER,use_ip_ver)
#	define USE_CGI          OPTION_GET(BOOLEAN,use_cgi)
#	define USE_REAL_CMD     OPTION_GET(BOOLEAN,use_real_cmd)
#	define USE_PARALLEL_CGI OPTION_GET(BOOLEAN,use_parallel_cgi)
#endif /* __EMBUILD_MOD__ */

#define BUFF_SZ     1024

static char httpd_g_inbuf[BUFF_SZ];

static int httpd_read_http_header(const struct client_info *cinfo, char *buf, size_t buf_sz) {
	const int sk = cinfo->ci_sock;
	const char *pattern = "\r\n\r\n";
	char pattbuf[strlen("\r\n\r\n")];
	char *pb;

	pb = buf;
	if (0 > read(sk, pattbuf, sizeof(pattbuf))) {
		return -errno;
	}
	while (0 != strncmp(pattern, pattbuf, sizeof(pattbuf)) && buf_sz > 0) {
		*(pb++) = pattbuf[0];
		buf_sz--;
		memmove(pattbuf, pattbuf + 1, sizeof(pattbuf) - 1);
		if (0 > read(sk, &pattbuf[sizeof(pattbuf) - 1], 1)) {
			return -errno;
		}
	}

	if (buf_sz == 0) {
		return -ENOENT;
	}

	memcpy(pb, pattbuf, sizeof(pattbuf));
	return pb + sizeof(pattbuf) - buf;
}

static int httpd_header(const struct client_info *cinfo, int st, const char *msg) {
	FILE *skf = fdopen(cinfo->ci_sock, "rw");

	if (!skf) {
		log_error("can't allocate FILE for socket");
		return -ENOMEM;
	}

	fprintf(skf,
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Connection: close\r\n"
		"\r\n",
		st, msg, "text/plain");

	fclose(skf);
	return 0;
}

static int httpd_wait_cgi_child(pid_t target, int opts) {
	pid_t child;

	do {
		child = waitpid(target, NULL, opts);
	} while (child == -1 && errno == EINTR);

	if (child == -1) {
		int err = errno;
		log_error("waitpid() : %s", strerror(err));
		return -err;
	}

	return child;
}

static void httpd_on_cgi_child(const struct client_info *cinfo, pid_t child) {
	if (child > 0) {
	       if (!USE_PARALLEL_CGI) {
		       httpd_wait_cgi_child(child, 0);
	       }
	} else {
		httpd_header(cinfo, 500, strerror(-child));
	}
}

static void httpd_client_process(const struct client_info *cinfo) {
	struct http_req hreq;
	pid_t cgi_child;
	int ret;

	ret = httpd_read_http_header(cinfo, httpd_g_inbuf, sizeof(httpd_g_inbuf) - 1);
	if (ret < 0) {
		log_error("can't read from client socket: %s", strerror(errno));
		return;
	}
	httpd_g_inbuf[ret] = '\0';

	memset(&hreq, 0, sizeof(hreq));
	if (NULL == httpd_parse_request(httpd_g_inbuf, &hreq)) {
		log_error("can't parse request");
		return;
	}

	log_debug("method=%s uri_target=%s uri_query=%s",
			   hreq.method, hreq.uri.target, hreq.uri.query);

	if ((cgi_child = httpd_try_respond_script(cinfo, &hreq))) {
		httpd_on_cgi_child(cinfo, cgi_child);
	} else if (USE_REAL_CMD && (cgi_child = httpd_try_respond_cmd(cinfo, &hreq))) {
		httpd_on_cgi_child(cinfo, cgi_child);
	} else if (httpd_try_respond_file(cinfo, &hreq)) {
		/* file sent, nothing to do */
	} else {
		httpd_header(cinfo, 404, "");
	}
}

int main(int argc, char **argv) {
	int host;
	const char *basedir;
#if USE_IP_VER == 4
	struct sockaddr_in inaddr;
	const size_t inaddrlen = sizeof(inaddr);
	const int family = AF_INET;

	inaddr.sin_family = AF_INET;
	inaddr.sin_port= htons(80);
	inaddr.sin_addr.s_addr = htonl(INADDR_ANY);
#elif USE_IP_VER == 6
	struct sockaddr_in6 inaddr;
	const size_t inaddrlen = sizeof(inaddr);
	const int family = AF_INET6;

	inaddr.sin6_family = AF_INET6;
	inaddr.sin6_port= htons(80);
	memcpy(&inaddr.sin6_addr, &in6addr_any, sizeof(inaddr.sin6_addr));
#else
#error Unknown USE_IP_VER
#endif

	basedir = argc > 1 ? argv[1] : "/";

	host = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (host == -1) {
		log_error("socket() failure: %s", strerror(errno));
		return -errno;
	}

	if (-1 == bind(host, (struct sockaddr *) &inaddr, inaddrlen)) {
		log_error("bind() failure: %s", strerror(errno));
		close(host);
		return -errno;
	}

	if (-1 == listen(host, 3)) {
		log_error("listen() failure: %s", strerror(errno));
		close(host);
		return -errno;
	}

	while (1) {
		struct client_info ci;

		ci.ci_addrlen = inaddrlen;
		ci.ci_sock = accept(host, &ci.ci_addr, &ci.ci_addrlen);
		if (ci.ci_sock == -1) {
			if (errno != EINTR) {
				log_error("accept() failure: %s", strerror(errno));
				usleep(100000);
			}
			continue;
		}
		assert(ci.ci_addrlen == inaddrlen);
		ci.ci_basedir = basedir;

		if (USE_PARALLEL_CGI) {
			while (0 < httpd_wait_cgi_child(-1, WNOHANG)) {
				/* wait another one */
			}
		}

		httpd_client_process(&ci);

		close(ci.ci_sock);
	}

	close(host);

	return 0;
}