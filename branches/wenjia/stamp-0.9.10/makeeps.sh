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
    'gen.OrecLazy.csv'               u 2:($3) pt 9 lw 1.5 t 'Classic',   \
    'gen.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'gen.CohortsEN2.csv'      u 2:($3) pt 13 lw 1.5 t 'Cohorts', \
    'gen.CTokenTurboELA.csv'    u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo'

set output 'int.eps'
plot \
    'int.OrecLazy.csv'               u 2:($3) pt 9 lw 1.5 t 'Classic',   \
    'int.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'int.CohortsEN2.csv'     u 2:($3) pt 13 lw 1.5 t 'Cohorts', \
    'int.CTokenTurboELA.csv'      u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo'

set output 'khi.eps'
plot \
    'khi.OrecLazy.csv'               u 2:($3) pt 9 lw 1.5 t 'Classic',   \
    'khi.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'khi.CohortsEN2.csv'     u 2:($3) pt 13 lw 1.5 t 'Cohorts',\
    'khi.CTokenTurboELA.csv'      u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo'

set output 'klo.eps'
plot \
    'klo.OrecLazy.csv'               u 2:($3) pt 9 lw 1.5 t 'Classic',   \
    'klo.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'klo.CohortsEN2.csv'     u 2:($3) pt 13 lw 1.5 t 'Cohorts',\
    'klo.CTokenTurboELA.csv'      u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo'

set output 'ssc.eps'
plot \
    'ssc.OrecLazy.csv'               u 2:($3) pt 9 lw 1.5 t 'Classic',   \
    'ssc.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'ssc.CohortsEN2.csv'          u 2:($3) pt 13 lw 1.5 t 'Cohorts',   \
    'ssc.CTokenTurboELA.csv'      u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo'
    
set output 'vhi.eps'
plot \
    'vhi.OrecLazy.csv'               u 2:($3) pt 9 lw 1.5 t 'Classic',   \
    'vhi.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'vhi.CohortsEN2.csv'     u 2:($3) pt 13 lw 1.5 t 'Cohorts',\
    'vhi.CTokenTurboELA.csv'      u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo'
    
set output 'vlo.eps'
plot \
    'vlo.OrecLazy.csv'               u 2:($3) pt 9 lw 1.5 t 'Classic',   \
    'vlo.Fastlane1.csv'         u 2:($3) pt 4 lw 1.5 t 'Fastlane1',   \
    'vlo.CohortsEN2.csv'     u 2:($3) pt 13 lw 1.5 t 'Cohorts',\
    'vlo.CTokenTurboELA.csv'      u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo'
    





set output 'gen.Cohorts.eps'
plot \
    'gen.CohortsEN2.csv'              u 2:($3) pt 9 lw 1.5 t 'NOrecInplace',   \
    'gen.CohortsEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecInplace',  \
    'gen.CohortsNOrec.csv'           u 2:($3) pt 3 lw 1.5 t 'NOrec',   \
    'gen.Cohorts.csv'                u 2:($3) pt 4 lw 1.5 t 'Orec'

set output 'int.Cohorts.eps'
plot \
    'int.CohortsEN2.csv'              u 2:($3) pt 9 lw 1.5 t 'NOrecInplace',   \
    'int.CohortsEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecInplace',  \
    'int.CohortsNOrec.csv'           u 2:($3) pt 3 lw 1.5 t 'NOrec',   \
    'int.Cohorts.csv'                u 2:($3) pt 4 lw 1.5 t 'Orec'

set output 'khi.Cohorts.eps'
plot \
    'khi.CohortsEN2.csv'              u 2:($3) pt 9 lw 1.5 t 'NOrecInplace',   \
    'khi.CohortsEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecInplace',  \
    'khi.CohortsNOrec.csv'           u 2:($3) pt 3 lw 1.5 t 'NOrec',   \
    'khi.Cohorts.csv'                u 2:($3) pt 4 lw 1.5 t 'Orec'

set output 'klo.Cohorts.eps'
plot \
    'klo.CohortsEN2.csv'              u 2:($3) pt 9 lw 1.5 t 'NOrecInplace',   \
    'klo.CohortsEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecInplace',  \
    'klo.CohortsNOrec.csv'           u 2:($3) pt 3 lw 1.5 t 'NOrec',   \
    'klo.Cohorts.csv'                u 2:($3) pt 4 lw 1.5 t 'Orec'

set output 'ssc.Cohorts.eps'
plot \
    'ssc.CohortsEN2.csv'              u 2:($3) pt 9 lw 1.5 t 'NOrecInplace',   \
    'ssc.CohortsEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecInplace',  \
    'ssc.CohortsNOrec.csv'           u 2:($3) pt 3 lw 1.5 t 'NOrec',   \
    'ssc.Cohorts.csv'                u 2:($3) pt 4 lw 1.5 t 'Orec'

set output 'vhi.Cohorts.eps'
plot \
    'vhi.CohortsEN2.csv'              u 2:($3) pt 9 lw 1.5 t 'NOrecInplace',   \
    'vhi.CohortsEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecInplace',  \
    'vhi.CohortsNOrec.csv'           u 2:($3) pt 3 lw 1.5 t 'NOrec',   \
    'vhi.Cohorts.csv'                u 2:($3) pt 4 lw 1.5 t 'Orec'

set output 'vlo.Cohorts.eps'
plot \
    'vlo.CohortsEN2.csv'              u 2:($3) pt 9 lw 1.5 t 'NOrecInplace',   \
    'vlo.CohortsEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecInplace',  \
    'vlo.CohortsNOrec.csv'           u 2:($3) pt 3 lw 1.5 t 'NOrec',   \
    'vlo.Cohorts.csv'                u 2:($3) pt 4 lw 1.5 t 'Orec'









set output 'gen.Classic.eps'
plot \
    'gen.LLT.csv'              u 2:($3) pt 9 lw 1.5 t 'TL2',   \
    'gen.NOrec.csv'             u 2:($3) pt 6 lw 1.5 t 'NOrec',  \
    'gen.OrecEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecEager',  \
    'gen.OrecLazy.csv'                u 2:($3) pt 4 lw 1.5 t 'OrecLazy'

set output 'int.Classic.eps'
plot \
    'int.LLT.csv'              u 2:($3) pt 9 lw 1.5 t 'TL2',   \
    'int.NOrec.csv'             u 2:($3) pt 6 lw 1.5 t 'NOrec',  \
    'int.OrecEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecEager',  \
    'int.OrecLazy.csv'                u 2:($3) pt 4 lw 1.5 t 'OrecLazy'

set output 'khi.Classic.eps'
plot \
    'khi.LLT.csv'              u 2:($3) pt 9 lw 1.5 t 'TL2',   \
    'khi.NOrec.csv'             u 2:($3) pt 6 lw 1.5 t 'NOrec',  \
    'khi.OrecEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecEager',  \
    'khi.OrecLazy.csv'                u 2:($3) pt 4 lw 1.5 t 'OrecLazy'

set output 'klo.Classic.eps'
plot \
    'klo.LLT.csv'              u 2:($3) pt 9 lw 1.5 t 'TL2',   \
    'klo.NOrec.csv'             u 2:($3) pt 6 lw 1.5 t 'NOrec',  \
    'klo.OrecEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecEager',  \
    'klo.OrecLazy.csv'                u 2:($3) pt 4 lw 1.5 t 'OrecLazy'

set output 'ssc.Classic.eps'
plot \
    'ssc.LLT.csv'              u 2:($3) pt 9 lw 1.5 t 'TL2',   \
    'ssc.NOrec.csv'             u 2:($3) pt 6 lw 1.5 t 'NOrec',  \
    'ssc.OrecEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecEager',  \
    'ssc.OrecLazy.csv'                u 2:($3) pt 4 lw 1.5 t 'OrecLazy'

set output 'vhi.Classic.eps'
plot \
    'vhi.LLT.csv'              u 2:($3) pt 9 lw 1.5 t 'TL2',   \
    'vhi.NOrec.csv'             u 2:($3) pt 6 lw 1.5 t 'NOrec',  \
    'vhi.OrecEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecEager',  \
    'vhi.OrecLazy.csv'                u 2:($3) pt 4 lw 1.5 t 'OrecLazy'

set output 'vlo.Classic.eps'
plot \
    'vlo.LLT.csv'              u 2:($3) pt 9 lw 1.5 t 'TL2',   \
    'vlo.NOrec.csv'             u 2:($3) pt 6 lw 1.5 t 'NOrec',  \
    'vlo.OrecEager.csv'           u 2:($3) pt 15 lw 1.5 t 'OrecEager',  \
    'vlo.OrecLazy.csv'                u 2:($3) pt 4 lw 1.5 t 'OrecLazy'



