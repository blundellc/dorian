#include "common.c"
#include <sys/socket.h>
#include <netinet/in.h>
#include <spf2/spf.h>

static void
error(const char *str)
{
	if (errno != 0) {
		syslog(LOG_ERR, "error: %s: %m", str);
	} else {
		syslog(LOG_ERR, "error: %s", str);
	}
	printf("action=warn %s\n\n", str);
	fflush(stdout);
	exit(1);
}

static void
exempt(const char *reason)
{
	syslog(LOG_NOTICE, "skipped mail; %s", reason);
	printf("action=prepend X-Comment: SPF skipped; %s\n\n", reason);
	fflush(stdout);
}

static void
reject(const char *spf_exp)
{
	syslog(LOG_NOTICE, "reject mail; %s", spf_exp);
	printf("action=550 %s\n\n", spf_exp);
	fflush(stdout);
}


static void
defer(const char *spf_exp)
{
	syslog(LOG_NOTICE, "deferred mail; %s", spf_exp);
	printf("action=defer_if_permit SPF-Result=%s\n\n", spf_exp);
	fflush(stdout);
}

static void
allow(const char *spf_hdr)
{
	syslog(LOG_NOTICE, "accept mail; %s", spf_hdr);
	printf("action=prepend %s\n\n", spf_hdr);
	fflush(stdout);
}


int
main(int c, char *v[])
{
	/*
	 * based on Dean Strik's postfix patch, which in turn is partly based on
	 * Jef Poskanzer's spfmilter
	 * TODO: local policy, explanation, 2mx stuff
	 * NOTE: a resolver cache is not used as i run a local dns cache.
	 */
	session_t sess;
	SPF_server_t *spf;
	SPF_request_t *spf_req;
	SPF_response_t *spf_response;
	SPF_errcode_t err;
	const char *spf_hdr, *spf_exp, *hostname;

	openlog("dorian/spf", LOG_PID, LOG_MAIL);

	if (c < 2)
		error("first parameter is hostname");

	hostname = v[1];
	if ((spf = SPF_server_new(0,0)) == NULL)
		error("unable to config SPF server");
	SPF_server_set_rec_dom(spf, hostname);
	SPF_server_set_sanitize(spf, 1);
	
	for (;;) {
		if ((sess = sess_req(stdin)) == NULL)
			break;
		spf_req = SPF_request_new(spf);
		{
			if ((err = SPF_request_set_ipv4_str(spf_req, sess->attr[Kclient_addr])))
				goto spf_error;
			if ((err = SPF_request_set_helo_dom(spf_req, sess->attr[Khelo_name])))
				goto spf_error;
			if ((err = SPF_request_set_env_from(spf_req, sess->attr[Ksender])))
				goto spf_error;
			spf_response = NULL;
			if ((err = SPF_request_query_mailfrom(spf_req, &spf_response)))
				goto spf_error;
			spf_hdr = SPF_response_get_received_spf(spf_response);
			spf_exp = SPF_response_get_smtp_comment(spf_response);
			switch (SPF_response_result(spf_response)) {
			case SPF_RESULT_PASS:
			case SPF_RESULT_SOFTFAIL:
			case SPF_RESULT_NEUTRAL:
			case SPF_RESULT_NONE:
			case SPF_RESULT_PERMERROR:
			case SPF_RESULT_INVALID:
						   allow(spf_hdr); break;

			case SPF_RESULT_FAIL:      reject(spf_exp); break;
			case SPF_RESULT_TEMPERROR: defer(spf_exp);  break;
			}
			if (0) {
spf_error:
				exempt(SPF_strerror(err));
			}
			SPF_response_free(spf_response);
		}
		SPF_request_free(spf_req);
		sess_free(&sess);
	}
	SPF_server_free(spf);
	return 0;
}
