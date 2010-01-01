#include "common.c"

static void
error(const char *str)
{
	if (errno != 0) {
		syslog(LOG_ERR, "error: %s: %m", str);
	} else {
		syslog(LOG_ERR, "error: %s", str);
	}
	printf("action=warn %s\n\n", str); exit(1);
}

static void
defer(void)
{
	syslog(LOG_NOTICE, "defered mail");
	printf("action=defer_if_permit dorian asks: please try again in %ds.\n\n", timeout_send);
	fflush(stdout);
}

static void
allow(const char *reason)
{
	syslog(LOG_NOTICE, "allowed mail; %s", reason);
	printf("action=prepend X-Greylist: dorian says: ok [%s]\n\n", reason);
	fflush(stdout);
}


static time_t
expired(void) {
	return time(NULL) - timeout_send;
}


static const char *whitelist_prefix[] = { "abuse@", "postmaster@" };


int
main()
{
	session_t sess;
	datum id;
	time_t create_time, last_seen;
	bool do_allow;
	char *reason;

	openlog("dorian/query", LOG_PID, LOG_MAIL);
	for (;;) {
		do_allow = false;
		if ((sess = sess_req(stdin)) == NULL)
			break;

		/* white listing */
		for_each(i, whitelist_prefix)
			if (!strncasecmp(whitelist_prefix[i], sess->attr[Krecipient],
						strlen(whitelist_prefix[i]))) {
				do_allow = true;
				reason = "whitelist address";
				break;
			}

		if (!do_allow) {
			sess_derive_key(sess, &id);
			{
				if (seen(id, &create_time, &last_seen)) {
					if (create_time < expired()) {
						bump(id, create_time);
						do_allow = true;
						reason = "in database";
					}
				} else
					add(id);
			}
			free(id.dptr);
		}
		sess_free(&sess);
		if (do_allow)
			allow(reason);
		else
			defer();
	}
	closelog();
	return 0;
}

