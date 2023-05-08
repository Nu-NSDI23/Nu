#!/bin/bash

docker ps | awk '{print $1}' | grep -v CON | xargs docker stop
docker ps | awk '{print $1}' | grep -v CON | xargs docker kill
