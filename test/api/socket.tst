
let command = locate("testMpr") + " --filter mpr.api.socket --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
