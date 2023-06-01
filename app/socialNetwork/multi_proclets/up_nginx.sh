#!/bin/bash

docker run -d --rm --hostname dns.mageddo -p 5380:5380 \
       -v /var/run/docker.sock:/var/run/docker.sock \
       -v /etc/resolv.conf:/etc/resolv.conf \
       -v `pwd`/config/dns.json:/app/conf/config.json \
       defreitas/dns-proxy-server:2.19.0

pushd ymls
docker-compose -f nginx-thrift.yml create
docker-compose -f nginx-thrift.yml start
popd
