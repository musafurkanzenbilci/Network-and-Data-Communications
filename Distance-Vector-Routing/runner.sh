#!/usr/bin/env bash

set -Eeuo pipefail

nodes=()

case $1 in
	simple)
		echo "running simple case"
		nodes=(3000 3001 3002)
		cp Node.py simple
		cd simple
		;;

	first)
		echo "running first case"
		nodes=(3000 3001 3002 3003 3004 3005)
		cp Node.py first
		cd first
		;;
	second)
		echo "running second case"
		nodes=(3000 3001 3002 3003)
		cp Node.py second
		cd second
		;;
	third)
		echo "running third case"
		nodes=(3000 3001 3002 3003 3004 3005 3006 3007 3008 3009 3010)
		cp Node.py third
		cd third
		;;
	*)
		echo 'Select from the list of tests or edit the script'
		printf '\t%s\n' "simple"
		printf '\t%s\n' "first"
		printf '\t%s\n' "second"
		printf '\t%s\n' "third"
        	exit
		;;
esac

for port in "${nodes[@]}"; do
    (python Node.py "${port}") &
done

sleep 0.1
wait $(jobs -p)
echo "All done"
