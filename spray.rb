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
BLOCK_LEN = NUM_DATA_FILES * DATA_LEN
SPLITS_DIR = "splits"
ENCODED_DIR = "encoded"

USAGE =
  "\nUsage:\n"                             \
  "\truby spray.rb srv_addr_file data_file_path\n\n"

if __FILE__ == $PROGRAM_NAME
  if ARGV.length != 2 or !File.exists?(ARGV[1])
    puts(USAGE)
    exit
  end

  data_file_path = ARGV[1]
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

  `dd if=/dev/zero status=none bs=1 count=#{padding} >> #{data_file_path}`
  `mkdir -p #{data_file_splits_dir}`
  `split -a #{num_digits} #{data_file_path} -b #{BLOCK_LEN} b -d`

  for i in 0..num_blocks-1
    block_id = "%0*d" % [num_digits, i]
    `mv b#{block_id} #{data_file_splits_dir}`
  end

  if !Dir.exists?(ENCODED_DIR)
    `mkdir #{ENCODED_DIR}`
  end

  splits = Dir.entries(data_file_splits_dir)
  for split in splits
    next if split == '.' or split == '..'
    `mkdir #{ENCODED_DIR}/#{split}`
  end

  for split in splits
    next if split == '.' or split == '..'
    `encoder #{File.join(data_file_splits_dir, split)} 10 10 cauchy_good 8 1 0`
    `mv #{ENCODED_DIR}/k* #{ENCODED_DIR}/m* #{ENCODED_DIR}/#{split}_meta.txt #{ENCODED_DIR}/#{split}`
  end

  `mkdir #{ENCODED_DIR}/#{data_file}`
  `mv #{ENCODED_DIR}/b?* #{ENCODED_DIR}/#{data_file}`
  `echo #{num_blocks} > #{ENCODED_DIR}/#{data_file}/#{data_file}_meta.txt`
  `rm -r splits`
  `head -c-#{padding} #{data_file_path} > #{data_file_path + ".bak"}`
  `mv #{data_file_path + ".bak"} #{data_file_path}`

  puts("Finished coding.")
  `./spray #{ARGV[0]} #{ENCODED_DIR} #{padding}`
end

