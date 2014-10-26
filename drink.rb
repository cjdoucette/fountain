#
# drink.rb: receive and decode a file
#
# Author: Cody Doucette <doucette@bu.edu>
#
# Receive a file and use a decoder to reassemble the
# contents using interleaved Cauchy Reed-Solomon coding.
#

require 'fileutils'

DATA_LEN =		384
NUM_DATA_FILES =	10
BLOCK_LEN =		NUM_DATA_FILES * DATA_LEN

DECODED_DIR =		"decoded"
RCVD_FILENAME =		"name.txt"
PADDING_FILENAME =	"padding.txt"
DECODER =		"decoder"
BAK_EXT =		".bak"

USAGE =
  "\nUsage:\n"                             \
  "\truby drink.rb cli-bind-addr\n\n"

def backup(s)
  return s + BAK_EXT
end

if __FILE__ == $PROGRAM_NAME
  if ARGV.length != 1
    puts(USAGE)
    exit
  end

  if !Dir.exists?(DECODED_DIR)
    FileUtils.mkdir(DECODED_DIR)
  end

  while true
    # Wait for a file to be received.
    `./drink #{ARGV[0]}`

    # Find the name of hte received file.
    filename = nil
    open(File.join(DECODED_DIR, RCVD_FILENAME), 'r') { |f|
      filename = f.readline().strip()
    }
    FileUtils.rm(File.join(DECODED_DIR, RCVD_FILENAME))

    padding = 0
    Dir.foreach(File.join(DECODED_DIR, filename)) do |block|
      next if block == '.' or block == '..'

      # Get padding for file.
      if block == 'padding.txt'
        open(File.join(DECODED_DIR, filename, PADDING_FILENAME), 'r') { |f|
          padding = f.readline().strip().to_i()
        }
        next 
      end

      # Directory to place this decoded block.
      block_path = File.join(DECODED_DIR, filename, block)

      # Number of files representing the original data. If there are
      # enough of these, decoding doesn't need to happen -- the
      # pieces can just be concatenated together to obtain the block.
      num_orig_files = `ls #{File.join(block_path, "k")}* | wc -l`.to_i()
      if num_orig_files == NUM_DATA_FILES
        `cat #{File.join(block_path, "k")}* > \
	     #{File.join(block_path, block + "_decoded")}`
      else
        `#{DECODER} #{File.join(DECODED_DIR, filename)} #{block}` 
      end
    end

    # Concatenate all the blocks together.
    file_path = File.join(DECODED_DIR, filename)
    backup_file_path = backup(file_path)
    `cat #{File.join(file_path, "b*", "b*_decoded")} > #{backup_file_path}`

    # Remove the decoded directories, since we don't
    # need the individual pieces anymore.
    FileUtils.rm_r(file_path)

    # Remove padding from end of file, if necessary.
    if padding > 0
      `head -c-#{padding} #{backup_file_path} > #{file_path}`
      FileUtils.rm(backup_file_path)
    else
      FileUtils.mv(backup_file_path, file_path)
    end

    puts("File decoded.")
  end
end

