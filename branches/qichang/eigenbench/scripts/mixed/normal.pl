#!/usr/bin/perl

$NAME = "normal";
@FREQ = (0.05, 0.2, 0.5, 0.2, 0.05);
@LEN  = (  10,   55, 100, 145,  190);

require "./dist_main.pl";

generate_distribute();


