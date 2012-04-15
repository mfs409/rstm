#!/usr/bin/env gnuplot
set terminal postscript eps monochrome 'Helvetica' 20
set style data linespoints
set ylabel 'Time (seconds)' font 'Helvetica,20'
set xlabel 'Threads' font 'Helvetica,20'
set pointsize 2
set key top left
set ytic auto
set yrange[0:*]
set xrange[0.8:12.2]
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
    'gen.PTM.csv'              u 2:($3) pt 4 lw 1.5 t 'PTM',   \
    'gen.Fastlane.csv'         u 2:($3) pt 5 lw 1.5 t 'Fastlane',   \
    'gen.Cohorts.csv'          u 2:($3) pt 5 lw 1.5 t 'Cohorts',   \
    'gen.CohortsNOrec.csv'     u 2:($3) pt 6 lw 1.5 t 'CohortsNOrec', \
    'gen.CTokenTurbo.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'

set output 'int.eps'
plot \
    'int.PTM.csv'              u 2:($3) pt 4 lw 1.5 t 'PTM',   \
    'int.Fastlane.csv'         u 2:($3) pt 5 lw 1.5 t 'Fastlane',   \
    'int.Cohorts.csv'          u 2:($3) pt 5 lw 1.5 t 'Cohorts',   \
    'int.CohortsNOrec.csv'     u 2:($3) pt 6 lw 1.5 t 'CohortsNOrec', \
    'int.CTokenTurbo.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
set output 'khi.eps'
plot \
    'khi.PTM.csv'              u 2:($3) pt 4 lw 1.5 t 'PTM',   \
    'khi.Fastlane.csv'         u 2:($3) pt 5 lw 1.5 t 'Fastlane',   \
    'khi.Cohorts.csv'          u 2:($3) pt 5 lw 1.5 t 'Cohorts',   \
    'khi.CohortsNOrec.csv'     u 2:($3) pt 6 lw 1.5 t 'CohortsNOrec',\
    'khi.CTokenTurbo.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
set output 'klo.eps'
plot \
    'klo.PTM.csv'              u 2:($3) pt 4 lw 1.5 t 'PTM',   \
    'klo.Fastlane.csv'         u 2:($3) pt 5 lw 1.5 t 'Fastlane',   \
    'klo.Cohorts.csv'          u 2:($3) pt 5 lw 1.5 t 'Cohorts',   \
    'klo.CohortsNOrec.csv'     u 2:($3) pt 6 lw 1.5 t 'CohortsNOrec',\
    'klo.CTokenTurbo.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
set output 'ssc.eps'
plot \
    'ssc.PTM.csv'              u 2:($3) pt 4 lw 1.5 t 'PTM',   \
    'ssc.Fastlane.csv'         u 2:($3) pt 5 lw 1.5 t 'Fastlane',   \
    'ssc.Cohorts.csv'          u 2:($3) pt 5 lw 1.5 t 'Cohorts',   \
    'ssc.CohortsNOrec.csv'     u 2:($3) pt 6 lw 1.5 t 'CohortsNOrec',\
    'ssc.CTokenTurbo.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
set output 'vhi.eps'
plot \
    'vhi.PTM.csv'              u 2:($3) pt 4 lw 1.5 t 'PTM',   \
    'vhi.Fastlane.csv'         u 2:($3) pt 5 lw 1.5 t 'Fastlane',   \
    'vhi.Cohorts.csv'          u 2:($3) pt 5 lw 1.5 t 'Cohorts',   \
    'vhi.CohortsNOrec.csv'     u 2:($3) pt 6 lw 1.5 t 'CohortsNOrec',\
    'vhi.CTokenTurbo.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
set output 'vlo.eps'
plot \
    'vlo.PTM.csv'              u 2:($3) pt 4 lw 1.5 t 'PTM',   \
    'vlo.Fastlane.csv'         u 2:($3) pt 5 lw 1.5 t 'Fastlane',   \
    'vlo.Cohorts.csv'          u 2:($3) pt 5 lw 1.5 t 'Cohorts',   \
    'vlo.CohortsNOrec.csv'     u 2:($3) pt 6 lw 1.5 t 'CohortsNOrec',\
    'vlo.CTokenTurbo.csv'      u 2:($3) pt 7 lw 1.5 t 'CTokenTurbo'
    
