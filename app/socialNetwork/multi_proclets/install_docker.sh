#!/bin/bash

sudo apt-get update
sudo apt install apt-transport-https ca-certificates curl gnupg-agent -y
sudo apt install software-properties-common -y
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
sudo apt update
sudo apt install docker-ce docker-ce-cli containerd.io -y

sudo curl -L "https://github.com/docker/compose/releases/download/1.29.2/docker-compose-$(uname -s)-$(uname -m)" \
     -o /usr/local/bin/docker-compose
sudo chmod +x /usr/local/bin/docker-compose

sudo groupadd docker
sudo usermod -aG docker $USER
