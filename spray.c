#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "fountain.h"

#define USAGE	"usage:\t./spray srv-bind-addr srv-dst-addr "\
		"file-path padding failure-rate\n"

#define CODING_META_INFO_FILE_LEN	4
#define META_FILENAME			"meta.txt"
#define ENCODED_DIR			"encoded"

static int get_num_blocks(const char *filename)
{
	char *meta_file_path;
	FILE *meta_file;
	__u32 num_blocks;
	int size;

	/* If it does exist, open the meta file to find the number of blocks. */
	size = asprintf(&meta_file_path, "%s/%s/%s", ENCODED_DIR,
			filename, META_FILENAME);
	if (size == -1) {
		fprintf(stderr,
			"asprintf: cannot allocate meta path string\n");
		return -1;
	}

	meta_file = fopen(meta_file_path, "r");
	if (!meta_file) {
		fprintf(stderr,
			"fopen: cannot open meta file\n");
		return -1;
	}

	/* Read in a single four-byte integer representing number of blocks. */
	size = fscanf(meta_file, "%d", &num_blocks);
	fclose(meta_file);
	if (size != 1) {
		fprintf(stderr,
			"fscanf: cannot read number of blocks\n");
		return -1;
	}

	return num_blocks;
}

static void send_file(int s, const struct sockaddr *cli, int cli_len,
		      const char *filename, const char *chunk_path,
		      __u32 num_blocks, __u32 block_id,
		      __s16 chunk_id, __u16 padding) 
{
	FILE *chunk;
	struct fountain_hdr *fountain;
	long file_size;
	size_t bytes_read;
	size_t filename_len;
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

	filename_len = strlen(filename);
	strncpy(fountain->filename, filename,
		filename_len > FILENAME_MAX_LEN
		? FILENAME_MAX_LEN
		: filename_len);
	memset(fountain->filename + filename_len, 0,
	       FILENAME_MAX_LEN - filename_len);

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

static unsigned int send_files(int s, const struct sockaddr *cli, int cli_len,
			       const char *filename, const char *prefix,
			       __u32 num_blocks, __u32 files_per_block,
			       __u16 padding, int send_data, unsigned int fr)
{
	unsigned int i, j;
	int size;
	unsigned int num_dropped = 0;

	/* Loop over all blocks. */
	for (i = 1; i <= files_per_block; i++) {
		__s16 chunk_id = send_data ? i : -i;
		for (j = 0; j < num_blocks; j++) {
			char *chunk_path;
			size = asprintf(&chunk_path, "%s/%s/b%0*d/%s%0*d",
					ENCODED_DIR, filename,
					num_digits(num_blocks - 1), j,
					prefix,
					num_digits(DATA_FILES_PER_BLOCK), i);
			if (size == -1) {
				fprintf(stderr,
					"asprintf: cannot alloc chunk path\n");
				return -1;
			}

			usleep(100);
			if ((unsigned)rand() % 100 >= fr)
				send_file(s, cli, cli_len, filename, chunk_path,
					  num_blocks, j, chunk_id, padding);
			else
				num_dropped++;
		}
	}
	return num_dropped;
}

static inline void send_data_files(int s, const struct sockaddr *cli,
					   int cli_len, const char *filename,
					   __u32 num_blocks, __u16 padding,
					   unsigned int fr)
{
	int num_dropped = send_files(s, cli, cli_len, filename,
				     "k", num_blocks,
				     DATA_FILES_PER_BLOCK, padding, 1, fr);

	if (num_dropped == -1)
		return;

	fprintf(stderr, "Dropped %d data packets out of %d (%.1f%%)\n",
		num_dropped, DATA_FILES_PER_BLOCK * num_blocks,
		100 * (float)num_dropped / (DATA_FILES_PER_BLOCK * num_blocks));
}

static inline void send_code_files(int s, const struct sockaddr *cli,
					   int cli_len, const char *filename,
					   __u32 num_blocks, __u16 padding,
					   unsigned int fr)
{
	int num_dropped = send_files(s, cli, cli_len, filename,
				     "m", num_blocks,
				     CODE_FILES_PER_BLOCK, padding, 0, fr);

	if (num_dropped == -1)
		return;

	fprintf(stderr, "Dropped %d code packets out of %d (%.1f%%)\n",
		num_dropped, CODE_FILES_PER_BLOCK * num_blocks,
		100 * (float)num_dropped / (CODE_FILES_PER_BLOCK * num_blocks));
}

void spray(int s, const struct sockaddr *cli, int cli_len,
	   const char *file_path, __u16 padding, unsigned int fr)
{
	char *filename = basename(file_path);
	char *encoded_file_path;
	int size;
	int num_blocks;

        size = asprintf(&encoded_file_path, "%s/%s", ENCODED_DIR, filename);
        if (size == -1) {
                fprintf(stderr,
                        "asprintf: cannot allocate encoded file path\n");
                return;
        }

	/* Check if a directory for that name exists. */
	if (!dir_exists(encoded_file_path)) {
		fprintf(stderr,
			"invalid request; %s encoding does not exist\n",
			filename);
		return;
	}

	num_blocks = get_num_blocks(filename);
	if (num_blocks == -1) {
		fprintf(stderr,
			"get_num_blocks: cannot find number of blocks\n");
		return;
	}

	send_data_files(s, cli, cli_len, filename, num_blocks, padding, fr);
	send_code_files(s, cli, cli_len, filename, num_blocks, padding, fr);
}

static int check_srv_params(int argc, char * const argv[])
{
	UNUSED(argv);
	if (argc != 6) {
		printf(USAGE);
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct sockaddr *srv, *cli;
	int s, srv_len, cli_len, rc;
	unsigned int fr;
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
		fprintf(stderr, "No padding data exists.\n");
		return 1;
	}

	rc = sscanf(argv[5], "%d", &fr);
	if (errno != 0) {
		fprintf(stderr, "%s: sscanf errno=%i: %s\n",
			__func__, errno, strerror(errno));
		return 1;
	} else if (rc != 1) { 
		fprintf(stderr, "No failure rate exists.\n");
		return 1;
	}

	if (fr > 100) {
		fprintf(stderr, "Failure rate must be between 0 and 100.\n");
		return 1;
	}

	spray(s, cli, cli_len, argv[3], padding, fr);
	fprintf(stderr, "File sent.\n");

	free(cli);
	free(srv);
	assert(!close(s));
	return 0;
}
