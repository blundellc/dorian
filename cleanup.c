#include "common.c"

static void
error(const char *msg)
{
	err(1, "oops: %s", msg);
}

int
main()
{
	id_chain_t chain;
	db_t db;
	time_t create_time, last_seen;

	chain = id_chain_db();
	db = open_db(false);
	{
		for_each_id(id, chain) {
			if (!seen(id, &create_time, &last_seen))
				continue;

			if (time(NULL)-last_seen > timeout_record)
				if (gdbm_delete(db, id) < 0)
					error("failed to delete");
		}
	}
	close_db(&db);
	id_chain_free(&chain);
}
