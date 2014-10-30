#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "fountain.h"

#define DECODED_DIR		"decoded"
#define NAME_FILE		"name.txt"
#define NAME_FILE_PATH		DECODED_DIR "/" NAME_FILE
#define DATA_PREFIX		"k"
#define CODE_PREFIX		"m"
#define WORD_SIZE		8
#define CODING_TECH		"cauchy_good"

#define USAGE	"usage:\t./drink cli_addr_file\n"

static int check_cli_params(int argc, char * const argv[])
{
	UNUSED(argv);
	if (argc != 2) {
		printf(USAGE);
		return 1;
	}
	return 0;
}

void create_block_dirs(const char *filename, __u32 num_blocks)
{
	char *block_path, *decoded_file_path;
	int alloc_len, len, rc;
	__u32 i;
	int n_digits = num_digits(num_blocks - 1);

	/* Create directory for decoded file. */
	len = asprintf(&decoded_file_path, "%s/%s", DECODED_DIR,
		       filename);
	if (len == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate file path string\n");
		return;
	}

	if (file_exists(decoded_file_path))
		/* Remove existing file. */
		remove(decoded_file_path);

	rc = mkdir(decoded_file_path, 0777);
	if (rc < 0) {
		fprintf(stderr, "%s: mkdir errno=%i on %s: %s\n",
			__func__, errno, decoded_file_path, strerror(errno));
		exit(1);
	}

	/* asprintf returns the number of bytes printed, not the
	 * number of bytes allocated, so we must add one for the
	 * terminating null byte since we're going to reuse
	 * the @block_path allocated memory.
	 */

	/* Create directories for separate decoded blocks. */
	len = asprintf(&block_path, "%s/%s/b%0*d", DECODED_DIR,
		       filename, n_digits, 0);
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
			       DECODED_DIR, filename, n_digits, i);
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

static void create_name_file(const char *filename)
{
	/* Create file that holds received file's name. */
	FILE *name_file = fopen(NAME_FILE_PATH, "wb");
	assert(name_file);
	fprintf(name_file, "%s", filename);
	fclose(name_file);
}

static void create_padding_file(const char *filename, __u16 padding)
{
	FILE *padding_file;
	char *padding_file_path;
	int size = asprintf(&padding_file_path, "%s/%s/padding.txt",
			    DECODED_DIR, filename);
	if (size == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate padding file path\n");
		return;
	}
	padding_file = fopen(padding_file_path, "wb");
	assert(padding_file);
	fprintf(padding_file, "%hd", padding);
	fclose(padding_file);
}

static int write_data_to_file(const char *file_path, const __u8 *data,
			      int num_bytes)
{
	FILE *f = fopen(file_path, "wb");
	int rc;
	assert(f);
	rc = fwrite(data, sizeof(char), num_bytes, f);
	fclose(f);
	return rc;
}

static int write_meta_data_to_file(const char *meta_file_path,
	const char *filename, __u32 num_blocks, __u32 block_id,
	unsigned int chunk_size)
{ 
	FILE *meta_file = fopen(meta_file_path, "wb");
	int rc;
	assert(meta_file);
	rc = fprintf(meta_file,
		     "%s/%s/b%0*d\n%u\n%d %d %d %d %u\n%s\n%d\n%d\n",
		     DECODED_DIR, filename, num_digits(num_blocks - 1),
		     block_id, chunk_size, DATA_FILES_PER_BLOCK,
		     CODE_FILES_PER_BLOCK, WORD_SIZE, 1, chunk_size,
		     CODING_TECH, 3, 1);
	fclose(meta_file);
	return rc;
}

static inline int create_file_path(char *file_path, int alloc_len,
				   const char *filename,
				   __u32 num_blocks, __u32 block_id,
				   __s16 chunk_id, __u16 chunk_id_abs)
{
	return snprintf(file_path, alloc_len, "%s/%s/b%0*d/%s%0*d",
			DECODED_DIR, filename,
			num_digits(num_blocks - 1), block_id,
			chunk_id > 0 ? DATA_PREFIX : CODE_PREFIX,
			chunk_id > 0 ? num_digits(DATA_FILES_PER_BLOCK) :
				       num_digits(CODE_FILES_PER_BLOCK),
			chunk_id_abs);
}

static inline int create_meta_file_path(char *file_path, int alloc_len,
					const char *filename,
					__u32 num_blocks, __u32 block_id)
{
	return snprintf(file_path, alloc_len, "%s/%s/b%0*d/b%0*d_meta.txt",
			DECODED_DIR, filename,
			num_digits(num_blocks - 1), block_id,
			num_digits(num_blocks - 1), block_id);
}

static void recv_file(int s)
{
	struct tmp_sockaddr_storage srv;
	socklen_t srv_len;

	char *recv_file_path, *meta_file_path;

	/* Variables that hold fountain header data. */
	struct fountain_hdr *fountain_hdr;
	__u32 num_blocks;
	__u32 block_id;
	__s16 chunk_id;
	__u16 chunk_id_abs;
	__u16 packet_len;
	__u16 padding;
	char *filename;

	unsigned int pkt_len, num_read;
	int file_path_alloc_len, meta_path_alloc_len, size, rc;
	__u32 blocks_filled = 0, num_written;
	__u16 *num_recv_in_block;

	/* Read how many bytes are in the packet. */
	pkt_len = recvfrom(s, NULL, 0, MSG_PEEK | MSG_TRUNC, NULL, NULL);
	assert(pkt_len > sizeof(*fountain_hdr));

	fountain_hdr = alloca(pkt_len);
	srv_len = sizeof(srv);
	num_read = recvfrom(s, fountain_hdr, pkt_len, 0,
			    (struct sockaddr *)&srv, &srv_len);
	assert(pkt_len == num_read);

	fprintf(stderr, "Receiving packets...\n");

	num_blocks = ntohl(fountain_hdr->num_blocks);
	block_id = ntohl(fountain_hdr->block_id);
	chunk_id = ntohs(fountain_hdr->chunk_id);
	chunk_id_abs = chunk_id < 0 ? -chunk_id : chunk_id;
	packet_len = ntohs(fountain_hdr->packet_len);
	padding = ntohs(fountain_hdr->padding);

	size = asprintf(&filename, "%s", fountain_hdr->filename);
	if (size == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate filename\n");
		return;
	}

	/* Create directories to hold received file. */
	create_block_dirs(filename, num_blocks);

	/* Create meta file with filename so that decoder knows
	 * what name to give the received file.
	 */
	create_name_file(filename);

	/* Create meta file that holds the received file's padding. */
	create_padding_file(filename, padding);

	/* Create file from first received packet. */
	size = asprintf(&recv_file_path, "%s/%s/b%0*d/%s%0*d", DECODED_DIR,
			filename, num_digits(num_blocks - 1), block_id,
			chunk_id > 0 ? DATA_PREFIX : CODE_PREFIX,
			chunk_id > 0 ? num_digits(DATA_FILES_PER_BLOCK) :
				       num_digits(CODE_FILES_PER_BLOCK),
			chunk_id_abs);
	if (size == -1) {
		fprintf(stderr, "asprintf: cannot allocate file path\n");
		return;
	}
	file_path_alloc_len = size + 1;

	/* Write the packet to a file, keep track of how many packets
	 * we have received and which blocks they belong to.
	 */
	num_written = write_data_to_file(recv_file_path, fountain_hdr->data,
					 packet_len - sizeof(*fountain_hdr));
	if (num_written != packet_len - sizeof(*fountain_hdr)) {
		fprintf(stderr, "fwrite: cannot write packet to file\n");
		return;
	}
	num_recv_in_block = malloc(num_blocks * sizeof(*num_recv_in_block));
	num_recv_in_block[block_id]++;
	if (num_recv_in_block[block_id] == DATA_FILES_PER_BLOCK)
		blocks_filled++;

	/* Create file that holds the received file's meta data. */
	size = asprintf(&meta_file_path, "%s/%s/b%0*d/b%0*d_meta.txt",
			DECODED_DIR, filename,
			num_digits(num_blocks - 1), block_id,
			num_digits(num_blocks - 1), block_id);
	if (size == -1) {
		fprintf(stderr, "asprintf: cannot allocate meta file path\n");
		return;
	}
	meta_path_alloc_len = size + 1;

	rc = write_meta_data_to_file(meta_file_path, filename,
				     num_blocks, block_id,
				     (packet_len - sizeof(*fountain_hdr)) *
				     DATA_FILES_PER_BLOCK);
	if (rc < 0) {
		fprintf(stderr, "fprintf: cannot write meta data to file\n");
		return;
	}

	/* Repeat the receive process until no more packets are received. */
	while (blocks_filled < num_blocks) {
		struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
		fd_set readfds;

		FD_ZERO(&readfds);
		FD_SET(s, &readfds);
		rc = select(s + 1, &readfds, NULL, NULL, &timeout);
		assert(rc >= 0);
		if (!rc)
			/* No response from server. */
			return;

		/* Read how many bytes are in the packet. */
		pkt_len = recvfrom(s, NULL, 0, MSG_PEEK|MSG_TRUNC, NULL, NULL);
		assert(pkt_len > sizeof(*fountain_hdr));

		/* Allocate a buffer for those bytes and read in the packet. */
		fountain_hdr = alloca(pkt_len);
		num_read = recvfrom(s, fountain_hdr, pkt_len, 0,
				    (struct sockaddr *)&srv, &srv_len);
		assert(pkt_len == num_read);

		assert(ntohl(fountain_hdr->num_blocks) == num_blocks);
		block_id = ntohl(fountain_hdr->block_id);
		chunk_id = ntohs(fountain_hdr->chunk_id);
		chunk_id_abs = chunk_id < 0 ? -chunk_id : chunk_id;
		packet_len = ntohs(fountain_hdr->packet_len);
		assert(padding == ntohs(fountain_hdr->padding));
		assert(strncmp(filename, fountain_hdr->filename,
		       strlen(filename)) == 0);

		/* Use same buffer up to @alloc_len bytes. We
		 * will never need more space than that.
		 */

		rc = create_file_path(recv_file_path, file_path_alloc_len,
				 filename, num_blocks, block_id,
				 chunk_id, chunk_id_abs);
		if (rc < 0) {
			fprintf(stderr, "snprintf: cannot create file path\n");
			return;
		}
		num_written = write_data_to_file(recv_file_path,
				fountain_hdr->data,
				packet_len - sizeof(*fountain_hdr));
		if (num_written != packet_len - sizeof(*fountain_hdr)) {
			fprintf(stderr,
				"fwrite: cannot write packet to file\n");
			return;
		}
		num_recv_in_block[block_id]++;
		if (num_recv_in_block[block_id] == DATA_FILES_PER_BLOCK)
			blocks_filled++;

		rc = create_meta_file_path(meta_file_path, meta_path_alloc_len,
					   filename, num_blocks, block_id);
		if (rc < 0) {
			fprintf(stderr, "snprintf: cannot create meta path\n");
			return;
		}
		rc = write_meta_data_to_file(meta_file_path, filename,
			num_blocks, block_id,
			(packet_len - sizeof(*fountain_hdr)) *
			DATA_FILES_PER_BLOCK);
		if (rc < 0) {
			fprintf(stderr,
				"fprintf: cannot write meta data to file\n");
			return;
		}
	}

	free(num_recv_in_block);
}

int main(int argc, char *argv[])
{
	struct sockaddr *cli;
	int s, cli_len;

	if (check_cli_params(argc, argv))
		exit(1);

	s = socket(AF_XIA, SOCK_DGRAM, get_xdp_type());
	assert(s >= 0);
	cli = get_addr(argv[1], &cli_len);
	assert(cli);
	assert(!bind(s, cli, cli_len));

	recv_file(s);

	free(cli);
	assert(!close(s));
	return 0;
}
