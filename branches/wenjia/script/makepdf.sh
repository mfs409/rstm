#/bin/bash

benches=( "" gen int khi klo ssc vhi vlo )
grepkeys=( "" Time time Time Time kernel Time Time )
cutkeys=( "" -f3 -f7 -f2 -f2 -c28-36 -f3 -f3 )

for i in `ls *.eps`
do
    epstopdf $i
done




