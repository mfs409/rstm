#!/usr/bin/env gnuplot
set terminal postscript eps monochrome 'Helvetica' 20
set style data linespoints
set ylabel 'Time (seconds)' font 'Helvetica,20'
set xlabel 'Threads' font 'Helvetica,20'
set pointsize 2
set key top left
set ytic auto
set yrange[0:*]
set xrange[0.8:6.2]
set xtics 1

# glyphs (the 'pt' parameter)
# 1 is plus
# 2 is x
# 3 is star
# 4 is empty square
# 5 is filled square
# 6 is empty circle
# 7 is filled circle
# 8 is empty up triangle
# 9 is filled up triangle
# 10 is empty down triangle
# 11 is full down triangle
# 12 is empty diamond
# 13 is full diamond
# 14 is empty pentagon
# 15 is full pentagon

set output 'gen.eps'
plot \
    'gen.LLT.csv'               u 2:($3) pt 9 lw 1.5 t 'LLT',   \
    'gen.NOrec.csv'             u 2:($3) pt 15 lw 1.5 t 'NOrec',  \
    'gen.PTM.csv'               u 2:($3) pt 3 lw 1.5 t 'PTM',   \
    'gen.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'gen.Cohorts.csv'           u 2:($3) pt 12 lw 1.5 t 'Cohorts',   \
    'gen.CohortsNOrec.csv'      u 2:($3) pt 13 lw 1.5 t 'CohortsNOrec', \
    'gen.CTokenTurboELA.csv'    u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'

set output 'int.eps'
plot \
    'int.LLT.csv'               u 2:($3) pt 9 lw 1.5 t 'LLT',   \
    'int.NOrec.csv'              u 2:($3) pt 15 lw 1.5 t 'NOrec',   \
    'int.PTM.csv'                u 2:($3) pt 3 lw 1.5 t 'PTM',   \
    'int.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'int.Cohorts.csv'          u 2:($3) pt 12 lw 1.5 t 'Cohorts',   \
    'int.CohortsNOrec.csv'     u 2:($3) pt 13 lw 1.5 t 'CohortsNOrec', \
    'int.CTokenTurboELA.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'

set output 'khi.eps'
plot \
    'khi.LLT.csv'               u 2:($3) pt 9 lw 1.5 t 'LLT',   \
    'khi.NOrec.csv'             u 2:($3) pt 15 lw 1.5 t 'NOrec', \
    'khi.PTM.csv'              u 2:($3) pt 3 lw 1.5 t 'PTM',   \
    'khi.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'khi.Cohorts.csv'          u 2:($3) pt 12 lw 1.5 t 'Cohorts',   \
    'khi.CohortsNOrec.csv'     u 2:($3) pt 13 lw 1.5 t 'CohortsNOrec',\
    'khi.CTokenTurboELA.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'

set output 'klo.eps'
plot \
    'klo.LLT.csv'               u 2:($3) pt 9 lw 1.5 t 'LLT',   \
    'klo.NOrec.csv'             u 2:($3) pt 15 lw 1.5 t 'NOrec', \
    'klo.PTM.csv'              u 2:($3) pt 3 lw 1.5 t 'PTM',   \
    'klo.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'klo.Cohorts.csv'          u 2:($3) pt 12 lw 1.5 t 'Cohorts',   \
    'klo.CohortsNOrec.csv'     u 2:($3) pt 13 lw 1.5 t 'CohortsNOrec',\
    'klo.CTokenTurboELA.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'

set output 'ssc.eps'
plot \
    'ssc.LLT.csv'               u 2:($3) pt 9 lw 1.5 t 'LLT',   \
    'ssc.NOrec.csv'             u 2:($3) pt 15 lw 1.5 t 'NOrec', \
    'ssc.PTM.csv'              u 2:($3) pt 3 lw 1.5 t 'PTM',   \
    'ssc.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'ssc.Cohorts.csv'          u 2:($3) pt 12 lw 1.5 t 'Cohorts',   \
    'ssc.CohortsNOrec.csv'     u 2:($3) pt 13 lw 1.5 t 'CohortsNOrec',\
    'ssc.CTokenTurboELA.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
set output 'vhi.eps'
plot \
    'vhi.LLT.csv'               u 2:($3) pt 9 lw 1.5 t 'LLT',   \
    'vhi.NOrec.csv'             u 2:($3) pt 15 lw 1.5 t 'NOrec', \
    'vhi.PTM.csv'              u 2:($3) pt 3 lw 1.5 t 'PTM',   \
    'vhi.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'vhi.Cohorts.csv'          u 2:($3) pt 12 lw 1.5 t 'Cohorts',   \
    'vhi.CohortsNOrec.csv'     u 2:($3) pt 13 lw 1.5 t 'CohortsNOrec',\
    'vhi.CTokenTurboELA.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
set output 'vlo.eps'
plot \
    'vlo.LLT.csv'               u 2:($3) pt 9 lw 1.5 t 'LLT',   \
    'vlo.NOrec.csv'             u 2:($3) pt 15 lw 1.5 t 'NOrec', \
    'vlo.PTM.csv'              u 2:($3) pt 3 lw 1.5 t 'PTM',   \
    'vlo.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'vlo.Cohorts.csv'          u 2:($3) pt 12 lw 1.5 t 'Cohorts',   \
    'vlo.CohortsNOrec.csv'     u 2:($3) pt 13 lw 1.5 t 'CohortsNOrec',\
    'vlo.CTokenTurboELA.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
