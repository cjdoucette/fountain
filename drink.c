#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "fountain.h"

#define CHUNK_SIZE	384
#define DECODED_DIR	"decoded"

#define USAGE	"usage:\t./drink cli_addr_file srv_addr_file req_file\n"

static int check_cli_params(int argc, char * const argv[])
{
	UNUSED(argv);
	if (argc != 4) {
		printf(USAGE);
		return 1;
	}
	return 0;
}

void send_req(int s, const struct sockaddr *srv, socklen_t srv_len,
	      const char *req_file)
{
	ssize_t rc = sendto(s, req_file, strlen(req_file), 0, srv, srv_len);
	if (rc < 0) {
		fprintf(stderr, "%s: sendto errno=%i: %s\n",
			__func__, errno, strerror(errno));
		exit(1);
	}
}

void create_block_dirs(const char *req_file, __u32 num_blocks)
{
	char *block_path;
	int alloc_len, len, rc;
	__u32 i;
	int n_digits = num_digits(num_blocks - 1);

	/* asprintf returns the number of bytes printed, not the
	 * number of bytes allocated, so we must add one for the
	 * terminating null byte.
	 */
	len = asprintf(&block_path, "%s/%s/b%0*d", DECODED_DIR,
			     req_file, n_digits, 0);
	if (len == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate file path string\n");
		return;
	}
	alloc_len = len + 1;
	rc = mkdir(block_path, 0777);
	if (rc < 0) {
		fprintf(stderr, "%s: mkdir errno=%i on %s: %s\n",
			__func__, errno, block_path, strerror(errno));
		exit(1);
	}

	for (i = 1; i < num_blocks; i++) {
		/* Use same buffer up to @alloc_len bytes. We
		 * will never need more space than that.
		 */
		len = snprintf(block_path, alloc_len, "%s/%s/b%0*d",
				DECODED_DIR, req_file, n_digits, i);
		if (len < 0) {
			fprintf(stderr,
				"snprintf: cannot allocate file path\n");
			exit(1);
		}
		rc = mkdir(block_path, 0777);
		if (rc < 0) {
			fprintf(stderr, "%s: mkdir errno=%i on %s: %s\n",
				__func__, errno, block_path, strerror(errno));
			exit(1);
		}
	}
}

void recv_file(int s, const struct sockaddr *expected_srv,
	       socklen_t exp_srv_len, const char *req_file)
{
	struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
	fd_set readfds;
	struct tmp_sockaddr_storage srv;
	struct fountain_hdr *fountain_pkt;
	char *recv_file_path, *meta_file_path, *padding_file_path;
	FILE *recv_file, *meta_file, *padding_file;
	socklen_t srv_len;
	unsigned int pkt_len, num_read;
	int rc, len, alloc_len, meta_len, meta_alloc_len, padding_len;
	__u16 *num_recv_in_block;
	__u32 blocks_filled = 0;
	int n_recvd = 0;

	__u32 num_blocks;
	__u32 block_id;
	__s16 chunk_id;
	__u16 chunk_id_abs;
	__u16 packet_len;
	__u16 padding;

	/* Is there anything to read? */
	FD_ZERO(&readfds);
	FD_SET(s, &readfds);
	rc = select(s + 1, &readfds, NULL, NULL, &timeout);
	assert(rc >= 0);
	if (!rc) {
		/* No response from server. */
		fprintf(stderr, "No response from server.");
		return;
	}

	/* Read how many bytes are in the packet. */
	pkt_len = recvfrom(s, NULL, 0, MSG_PEEK | MSG_TRUNC, NULL, NULL);
	assert(pkt_len > sizeof(*fountain_pkt));

	/* Allocate a buffer for those bytes and read in the packet. */
	fountain_pkt = alloca(pkt_len);
	srv_len = sizeof(srv);
	num_read = recvfrom(s, fountain_pkt, pkt_len, 0,
			    (struct sockaddr *)&srv, &srv_len);
	assert(pkt_len == num_read);
	n_recvd++;

	/* Make sure that we're reading from the server. */
	assert(address_match((struct sockaddr *)&srv, srv_len,
		expected_srv, exp_srv_len));

	num_blocks = ntohl(fountain_pkt->num_blocks);
	block_id = ntohl(fountain_pkt->block_id);
	chunk_id = ntohs(fountain_pkt->chunk_id);
	chunk_id_abs = chunk_id < 0 ? -chunk_id : chunk_id;
	packet_len = ntohs(fountain_pkt->packet_len);
	padding = ntohs(fountain_pkt->padding);

	create_block_dirs(req_file, num_blocks);
	num_recv_in_block = malloc(num_blocks * sizeof(*num_recv_in_block));

	padding_len = asprintf(&padding_file_path, "%s/%s/padding.txt",
		DECODED_DIR, req_file);
	if (padding_len == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate padding file path\n");
		return;
	}
	padding_file = fopen(padding_file_path, "wb");
	assert(padding_file);
	fprintf(padding_file, "%hd", padding);
	fclose(padding_file);

	len = asprintf(&recv_file_path, "%s/%s/b%0*d/%s%0*d", DECODED_DIR,
		       req_file, num_digits(num_blocks - 1), block_id,
		       chunk_id > 0 ? "k" : "m",
		       chunk_id > 0 ? num_digits(DATA_FILES_PER_BLOCK) :
				      num_digits(CODE_FILES_PER_BLOCK),
		       chunk_id_abs);
	if (len == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate received file path\n");
		return;
	}
	alloc_len = len + 1;

	recv_file = fopen(recv_file_path, "wb");
	assert(recv_file);
	fwrite(fountain_pkt->data, sizeof(char),
	       packet_len - sizeof(struct fountain_hdr), recv_file);
	fclose(recv_file);
	num_recv_in_block[block_id]++;
	if (num_recv_in_block[block_id] == DATA_FILES_PER_BLOCK)
		blocks_filled++;

	meta_len = asprintf(&meta_file_path, "%s/%s/b%0*d/b%0*d_meta.txt",
			    DECODED_DIR, req_file,
			    num_digits(num_blocks - 1), block_id,
			    num_digits(num_blocks - 1), block_id);
	meta_alloc_len = meta_len + 1;

	meta_file = fopen(meta_file_path, "wb");
	assert(meta_file);
	fprintf(meta_file, "%s/%s/b%0*d\n%lu\n%d %d %d %d %lu\n%s\n%d\n%d\n",
		DECODED_DIR, req_file, num_digits(num_blocks - 1), block_id,
		(packet_len - sizeof(struct fountain_hdr)) *
		DATA_FILES_PER_BLOCK, DATA_FILES_PER_BLOCK,
		CODE_FILES_PER_BLOCK, 8, 1,
		(packet_len - sizeof(struct fountain_hdr)) *
		DATA_FILES_PER_BLOCK, "cauchy_good", 3, 1);
	fclose(meta_file);

	while (blocks_filled < num_blocks) {
		FD_ZERO(&readfds);
		FD_SET(s, &readfds);
		rc = select(s + 1, &readfds, NULL, NULL, &timeout);
		assert(rc >= 0);
		if (!rc) {
			/* No response from server. */
			fprintf(stderr, "Done receiving packets. Decoding.\n");
			return;
		}

		/* Read how many bytes are in the packet. */
		pkt_len = recvfrom(s, NULL, 0, MSG_PEEK|MSG_TRUNC, NULL, NULL);
		assert(pkt_len > sizeof(*fountain_pkt));
		n_recvd++;

		/* Allocate a buffer for those bytes and read in the packet. */
		fountain_pkt = alloca(pkt_len);
		srv_len = sizeof(srv);
		num_read = recvfrom(s, fountain_pkt, pkt_len, 0,
			    (struct sockaddr *)&srv, &srv_len);
		assert(pkt_len == num_read);

		/* Make sure that we're reading from the server. */
		assert(address_match((struct sockaddr *)&srv, srv_len,
			expected_srv, exp_srv_len));

		assert(ntohl(fountain_pkt->num_blocks) == num_blocks);
		block_id = ntohl(fountain_pkt->block_id);
		chunk_id = ntohs(fountain_pkt->chunk_id);
		chunk_id_abs = chunk_id < 0 ? -chunk_id : chunk_id;
		packet_len = ntohs(fountain_pkt->packet_len);
		assert(padding = ntohs(fountain_pkt->padding));

		/* Use same buffer up to @alloc_len bytes. We
		 * will never need more space than that.
		 */
		len = snprintf(recv_file_path, alloc_len, "%s/%s/b%0*d/%s%0*d",
			       DECODED_DIR, req_file,
			       num_digits(num_blocks - 1), block_id,
			       chunk_id > 0 ? "k" : "m",
			       chunk_id > 0 ? num_digits(DATA_FILES_PER_BLOCK) :
					      num_digits(CODE_FILES_PER_BLOCK),
			       chunk_id_abs);
		if (len < 0) {
			fprintf(stderr,
				"snprintf: cannot allocate file path\n");
			exit(1);
		}

		recv_file = fopen(recv_file_path, "wb");
		assert(recv_file);
		fwrite(fountain_pkt->data, sizeof(char),
		       packet_len - sizeof(struct fountain_hdr), recv_file);
		fclose(recv_file);
		num_recv_in_block[block_id]++;
		if (num_recv_in_block[block_id] == DATA_FILES_PER_BLOCK)
			blocks_filled++;

		meta_len = snprintf(meta_file_path, meta_alloc_len,
				"%s/%s/b%0*d/b%0*d_meta.txt",
				DECODED_DIR, req_file,
				num_digits(num_blocks - 1), block_id,
				num_digits(num_blocks - 1), block_id);
		if (meta_len < 0) {
			fprintf(stderr,
				"snprintf: cannot allocate meta file path\n");
			exit(1);
		}

		meta_file = fopen(meta_file_path, "wb");
		assert(meta_file);
		fprintf(meta_file,
			"%s/%s/b%0*d\n%lu\n%d %d %d %d %lu\n%s\n%d\n%d\n",
			DECODED_DIR, req_file, num_digits(num_blocks - 1),
			block_id,
			(packet_len - sizeof(struct fountain_hdr)) *
			DATA_FILES_PER_BLOCK, DATA_FILES_PER_BLOCK,
			CODE_FILES_PER_BLOCK, 8, 1,
			(packet_len - sizeof(struct fountain_hdr)) *
			DATA_FILES_PER_BLOCK, "cauchy_good", 3, 1);
		fclose(meta_file);
	}
}

int main(int argc, char *argv[])
{
	struct sockaddr *cli, *srv;
	char *recv_file_path;
	int s, cli_len, srv_len, size, rc;

	if (check_cli_params(argc, argv))
		exit(1);

	size = asprintf(&recv_file_path, "%s/%s", DECODED_DIR, argv[3]);
	if (size == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate file path string\n");
		exit(1);
	}

        /* Check if a file for that name exists. */
        if (dir_exists(recv_file_path)) {
		fprintf(stderr, "directory %s already exists\n",
			recv_file_path);
		exit(1);
	} else {
		rc = mkdir(recv_file_path, 0777);
		if (rc < 0) {
			fprintf(stderr, "%s: mkdir errno=%i on %s: %s\n",
				__func__, errno, recv_file_path, strerror(errno));
			exit(1);
		}
	}

	s = socket(AF_XIA, SOCK_DGRAM, get_xdp_type());
	assert(s >= 0);
	cli = __get_addr(argv[1], &cli_len);
	assert(cli);
	srv = __get_addr(argv[2], &srv_len);
	assert(srv);
	assert(!bind(s, cli, cli_len));

	send_req(s, srv, srv_len, argv[3]);
	recv_file(s, srv, srv_len, argv[3]);

	free(srv);
	free(cli);
	assert(!close(s));
	return 0;
}
