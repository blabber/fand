PROG=		fand

LDADD=		-lutil

WARNS?=		6
WFORMAT?=	1
CSTD=		c99

NO_MAN=		YES

.include <bsd.prog.mk>
