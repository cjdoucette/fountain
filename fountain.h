#ifndef _FOUNTAIN_H
#define _FOUNTAIN_H

#include <stdio.h>
#include <sys/socket.h>
#include <xia_socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define UNUSED(x) (void)x

#define DATA_FILES_PER_BLOCK		10
#define CODE_FILES_PER_BLOCK		10
#define CHUNK_SIZE			384
#define FILENAME_MAX_LEN		18

struct fountain_hdr {
	__u32	num_blocks;
	__u32	block_id;
	__s16	chunk_id;
	__u16	packet_len;
	__u16	padding;
	char	filename[18];
	__u8	data[0];
};


int dir_exists(const char *filename);
int file_exists(const char *filename);
int num_digits(int num);

/* The best appoach would be to use struct __kernel_sockaddr_storage
 * defined in <linux/socket.h>, or struct sockaddr_storage defined in libc.
 * However, while XIA doesn't make into mainline, these structs are only
 * half of the size needed.
 */
struct tmp_sockaddr_storage {
	char memory[256];
};

struct sockaddr *get_addr(char *str1, int *plen);

void send_packet(int s, const char *buf, int n, const struct sockaddr *dst,
	socklen_t dst_len);

xid_type_t get_xdp_type(void);

int address_match(const struct sockaddr *addr, socklen_t addr_len,
		  const struct sockaddr *expected, socklen_t exp_len);

#endif /* _FOUNTAIN_H */
