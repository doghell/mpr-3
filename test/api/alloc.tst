
let command = locate("testMpr") + " --filter mpr.api.alloc --iterations 2 " + test.mapVerbosity(-1)
testCmdNoCapture(command)
