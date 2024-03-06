#!/bin/bash

if [ ! -f $HOME/.ssh/id_rsa ]; then
  echo "Generating key pair..."
  ssh-keygen -b 2048 -t rsa -f ~/.ssh/id_rsa -q -N ""
  echo "Finished."
else
  echo "Key pair already exists."
fi

cat $HOME/.ssh/id_rsa.pub
