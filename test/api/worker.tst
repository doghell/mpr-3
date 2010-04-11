
let command = locate("testMpr") + " --filter mpr.api.worker --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
