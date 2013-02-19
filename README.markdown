FAND - a fan controller hack
============================

CAVEAT
------

This code is experimental and my break your hardware. *Use at your own risk!*

What is this?
-------------

This is a small hack for my laptop. It uses the FreeBSD `acpi_ibm` and
`coretemp` modules to throttle the fan level according to the core temperatures
(using a very basic algorithm).

Why was it written?
-------------------

The fan of my laptop tends to refuse to throttle down, even when the cpu cores
are rather cool. This tool fixes this issue.

And to be honest I was curious how to do this.

TODO
----

 * Logging
 * The thresholds for the fan levels have to be fine tuned.
