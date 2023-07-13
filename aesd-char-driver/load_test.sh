#!/bin/bash
clear 
sudo killall aesdsocket 
sudo ./aesdchar_unload
make && sudo ./aesdchar_load && ../server/aesdsocket
