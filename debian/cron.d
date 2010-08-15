#
# Regular cron jobs for the dorian package
#
16 18	8 * *	dorian	test -x /usr/bin/dorian-cleanup && /usr/bin/dorian-cleanup
