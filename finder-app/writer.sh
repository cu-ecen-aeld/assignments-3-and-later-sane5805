#!/bin/sh

# Check if both arguments are provided
if [ $# -ne 2 ]; then
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

# Assign arguments to variables
writefile="$1"
writestr="$2"

#echo "[Debug] writefile = $writefile & writestr = $writestr"

# Check if writefile is not empty
if [ -z "$writefile" ]; then
    echo "Error: 'writefile' argument is empty."
    exit 1
fi

# Check if writestr is not empty
if [ -z "$writestr" ]; then
    echo "Error: 'writestr' argument is empty."
    exit 1
fi

# Create the directory path if it doesn't exist
#dir_path=$(dirname "$writefile")
dir_path="${writefile%/*}"
#echo "[Debug] $dir_path"

if [ ! -d "$dir_path" ]; then
    mkdir -p "$dir_path" || {
        echo "Error: Failed to create directory '$dir_path'."
        exit 1
    }
fi

# Write the content to the file, overwriting if it exists
echo "$writestr" > "$writefile" || {
    echo "Error: Failed to write to '$writefile'."
    exit 1
}

#echo "Content written to '$writefile'."

# Exit with success status (0)
exit 0
