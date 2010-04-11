/*
 *  load.tst - API Load tests
 */

if (test.depth >= 3) {
    let command = locate("testMpr") + " --iterations 400 " + test.mapVerbosity(-1)

    testCmdNoCapture(command)
    if (test.multithread) {
        testCmdNoCapture(command + "--threads " + 2)
    }
    if (test.multithread) {
        for each (count in [2, 4, 8, 16]) {
            testCmdNoCapture(command + "--threads " + count)
        }
    }

} else {
    test.skip("Runs at depth 3")
}
