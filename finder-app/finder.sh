#!/bin/sh

# Check if both arguments are provided
if [ $# -ne 2 ]; then
    echo "Usage: $0 <filesdir> <searchstr>"
    exit 1
fi

# Assign arguments to variables
filesdir="$1"
searchstr="$2"

#echo  "filesdir = $filesdir & searchstr = $searchstr" 

# Check if filesdir exists and is a directory
if [ ! -d "$filesdir" ]; then
    echo "Error: '$filesdir' is not a valid directory."
    exit 1
fi


# Find files in the directory and its subdirectories
file_count=$(find "$filesdir" -type f | wc -l)

#echo "total file count is $file_count"

# Initialize a variable to count matching lines
match_count=0

# Debug
#echo "List of files to process:"
#find "$filesdir" -type f | while read -r file; do
#	echo "$file"
#done

# Loop through each file and search for the specified string
for file in $(find "$filesdir" -type f)
do
    if grep -q "$searchstr" "$file"; then
        match_count=$((match_count + 1))
    fi
done

# Display the results
echo "The number of files are $file_count and the number of matching lines are $match_count"

# Exit with success status (0)
exit 0

