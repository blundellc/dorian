#define _GNU_SOURCE
#include <sys/time.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <err.h>

#include <gdbm.h>
#include <syslog.h>

static void error(const char *);

#define nelem(a) (sizeof(a)/sizeof((a)[0]))
#define for_eachn(i,n) for(ssize_t i = 0; i < (n); i++)
#define for_each(i,a) for_eachn(i, nelem(a))

typedef GDBM_FILE db_t;

enum {
	max_alloc_buf  = 1024,
	timeout_send   = 300,
	timeout_record = 30*24*60*60
};

static const char *keys[] = {"recipient","sender","client_address","request","helo_name","client_name"};
enum {
	Krecipient = 0,
	Ksender = 1,
	Kclient_addr = 2,
	Krequest = 3,
	Khelo_name = 4,
	Kclient_name = 5,
	Knum = 6
};

static const int db_fields[] = {Kclient_addr, Krecipient, Ksender };

static const char *db_filename = "/var/lib/dorian/db";

typedef struct {
	char *attr[Knum];
} *session_t;

static bool
exists_db(void)
{
	struct stat buf;
	return stat(db_filename, &buf) == 0;
}

static db_t
open_db(bool readonly)
{
	db_t db;

	for_eachn(retry, 5) {
		db = gdbm_open(db_filename, 1024, (readonly? GDBM_READER: GDBM_WRCREAT), 0600, error);
		if (db != NULL)
			break;
		/* TODO: random time */
		(void)sleep(1);
	}
	if (db == NULL)
		error("could not open database");
	return db;
}

static void
close_db(db_t *db)
{
	gdbm_close(*db);
	*db = NULL;
}

static bool
seen(datum id, time_t *create_time, time_t *last_seen) 
{
	db_t db;
	datum lookup;
	intmax_t s, t;

	if (!exists_db())
		return false;

	db = open_db(true);
	{
		lookup = gdbm_fetch(db, id);
	}
	close_db(&db);

	if (lookup.dptr != NULL) {
		if (lookup.dptr[lookup.dsize-1] != '\0') goto err;
		if (sscanf(lookup.dptr, "%jd %jd", &s, &t) != 2) goto err;

		*create_time = (time_t)s;
		*last_seen = (time_t)t;
		free(lookup.dptr);
		return true;
err:
		/* uh oh... */
		free(lookup.dptr);
		return false;
	}
	return false;
}


static void
bump(datum id, time_t create_time)
{
	db_t db;
	datum tim;
	int len;

	len = asprintf(&tim.dptr, "%jd %jd", (intmax_t)create_time, (intmax_t)time(NULL));
	if (len <= 0)
		error("no memory");
	tim.dsize = len+1;

	db = open_db(false);
	{
		(void)gdbm_store(db, id, tim, GDBM_REPLACE);
	}
	close_db(&db);
	free(tim.dptr);
}

static void
sess_derive_key(session_t sess, datum *id)
{
	char *p;

	id->dsize = 0;
	for_each(i, db_fields) {
		if (sess->attr[db_fields[i]] == NULL)
			error("missing field");
		id->dsize += strlen(sess->attr[db_fields[i]])+1;
	}

	if (id->dsize <= 0 || id->dsize > max_alloc_buf) 
		error("buffer size problem");

	if ((id->dptr = malloc(id->dsize)) == NULL)
		error("no memory");

	p = id->dptr;
	for_each(i, db_fields) {
		p[0] = '\0';
		strcat(p, sess->attr[db_fields[i]]);
		p += strlen(p)+1;
	}
}

static void
sess_free(session_t *sess)
{
	for_each(i, (*sess)->attr)
		free((*sess)->attr[i]);
	free(*sess);
	*sess = NULL;
}


static session_t
sess_req(FILE *fp)
{
	session_t sess;
	char *line, *p, *q;
	size_t len;
	ssize_t n;

	line = NULL;
	len = 0;
	if ((sess = malloc(sizeof(*sess))) == NULL)
		error("no memory");
	assert(nelem(keys) == nelem(sess->attr));

	for_each(i, sess->attr)
		sess->attr[i] = NULL;

	/* TODO: timeout? */
	while ((n = getline(&line, &len, fp)) > 0) {
		line[n-1] = '\0';
		if (strlen(line) == 0)
			/* we're done with this request */
			break;
		q = line;
		p = strsep(&q, "=");
		if (q == NULL)
			continue;
		for_each(i, keys)
			if (!strcmp(p,keys[i])) {
				if (sess->attr[i] == NULL) {
					/* TODO: max_alloc_buf? */
					sess->attr[i] = strdup(q);
				}
				break;
			}
	}
	free(line);
	if (n < 0) {
		sess_free(&sess);
		return NULL;
	}
	return sess;
}

static void add(datum id) { bump(id, time(NULL)); }


typedef struct _id_chain_t {
	struct _id_chain_t *next;
	datum id;
} *id_chain_t;

static id_chain_t
id_chain_db(void)
{
	db_t db;
	id_chain_t head, chain;

	chain = NULL;

	if (!exists_db())
		return chain;

	db = open_db(true);
	{
		for (datum id = gdbm_firstkey(db); id.dptr != NULL; id = gdbm_nextkey(db,id)) {
			assert(id.dsize > 0);
			assert(id.dptr[id.dsize-1] == '\0');
			if ((head = malloc(sizeof(*head))) == NULL)
				error("no memory");
			head->next = chain;
			head->id = id;
			chain = head;
		}
	}
	close_db(&db);
	return chain;
}

/* TODO: a nicer way of doing this */
#define for_each_id(var, chain) for (bool _id_trick_##var = true; _id_trick_##var;) \
				for (datum var={0,0};_id_trick_##var;_id_trick_##var=false) \
				for (id_chain_t _id_ptr_##var = chain; \
				_id_ptr_##var != NULL && (var = _id_ptr_##var->id).dptr != NULL; \
				_id_ptr_##var = _id_ptr_##var->next)

static void
id_chain_free(id_chain_t *chain)
{
	id_chain_t next;

	while (*chain != NULL) {
		next = (*chain)->next;
		free((*chain)->id.dptr);
		free(*chain);
		*chain = next;
	}
}

