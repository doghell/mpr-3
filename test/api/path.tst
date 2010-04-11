
let command = locate("testMpr") + " --filter mpr.api.path --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
