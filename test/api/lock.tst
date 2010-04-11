
let command = locate("testMpr") + " --filter mpr.api.lock --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
