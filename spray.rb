#
# spray.rb: split and send a file
#
# Author: Cody Doucette <doucette@bu.edu>
#
# Split a file and use a digital fountain to spray the
# contents using interleaved Cauchy Reed-Solomon coding.
#

require 'fileutils'

DATA_LEN =		384
NUM_DATA_FILES =	10
NUM_CODE_FILES =	10
BLOCK_LEN =		NUM_DATA_FILES * DATA_LEN

SPLITS_DIR =	"splits"
ENCODED_DIR =	"encoded"
META_FILE =	"meta.txt"
BAK_EXT =	".bak"
ENCODER =	"encoder"
CODING_TECH =	"cauchy_good"
WORD_SIZE =	8

USAGE =
  "\nUsage:\n"                             \
  "\truby spray.rb srv-bind-addr srv-dst-addr data-path\n\n"

def pad_with_zeros(file_path, padding)
  `dd if=/dev/zero status=none bs=1 count=#{padding} >> #{file_path}`
end

def encode_block(file_splits_path, filename, block, num_digits)
  block_padded = "b%0*d" % [num_digits, block]
  FileUtils.mkdir_p(File.join(ENCODED_DIR, filename, block_padded))
  `#{ENCODER} #{File.join(file_splits_path, block_padded)} #{filename}	\
              #{block_padded} #{NUM_DATA_FILES} #{NUM_CODE_FILES}	\
              #{CODING_TECH} #{WORD_SIZE} 1 0`
end

def backup(s)
  return s + BAK_EXT
end

if __FILE__ == $PROGRAM_NAME
  if ARGV.length != 3 or !File.exists?(ARGV[1])
    puts(USAGE)
    exit
  end

  file_path = ARGV[2]
  filename = File.basename(file_path)
  file_splits_path = File.join(SPLITS_DIR, filename)

  # Get size of file to encode.
  file_size = File.size(file_path)

  # Get number of blocks in file.
  num_blocks = (file_size / BLOCK_LEN)
  if file_size % BLOCK_LEN != 0
    num_blocks += 1
  end

  # Get number of digits to use for block names.
  num_digits = (num_blocks - 1).to_s().size()

  # Calculate number of zero bytes needed to
  # pad the file to a multiple of BLOCK_LEN.
  padding = (BLOCK_LEN - (file_size % BLOCK_LEN) % BLOCK_LEN)

  # Check if an encoding already exists for this file.
  # If it does, we don't have to encode it, we just
  # need to send it.
  if Dir.exists?(File.join(ENCODED_DIR, filename))
    puts("Sending previously encoded files for #{file_path}...")
    `./spray #{ARGV[0]} #{ARGV[1]} #{ARGV[2]} #{padding}`
    exit
  end

  # Pad with zeros up to the nearest multiple of BLOCK_LEN, if necessary.
  if padding > 0
    FileUtils.cp(file_path, backup(file_path))
    pad_with_zeros(file_path, padding)
  end

  # Create a directory to hold the split-up file and split it.
  FileUtils.mkdir_p(file_splits_path)
  prefix = File.join(file_splits_path, "b")
  `split -a #{num_digits} #{file_path} -b #{BLOCK_LEN} #{prefix} -d`

  puts("Encoding...")

  # Make individual encoded directories for chunks and encode them.
  for block in 0..(num_blocks - 1)
    encode_block(file_splits_path, filename, block, num_digits)
  end

  # Save the number of blocks in a meta file.
  `echo #{num_blocks} > #{ENCODED_DIR}/#{filename}/#{META_FILE}`

  # Remove the splits of the file, as they are no longer of any use.
  FileUtils.rm_r(SPLITS_DIR)

  # If we needed to pad the file, restore the original file.
  if padding > 0
    FileUtils.mv(backup(file_path), file_path)
  end

  puts("Sending packets...")

  # Use network application to serve encoded file.
  `./spray #{ARGV[0]} #{ARGV[1]} #{ARGV[2]} #{padding}`
end
