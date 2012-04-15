#/bin/bash

benches=( "" gen int khi klo ssc vhi vlo )
grepkeys=( "" Time time Time Time kernel Time Time )
cutkeys=( "" -f3 -f7 -f2 -f2 -c28-36 -f3 -f3 )

for i in `seq 1 7`
do
    bench=${benches[$i]}
    grepkey=${grepkeys[$i]}
    cutkey=${cutkeys[$i]}
    
    for alg in 'PTM' 'Fastlane' 'CTokenTurbo' 'Cohorts' 'CohortsNOrec'
    do
        
        filename="$bench"."$alg".csv

        if [ -e $filename ]
        then
            echo $filename already exists... remove it
            rm $filename
        fi
                
        echo Generating file "$filename"...
        
        for thread in 1 2 3 4 5 6 7 8 10 12
        do
            echo -n "$filename", "$thread", "" >> $filename
            if test $i -eq 5
            then
                cat NONE."$alg"."$bench"."$thread".1.64.edat | grep "$grepkey" | cut "$cutkey" >> $filename
            else
                cat NONE."$alg"."$bench"."$thread".1.64.edat | grep "$grepkey" | cut -d' ' "$cutkey" >> $filename
            fi
        done
        
    done
done




