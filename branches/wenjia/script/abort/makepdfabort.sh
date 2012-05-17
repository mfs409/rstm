#/bin/bash
for i in `ls *.eps`
do
    epstopdf $i
done




