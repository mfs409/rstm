#!/usr/bin/perl

$NAME = "bimodal";
@FREQ = ( 0.1, 0.3, 0.1,  0.1, 0.3, 0.1);
@LEN  = (   5,  10,  15,  160, 180, 200);

require "./dist_main.pl";

generate_distribute();


