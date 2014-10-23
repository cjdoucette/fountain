#
# spray.rb: split and send a file
#
# Author: Cody Doucette <doucette@bu.edu>
#
# Split a file and use a digital fountain to spray the
# contents using interleaved Cauchy Reed-Solomon coding.
#

DATA_LEN = 384
NUM_DATA_FILES = 10
NUM_CODE_FILES = 10
BLOCK_LEN = NUM_DATA_FILES * DATA_LEN
SPLITS_DIR = "splits"
ENCODED_DIR = "encoded"

USAGE =
  "\nUsage:\n"                             \
  "\truby spray.rb srv-bind-addr srv-dst-addr data-path\n\n"

if __FILE__ == $PROGRAM_NAME
  if ARGV.length != 3 or !File.exists?(ARGV[1])
    puts(USAGE)
    exit
  end

  data_file_path = ARGV[2]
  data_file = File.basename(data_file_path)
  data_file_splits_dir = File.join(SPLITS_DIR, data_file)

  # Check if an encoding already exists for this file.
  if Dir.exists?(File.join(ENCODED_DIR, data_file))
    puts("Encoded files for #{data_file_path} already exist.\n"    \
         "Remove #{File.join(ENCODED_DIR, data_file)} and try again.")
    exit
  end

  size = File.size(data_file_path)
  num_blocks = (File.size(data_file_path) / BLOCK_LEN)
  if File.size(data_file_path) % BLOCK_LEN != 0
    num_blocks += 1
  end
  num_digits = (num_blocks - 1).to_s().size()
  padding = BLOCK_LEN - (size % BLOCK_LEN)

  # Pad with zeros up to the nearest multiple of BLOCK_LEN, if necessary.
  if padding != BLOCK_LEN
    `dd if=/dev/zero status=none bs=1 count=#{padding} >> #{data_file_path}`
  end

  # Create a directory to hold the split-up file and split it.
  `mkdir -p #{data_file_splits_dir}`
  prefix = File.join(data_file_splits_dir, "b")
  `split -a #{num_digits} #{data_file_path} -b #{BLOCK_LEN} #{prefix} -d`

  # Make encoded directory, if necessary.
  if !Dir.exists?(ENCODED_DIR)
    `mkdir #{ENCODED_DIR}`
  end

  # Make individual encoded directories for
  # chunks and encode them.
  `mkdir #{File.join(ENCODED_DIR, data_file)}`
  for block in 0..(num_blocks - 1)
    block_padded = "b%0*d" % [num_digits, block]
    `mkdir -p #{File.join(ENCODED_DIR, data_file, block_padded)}`
    `encoder #{File.join(data_file_splits_dir, block_padded)} #{data_file} \
             #{block_padded} #{NUM_DATA_FILES} #{NUM_CODE_FILES} \
             cauchy_good 8 1 0`
  end

  # Save the number of blocks in a meta file.
  `echo #{num_blocks} > #{ENCODED_DIR}/#{data_file}/meta.txt`

  # Remove the splits of the file, as they are no longer of any use.
  `rm -r splits`

  if padding != BLOCK_LEN
    `head -c-#{padding} #{data_file_path} > #{data_file_path + ".bak"}`
    `mv #{data_file_path + ".bak"} #{data_file_path}`
  end

  puts("Finished coding.")

  # Use network application to serve encoded file.
  `./spray #{ARGV[0]} #{ARGV[1]} #{ARGV[2]} #{padding}`
end
