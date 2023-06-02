#!/bin/sh
if [ "$#" -ne 2 ]; then
    echo "No parameters specified"
    return 1
fi
if [ ! -d "$1" ]; then
  echo "Folder $1 does not exist."
  return 1
fi

cd $1
filecount=$(find -type f | wc -l)
# grep -r does not seem to work the same with busybox
#linecount=$(grep -r $2 | wc -l)
linecount=$(grep $2 $(find . -type f) | wc -l)
echo "The number of files are $filecount and the number of matching lines are $linecount"