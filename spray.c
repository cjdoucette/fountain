#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "fountain.h"

#define USAGE	"usage:\t./spray srv_addr_file coding_dir padding\n"

#define CODING_META_INFO_FILE_LEN	4
#define CHUNK_SIZE			384

static void send_file(int s, const struct sockaddr *cli, socklen_t cli_len,
		      __u32 num_blocks, __u32 block_id, __s16 chunk_id,
		      const char *chunk_path, __u16 padding)
{
	FILE *chunk;
	struct fountain_hdr *fountain;
	long file_size;
	size_t bytes_read;
	ssize_t rc;
	__u16 packet_len;

	chunk = fopen(chunk_path, "rb");
	assert(chunk);
	fseek(chunk, 0, SEEK_END);
	file_size = ftell(chunk);
	fseek(chunk, 0, SEEK_SET);

	packet_len = sizeof(*fountain) + file_size;
	fountain = alloca(packet_len);
	fountain->num_blocks = htonl(num_blocks);
	fountain->block_id = htonl(block_id);
	fountain->chunk_id = htons(chunk_id);
	fountain->packet_len = htons(packet_len);
	fountain->padding = htons(padding);

	bytes_read = fread(fountain->data, 1, file_size, chunk);
	assert(!ferror(chunk));
	assert(bytes_read <= CHUNK_SIZE);

	rc = sendto(s, fountain, packet_len, 0, cli, cli_len);
	if (rc < 0) {
		fprintf(stderr, "%s: sendto errno=%i: %s\n",
			__func__, errno, strerror(errno));
		fclose(chunk);
		exit(1);
	}
	fclose(chunk);
}

void spray(int s, const char *coding_dir, ssize_t req_len, __u16 padding)
{
	struct tmp_sockaddr_storage cli_stack;
	struct sockaddr *cli = (struct sockaddr *)&cli_stack;
	unsigned int cli_len = sizeof(cli_stack);

	char *req_file_path;
	char *req_meta_file_path;
	char *req_file;

	FILE *meta_file;
	int size;
	ssize_t num_read;
	__u32 num_blocks;
	__u32 j;
	__u16 i;

	/* Need to NUL terminate the requested file string. */
	req_file = alloca(req_len + 1);
	req_file[req_len] = '\0';
	num_read = recvfrom(s, req_file, req_len, 0, cli, &cli_len);
	assert(num_read == req_len);

	size = asprintf(&req_file_path, "%s/%s", coding_dir, req_file);
	if (size == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate file path string\n");
		return;
	}

	/* Check if a file for that name exists. */
	if (!dir_exists(req_file_path)) {
		fprintf(stderr, "invalid request; %s does not exist\n",
			req_file);
		return;
	}

	/* If it does exist, open the meta file to find the number of blocks. */
	size = asprintf(&req_meta_file_path, "%s/meta.txt", req_file_path);
	if (size == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate meta path string\n");
		return;
	}

	meta_file = fopen(req_meta_file_path, "r");
	if (!meta_file) {
		fprintf(stderr,
			"fopen: cannot open meta file\n");
		return;
	}
	/* Read in a single four-byte integer. */
	num_read = fscanf(meta_file, "%d", &num_blocks);
	/* Use a different variable. */
	if (num_read != 1) {
		fprintf(stderr,
			"fscanf: cannot read number of blocks\n");
		fclose(meta_file);
		return;
	}
	fclose(meta_file);

	/* Loop over all blocks. */
	for (i = 1; i <= DATA_FILES_PER_BLOCK; i++) {
		for (j = 0; j < num_blocks; j++) {
			char *chunk_path;
			size = asprintf(&chunk_path,
					"%s/b%0*d/k%0*d",
					req_file_path,
					num_digits(num_blocks - 1), j,
					num_digits(DATA_FILES_PER_BLOCK), i);
			if (size == -1) {
				fprintf(stderr,
					"asprintf: cannot allocate chunk path string\n");
				return;
			}
			usleep(100);
			send_file(s, cli, cli_len, num_blocks,
				  j, i, chunk_path, padding);
		}
	}

	for (i = 1; i <= CODE_FILES_PER_BLOCK; i++) {
		for (j = 0; j < num_blocks; j++) {
			char *chunk_path;
			size = asprintf(&chunk_path,
					"%s/b%0*d/m%0*d",
					req_file_path,
					num_digits(num_blocks - 1), j,
					num_digits(CODE_FILES_PER_BLOCK), i);
			if (size == -1) {
				fprintf(stderr,
					"asprintf: cannot allocate chunk path string\n");
				return;
			}
			usleep(100);
			send_file(s, cli, cli_len, num_blocks,
				  j, -i, chunk_path, padding);
		}
	}
}

static void recv_loop(int sock, const char *coding_dir_name, __u16 padding)
{
	while (1) {
		ssize_t req_len = recvfrom(sock, NULL, 0, MSG_PEEK | MSG_TRUNC,
					   NULL, NULL);
		assert(req_len >= 0);
		spray(sock, coding_dir_name, req_len, padding);
	}
}

static int check_srv_params(int argc, char * const argv[])
{
	UNUSED(argv);
	if (argc != 4) {
		printf(USAGE);
		return 1;
	}
	return 0;
}


/*
 * Check the parameters. The server should receive a directory that
 * contains subdirectories that represent a given chunk. The subdirectories
 * should be of the form:
 *
 * sample_vid/
 *   sample_vid00
 *   sample_vid01
 *   sample_vid02
 *   ...
 *   sample_vid10
 *   ...
 *
 * Each one of these directories should contain 21 files, exactly ten
 * of which are data files, ten of which are coding files, and one of
 * which is a meta file.
 *
 * The server should also receive an XIP address file.
 *
 * Next, the server should create packets based on the individual files.
 * Each packet should be of the form:
 *
 * |-------------------------------------------|
 * |              num-of-blocks                |
 * |-------------------------------------------|
 * |                 block-id                  |
 * |-------------------------------------------|
 * |        chunk-id     |      packet-len     |
 * |-------------------------------------------|
 *
 * 
 */

int main(int argc, char *argv[])
{
	struct sockaddr *srv;
	int s, srv_len;
	__u16 padding;

	if (check_srv_params(argc, argv))
		exit(1);

	s = socket(AF_XIA, SOCK_DGRAM, get_xdp_type());
	if (s < 0) {
		int orig_errno = errno;
		fprintf(stderr, "Cannot create XDP socket: %s\n",
			strerror(orig_errno));
		return 1;
	}

	srv = __get_addr(argv[1], &srv_len);
	assert(!bind(s, srv, srv_len));

	/* Check return value. */
	sscanf(argv[3], "%hd", &padding);
	recv_loop(s, argv[2], padding);

	free(srv);
	assert(!close(s));
	return 0;
}
