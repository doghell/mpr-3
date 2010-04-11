
let command = locate("testMpr") + " --filter mpr.api.list --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
