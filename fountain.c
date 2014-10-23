#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include "fountain.h"

static int ppal_map_loaded = 0;

int dir_exists(const char *filename)
{
	struct stat st;
	if (stat(filename, &st) == -1) {
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "Error %d using stat on %s: %s\n",
			errno, filename, strerror(errno));
		exit(1);
	}
	return S_ISDIR(st.st_mode);
}

int num_digits(int num)
{
	char snum[100];    
	return sprintf(snum, "%d", num);
}

static inline void load_ppal_map(void)
{
	if (ppal_map_loaded)
		return;
	assert(!init_ppal_map(NULL));
	ppal_map_loaded = 1;
}

static xid_type_t xidtype_xdp = XIDTYPE_NAT;

xid_type_t get_xdp_type(void)
{
	if (xidtype_xdp != XIDTYPE_NAT)
		return xidtype_xdp;

	load_ppal_map();
	assert(!ppal_name_to_type("xdp", &xidtype_xdp));
	return xidtype_xdp;
}

/* XXX This function was copied from xiaconf/xip/xiphid.c.
 * It should go to a library!
 */
static int parse_and_validate_addr(char *str, struct xia_addr *addr)
{
	int invalid_flag;
	int rc;

	rc = xia_pton(str, INT_MAX, addr, 0, &invalid_flag);
	if (rc < 0) {
		fprintf(stderr, "Syntax error: invalid address: [[%s]]\n", str);
		return rc;
	}
	rc = xia_test_addr(addr);
	if (rc < 0) {
		char buf[XIA_MAX_STRADDR_SIZE];
		assert(xia_ntop(addr, buf, XIA_MAX_STRADDR_SIZE, 1) >= 0);
		fprintf(stderr, "Invalid address (%i): [[%s]] "
			"as seen by xia_xidtop: [[%s]]\n", -rc, str, buf);
		return rc;
	}
	if (invalid_flag) {
		fprintf(stderr, "Although valid, address has invalid flag: "
			"[[%s]]\n", str);
		return -1;
	}
	return 0;
}

static int set_sockaddr_xia(struct sockaddr_xia *xia, const char *filename)
{
#define BUFSIZE (4 * 1024)
	FILE *f;
	char buf[BUFSIZE];
	int len;

	load_ppal_map();

	/* Read address. */
	f = fopen(filename, "r");
	if (!f) {
		perror(__func__);
		return errno;
	}
	len = fread(buf, 1, BUFSIZE, f);
	assert(len < BUFSIZE);
	fclose(f);
	buf[len] = '\0';

	xia->sxia_family = AF_XIA;
	return parse_and_validate_addr(buf, &xia->sxia_addr);
}

struct sockaddr *get_addr(char *str1, int *plen)
{
	struct tmp_sockaddr_storage *skaddr;

	skaddr = malloc(sizeof(*skaddr));
	assert(skaddr);
	memset(skaddr, 0, sizeof(*skaddr));

	struct sockaddr_xia *xia = (struct sockaddr_xia *)skaddr;
	/* XXX It should be a BUILD_BUG_ON(). */
	assert(sizeof(*skaddr) >= sizeof(*xia));
	assert(!set_sockaddr_xia(xia, str1));
	*plen = sizeof(*xia);

	return (struct sockaddr *)skaddr;
}

static int count_rows(const struct sockaddr_xia *xia)
{
	int i;
	for (i = 0; i < XIA_NODES_MAX; i++)
		if (xia_is_nat(xia->sxia_addr.s_row[i].s_xid.xid_type))
			break;
	return i;
}

int address_match(const struct sockaddr *addr, socklen_t addr_len,
		  const struct sockaddr *expected, socklen_t exp_len)
{
	assert(addr->sa_family == expected->sa_family);

	switch (addr->sa_family) {
	case AF_INET:
		return addr_len == exp_len && !memcmp(addr, expected, addr_len);

	case AF_XIA: {
		struct sockaddr_xia *addr_xia = (struct sockaddr_xia *)addr;
		struct sockaddr_xia *exp_xia = (struct sockaddr_xia *)expected;
		int addr_n = count_rows(addr_xia);
		int exp_n = count_rows(exp_xia);

		assert(addr_n > 0 && exp_n > 0);

		return !memcmp(&addr_xia->sxia_addr.s_row[addr_n - 1].s_xid,
			&exp_xia->sxia_addr.s_row[exp_n - 1].s_xid,
			sizeof(addr_xia->sxia_addr.s_row[0].s_xid));
		break;
	}

	default:
		assert(0);
	}
}


