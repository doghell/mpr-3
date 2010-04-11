/*
 *  thread.tst - Multithreaded test of the MPR API
 */

//  cygwin isn't reliable with multithreaded yet with cygwin 1.7

if (test.multithread && test.os != "CYGWIN") {
    let command = locate("testMpr") + " --iterations 5 " + test.mapVerbosity(-1)

    for (i = 1; i < (test.depth * 2); i += 2) {
        testCmdNoCapture(command + "--threads " + i)
    }

} else {
    test.skip("Run if multithreaded")
}
