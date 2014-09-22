#
# spray.rb: split and send a file
#
# Author: Cody Doucette <doucette@bu.edu>
#
# Split a file and use a digital fountain to spray the
# contents using interleaved Cauchy Reed-Solomon coding.
#

# 512 - length_of_srv_addr - max_dag_size - sizeof_xia_hdr - sizeof_eth
DATA_LEN = 384
BLOCK_LEN = 10 * DATA_LEN

SPLITS_DIR = "splits"

USAGE =
  "\nUsage:\n"                             \
  "\truby spray.rb srv_addr_file data_file\n\n"

if __FILE__ == $PROGRAM_NAME
  if ARGV.length != 2 or !File.exists?(ARGV[1])
    puts(USAGE)
    exit
  end

  data_file = ARGV[1]
  base_file = File.basename(data_file)
  base_file_no_ext = base_file
  data_dir = File.join(SPLITS_DIR, base_file)

  if Dir.exists?(data_dir)
    puts("Splits for #{data_file} already exist. " \
         "Remove the #{data_dir} directory and try again.")
    exit
  end

  size = File.size(data_file)
  num_blocks = (File.size(data_file) / BLOCK_LEN)
  if File.size(data_file) % BLOCK_LEN != 0
    num_blocks += 1
  end
  num_digits = (num_blocks - 1).to_s().size()
  padding = BLOCK_LEN - (size % BLOCK_LEN)

  `dd if=/dev/zero status=none bs=1 count=#{padding} >> #{data_file}`
  `mkdir -p #{data_dir}`
  `split -a #{num_digits} #{data_file} -b #{BLOCK_LEN} b -d`

  for i in 0..num_blocks-1
    block_id = "%0*d" % [num_digits, i]
    `mv b#{block_id} #{data_dir}`
  end

  `mkdir Coding`

  splits = Dir.entries(data_dir)
  for split in splits
    next if split == '.' or split == '..'
    `mkdir Coding/#{split}`
  end

  for split in splits
    next if split == '.' or split == '..'
    `encoder #{File.join(data_dir, split)} 10 10 cauchy_good 8 1 0`
    `mv Coding/k* Coding/m* Coding/#{split}_meta.txt Coding/#{split}`
  end

  `mkdir Coding/#{base_file}`
  `mv Coding/b?* Coding/#{base_file}`
  `echo #{num_blocks} > Coding/#{base_file}/#{base_file}_meta.txt`
  `rm -r splits`
  `head -c-#{padding} #{data_file} > #{data_file + ".bak"}`
  `mv #{data_file + ".bak"} #{data_file}`

  puts("Finished coding.")
  `./spray #{ARGV[0]} Coding #{padding}`
end

