#ifndef AUND_BSD_LIBC_H
#define AUND_BSD_LIBC_H

void strmode(mode_t, char *);

const char *user_from_uid(uid_t, int);
const char *group_from_gid(gid_t, int);

#endif /* AUND_BSD_LIBC_H */
