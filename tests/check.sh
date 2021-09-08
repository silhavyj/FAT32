#!/bin/bash

for a in $(ls input) ; do
	if test -f "output/$a" ; then
    		cmp "input/$a" "output/$a" && echo "$a OK"
	else
		echo "file output/$a does not exist!" 
	fi
done