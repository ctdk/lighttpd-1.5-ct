#!/usr/bin/perl


print "Content-Type: text/plain\r\n\r\n";

print $ENV{$ENV{"QUERY_STRING"}};
