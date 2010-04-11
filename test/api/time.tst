
let command = locate("testMpr") + " --filter mpr.api.time --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
