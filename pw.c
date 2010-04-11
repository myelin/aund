/*
 * Password file management for aund.
 */

#define _XOPEN_SOURCE 500	       /* for crypt and strdup */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "extern.h"

char *pwfile = NULL;
char *pwtmp = NULL; 
static int pwline;
static FILE *fp, *newfp;

extern int default_opt4;

int
pw_open(int write)
{

	assert(pwfile);		       /* shouldn't even be called otherwise */

	fp = fopen(pwfile, "r");
	if (!fp) {
		warn("%s: open", pwfile);
		newfp = NULL;
		return 0;
	}

	if (write) {
		int newfd;
		if (!pwtmp) {
			pwtmp = malloc(strlen(pwfile) + 10);
			sprintf(pwtmp, "%s.tmp", pwfile);
		}
		newfd = open(pwtmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (newfd < 0) {
			warn("%s: open", pwtmp);
			fclose(fp);
			fp = NULL;
			return 0;
		}
		newfp = fdopen(newfd, "w");
		if (!newfp) {
			warn("%s: fdopen", pwtmp);
			fclose(fp);
			fp = NULL;
			return 0;
		}
	} else {
		newfp = NULL;
	}

	pwline = 0;
	return 1;
}

void
pw_close(void)
{

	fclose(fp);
	if (newfp)
		fclose(newfp);
	fp = newfp = NULL;
}

int
pw_close_rename(void)
{

	fclose(fp);
	fp = NULL;
	if (newfp) {
		fclose(newfp);
		newfp = NULL;
		if (rename(pwtmp, pwfile) < 0) {
			warn("%s -> %s: rename", pwfile, pwtmp);
			return 0;
		}
		return 1;
	}
}

int
pw_read_line(char **user, char **pw, char **urd, int *opt4)
{
	static char buffer[16384];
	char *p, *q, *r;

	if (!fgets(buffer, sizeof(buffer), fp))
		return 0;
	pwline++;

	buffer[strcspn(buffer, "\r\n")] = '\0';
	if ((p = strchr(buffer, ':')) == NULL ||
	    (q = strchr(p+1, ':')) == NULL) {
		warnx("%s:%d: malformatted line\n", pwfile, pwline);
		return 0;
	}

	*p++ = '\0';
	*q++ = '\0';

	r = strchr(q, ':');
	if (r) {
		*r++ = '\0';
		*opt4 = atoi(r);
	} else {
		*opt4 = default_opt4;
	}

	*user = buffer;
	*pw = p;
	*urd = q;

	return 1;
}

void
pw_write_line(char *user, char *pw, char *urd, int opt4)
{

	fprintf(newfp, "%s:%s:%s:%d\n", user, pw, urd, opt4);
}

char *
pw_validate(char *user, const char *pw, int *opt4)
{
	char *u, *p, *d;
	char *ret;

	if (!pw_open(0))
		return;

	while (pw_read_line(&u, &p, &d, opt4)) {
		if (!strcasecmp(user, u)) {
			int ok = 0;
			char *ret;
			if (*p) {
				char *cp = crypt(pw, p);
				ok = !strcmp(cp, p);
			} else {
				ok = (!pw || !*pw);
			}
			if (!ok)
				ret = NULL;
			else
				ret = strdup(d);
			strcpy(user, u);   /* normalise case */
			pw_close();
			return ret;
		}
	}

	pw_close();
	return NULL;
}

int
pw_change(const char *user, const char *oldpw, const char *newpw)
{
	char *u, *p, *d;
	int opt4;
	int done = 0;

	if (!pw_open(1))
		return;

	while (pw_read_line(&u, &p, &d, &opt4)) {
		if (!done && !strcasecmp(user, u)) {
			int ok = 0;
			char *ret;
			char salt[64];
			char *cp;
			struct timeval tv;
			if (*p) {
				cp = crypt(oldpw, p);
				ok = !strcmp(cp, p);
			} else {
				ok = (!oldpw || !*oldpw);
			}
			if (!ok) {
				pw_close();
				return 0;
			}
			gettimeofday(&tv, NULL);
			sprintf(salt, "$6$%08x%08x$",
				tv.tv_sec & 0xFFFFFFFFUL,
				tv.tv_usec & 0xFFFFFFFFUL);
			p = crypt(newpw, salt);
		}
		pw_write_line(u, p, d, opt4);
	}

	return pw_close_rename();
}

int
pw_set_opt4(const char *user, int newopt4)
{
	char *u, *p, *d;
	int opt4;
	int done = 0;

	if (!pw_open(1))
		return;

	while (pw_read_line(&u, &p, &d, &opt4)) {
		if (!done && !strcasecmp(user, u)) {
		    opt4 = newopt4;
		}
		pw_write_line(u, p, d, opt4);
	}

	return pw_close_rename();
}
