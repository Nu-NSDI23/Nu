#!/bin/bash

# Check if the correct number of arguments are passed
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 remote_user remote_host directory"
    exit 1
fi
#
# Get the full path to the script
script_path=$(realpath "$0")
# Extract the directory path
script_dir=$(dirname "$script_path")

# Assign script arguments to variables
remote_user=$1
remote_host=$2
local_dir=$script_dir/$3/.
remote_dir=$script_dir/$3/


# Check if local directory exists
if [ ! -d "$local_dir" ]; then
    echo "Local directory $local_dir does not exist."
    exit 1
fi

echo "FROM: $local_dir"
echo "TO: $remote_user@$remote_host:$remote_dir"

# Create the remote directory if it doesn't exist
ssh $remote_user@$remote_host "mkdir -p $remote_dir"

# Perform rsync operation
rsync -avzh --progress --delete $local_dir $remote_user@$remote_host:$remote_dir

echo "Transfer operation completed."

