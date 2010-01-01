#include "common.c"

static void
error(const char *msg)
{
	err(1, "oops: %s", msg);
}

int
main()
{
	int size[nelem(db_fields)], sz;
	id_chain_t chain;
	time_t create_time, last_seen;
	char *p;

	chain = id_chain_db();
	{
		for_each(i, db_fields)
			size[i] = strlen(keys[db_fields[i]]);
		for_each_id(id, chain) {
			assert(id.dsize > 0);
			assert(id.dptr[id.dsize-1] == '\0');
			p = id.dptr;
			for_each(i, db_fields) {
				assert(p < &id.dptr[id.dsize]);
				sz = strlen(p);
				if (sz > size[i])
					size[i] = sz;
				p += sz+1;
			}
		}

		for_each(i, db_fields)
			printf("%-*s ", size[i], keys[db_fields[i]]);
		printf("%-20s %-20s\n", "created", "last seen");

		for_each_id(id, chain) {
			p = id.dptr;
			for_each(i, db_fields) {
				assert(p < &id.dptr[id.dsize]);
				printf("%-*s ", size[i], p);
				p += strlen(p)+1;
			}
			if (seen(id, &create_time, &last_seen))
				printf("%-20jd %-20jd", (intmax_t)create_time, (intmax_t)last_seen);
			puts("");
		}
	}
	id_chain_free(&chain);
	return 0;
}
