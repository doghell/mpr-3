
let command = locate("testMpr") + " --filter mpr.api.cmd --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
