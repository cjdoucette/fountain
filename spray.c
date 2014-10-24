#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "fountain.h"

#define USAGE	"usage:\t./spray srv-bind-addr srv-dst-addr data-path pad\n"

#define CODING_META_INFO_FILE_LEN	4
#define META_FILE_NAME			"meta.txt"
#define ENCODED_DIR			"encoded"

static void send_file(int s, const struct sockaddr *cli, socklen_t cli_len,
		      __u32 num_blocks, __u32 block_id, __s16 chunk_id,
		      const char *chunk_path, __u16 padding,
		      const char *file_name)
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
	strncpy(fountain->filename, file_name, strlen(file_name));
	memset(fountain->filename + strlen(file_name), 0,
	       FILE_NAME_MAX_LEN - strlen(file_name));
	fountain->filename[FILE_NAME_MAX_LEN - 1] = '\0';

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

void spray(int s, struct sockaddr *cli, unsigned int cli_len,
	   const char *file_path, __u16 padding)
{
	char *meta_file_path;
	char *encoded_file_path;
	char *file_name = basename(file_path);

	FILE *meta_file;

	int size;
	__u32 num_blocks;
	__u32 j;
	__u16 i;

        size = asprintf(&encoded_file_path, "%s/%s", ENCODED_DIR, file_name);
        if (size == -1) {
                fprintf(stderr,
                        "asprintf: cannot allocate encoded file path\n");
                return;
        }

	/* Check if a directory for that name exists. */
	if (!dir_exists(encoded_file_path)) {
		fprintf(stderr, "invalid request; %s encoding does not exist\n",
			file_name);
		return;
	}

	/* If it does exist, open the meta file to find the number of blocks. */
	size = asprintf(&meta_file_path, "%s/%s/%s", ENCODED_DIR,
			file_name, META_FILE_NAME);
	if (size == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate meta path string\n");
		return;
	}

	meta_file = fopen(meta_file_path, "r");
	if (!meta_file) {
		fprintf(stderr,
			"fopen: cannot open meta file\n");
		return;
	}
	/* Read in a single four-byte integer. */
	size = fscanf(meta_file, "%d", &num_blocks);
	fclose(meta_file);
	if (size != 1) {
		fprintf(stderr,
			"fscanf: cannot read number of blocks\n");
		return;
	}

	/* Loop over all blocks. */
	for (i = 1; i <= DATA_FILES_PER_BLOCK; i++) {
		for (j = 0; j < num_blocks; j++) {
			char *chunk_path;
			size = asprintf(&chunk_path,
					"%s/%s/b%0*d/k%0*d",
					ENCODED_DIR, file_name,
					num_digits(num_blocks - 1), j,
					num_digits(DATA_FILES_PER_BLOCK), i);
			if (size == -1) {
				fprintf(stderr,
					"asprintf: cannot allocate chunk path string\n");
				return;
			}
			usleep(100);
			send_file(s, cli, cli_len, num_blocks, j, i,
				  chunk_path, padding, file_name);
		}
	}

	for (i = 1; i <= CODE_FILES_PER_BLOCK; i++) {
		for (j = 0; j < num_blocks; j++) {
			char *chunk_path;
			size = asprintf(&chunk_path,
					"%s/%s/b%0*d/m%0*d",
					ENCODED_DIR, file_name,
					num_digits(num_blocks - 1), j,
					num_digits(CODE_FILES_PER_BLOCK), i);
			if (size == -1) {
				fprintf(stderr,
					"asprintf: cannot allocate chunk path string\n");
				return;
			}
			usleep(100);
			send_file(s, cli, cli_len, num_blocks, j, -i,
				  chunk_path, padding, file_name);
		}
	}
}

static int check_srv_params(int argc, char * const argv[])
{
	UNUSED(argv);
	if (argc != 5) {
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
	struct sockaddr *srv, *cli;
	int s, srv_len, cli_len, rc;
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

	srv = get_addr(argv[1], &srv_len);
	assert(srv);
	assert(!bind(s, srv, srv_len));
	cli = get_addr(argv[2], &cli_len);
	assert(cli);

	/* Read padding amount. */
	rc = sscanf(argv[4], "%hd", &padding);
	if (errno != 0) {
		fprintf(stderr, "%s: sscanf errno=%i: %s\n",
			__func__, errno, strerror(errno));
		return 1;
	} else if (rc != 1) { 
		fprintf(stderr, "No padding number exists.\n");
		return 1;
	}

	spray(s, cli, cli_len, argv[3], padding);

	free(srv);
	assert(!close(s));
	return 0;
}
