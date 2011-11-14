#!/usr/bin/perl

$NAME = "longtail";
@FREQ = ( 0.5,  0.2, 0.05, 0.05, 0.05, 0.05);
@LEN  = (  10,   50,  100,  200,  500, 1000);

require "./dist_main.pl";

generate_distribute();


