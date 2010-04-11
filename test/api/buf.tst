
let command = locate("testMpr") + " --filter mpr.api.buf --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
