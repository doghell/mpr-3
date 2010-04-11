
let command = locate("testMpr") + " --filter mpr.api.event --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
