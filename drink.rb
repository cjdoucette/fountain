#
# drink.rb: receive and decode a file
#
# Author: Cody Doucette <doucette@bu.edu>
#
# Receive a file and use a decoder to reassemble the
# contents using interleaved Cauchy Reed-Solomon coding.
#

# 512 - length_of_srv_addr - max_dag_size - sizeof_xia_hdr - sizeof_eth
DATA_LEN = 384
NUM_DATA_FILES = 10
BLOCK_LEN = NUM_DATA_FILES * DATA_LEN

CONTENT_DIR = "content"

USAGE =
  "\nUsage:\n"                             \
  "\truby drink.rb cli_addr_file srv_addr_file data_file\n\n"

if __FILE__ == $PROGRAM_NAME
  if ARGV.length != 3
    puts(USAGE)
    exit
  end

  `mkdir #{CONTENT_DIR}`
  `./drink #{ARGV[0]} #{ARGV[1]} #{ARGV[2]}`

  padding = 0
  Dir.foreach(File.join(CONTENT_DIR, ARGV[2])) do |block|
    next if block == '.' or block == '..'
    if block == 'padding.txt'
      open(File.join(CONTENT_DIR, ARGV[2], 'padding.txt'), 'r') { |f|
        padding = f.readline().strip().to_i()
      }
      next 
    end
    block_path = File.join(CONTENT_DIR, ARGV[2], block)
    num_orig_files = `ls #{File.join(block_path, "k")}* | wc -l`.to_i()
    if num_orig_files == NUM_DATA_FILES
      `cat #{File.join(block_path, "k")}* > #{File.join(block_path, block + "_decoded")}`
    else
      `decoder #{File.join(CONTENT_DIR, ARGV[2])} #{block}` 
    end
  end

  new_filename = File.join(CONTENT_DIR, ARGV[2])
  bak_filename = new_filename + ".bak"
  `cat #{File.join(new_filename, "b*", "b*_decoded")} > #{bak_filename}`
  `rm -r #{new_filename}`
  `head -c-#{padding} #{bak_filename} > #{new_filename}`
end

