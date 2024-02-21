#!/bin/bash

SOURCE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

source $SOURCE_DIR/rt.sh

shutdown_nu_rt
start_nu_rt
