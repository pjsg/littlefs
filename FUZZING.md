# Fuzzing littlefs

There is a standalone test program `test_afl.c` designed for fuzzing with
[AFL](http://lcamtuf.coredump.cx/afl/). This automatically exercises the 
littlefs API and verifies that the file system does not crash or interact incorrectly
with the flash chip. 

There are two steps to fuzzing. The first is to build the test program with
the AFL version of gcc. 

```
make test_afl
```

The `afl/test_afl` and `afl/test` programs read from stdin a list of commands
and arguments. These are interpreted and executed on the API. These two programs
are identical, except that `afl/test` does not require that `afl` is installed.

The second is to run this test program under afl as follows (where findings is 
the output directory):

```
afl-fuzz -i afltests -o /dev/shm/findings afl/test_afl
```

In the invocation about, the output directory is set to /dev/shm/findings. This is in RAM on my system and so you don't do lots
of real disk I/O during these tests. If your drive is an SSD, then this will save lots of writes to the 
SSD -- which is a good thing! You can specify any directory that you want -- but it should be empty before the start of the run.

This run will take hours (or days) and will (hopefully) not find any crashes.
If a crash (or hang) is found, then the input file that caused the crash is 
saved. This allows the specific test case to be debugged.

## Reducing the size of the file

AFL comes with `afl-tmin` which can reduce the size of the test input file to
make it easier to debug.

```
afl-tmin -i findings/crashes/<somefile> -o smalltest -- afl/test_afl
```

This will write a short version of the testcase file to `smalltest`. This can then be
fed into the test program for debugging:

```
afl/test print < smalltest
```

This should still crash, but allows it to be run under a debugger. The print argument
causes the test program to print out the sequence of operations being performed. 

## aflresults/

This directory contains input files that can be fed into `test_afl` and which exhibit the
crashes (or assertion failures). E.g.

```
$ afl/test < aflresults/prog 
Trying to program 0x70 into location with value 0x10
a.out: bd/lfs_rambd.c:116: lfs_rambd_prog: Assertion `(current & new_value) == new_value' failed.
Aborted
$
```

This can be debugged in `gdb` or whatever your favorite debugger is. 


## Notes

* Only the file portion of the API is exercised. It does not try and handle directories or attributes.

* The code is really hacky.

* As of v2.1.4, there are a number of test cases in aflresults that demonstrate various failures.

* The driver code simulates power failures happening at *any* time. In particular, it uses the model
of writing bytes one at a time to the flash, and then, if the power failure happens *during* the write
of a byte, then only some of the bits may be cleared. 
