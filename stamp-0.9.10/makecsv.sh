#/bin/bash

benches=( "" gen int khi klo ssc vhi vlo )
grepkeys=( "" Time time Time Time kernel Time Time )
cutkeys=( "" -f3 -f7 -f2 -f2 -c28-36 -f3 -f3 )

for i in `seq 1 7`
do
    bench=${benches[$i]}
    grepkey=${grepkeys[$i]}
    cutkey=${cutkeys[$i]}
    
    for alg in 'NOrec' 'OrecEager' 'OrecLazy' 'LLT' 'PTM' 'Fastlane1' 'Fastlane2' 'CTokenTurboELA' 'CohortsEN' 'CohortsEN2' 'CohortsNOrec' 'CohortsEager' 'Cohorts'
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
            echo -n "$filename", "$thread, " >> $filename

            point=""
            points=""

            for trial in `seq 1 5`
            do
                if test $i -eq 5
                then
                    point=`cat NONE."$alg"."$bench"."$thread"."$trial".64.edat | grep "$grepkey" | cut "$cutkey"`
                    points=$points"\n"$point
                else
                    point=`cat NONE."$alg"."$bench"."$thread"."$trial".64.edat | grep "$grepkey" | cut -d" " "$cutkey"`
                    points=$points"\n"$point
                fi
            done

            printf "$points" | sort -n | tail -4 | head -1  >> $filename
            
        done
                
    done
done




