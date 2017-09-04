#!/usr/bin/env python3
# Boomerang test functional test runner
# ARGV[0] test_executable
# ARGV[1] platform
# ARGV[2] test
# ARGV[3] test-set
# ARGV[4] options
# ARGV[5] parameters to the recompiled executable


import os
import subprocess
import shutil
import sys
import time
import operator
from collections import defaultdict


print("Boomerang Regression tester 0.0.1\n")

TESTS_DIR = "." + os.sep + "tests"
TEST_INPUT = os.path.join(TESTS_DIR, "inputs")
FAILED_COMMANDLINES = ""


'''
  Perform the actual test.
'''
def perform_test(exepath, test_file, output_path, args):
    log_name  = output_path
    file_size = os.path.getsize(test_file)
    upper_dir = os.sep.join(output_path.split(os.sep)[:-1])
    cmdline   = ['-P', os.getcwd(), '-o', upper_dir] + args + [test_file]

    test_stdout = open(log_name+".stdout", "w")
    test_stderr = open(log_name+".stderr", "w")

    start_t = time.time()

    try:
        result = subprocess.call([exepath]+cmdline, stdout=test_stdout, stderr=test_stderr, timeout=20)
    except:
        result = 1

    end_t = time.time()
    test_stdout.close()
    test_stdout.close()

    sys.stdout.write('.' if (result==0) else '!')
    sys.stdout.flush()
    return [result == 0, ' '.join(cmdline), test_file, float(file_size)/(end_t-start_t)]


'''
  Test the decompiler with all files in a specific directory.
'''
def test_all_inputs_in(base_dir, dirname=""):
    if dirname != "":
        sys.stdout.write("\nTesting in " + os.path.join(base_dir, "inputs", dirname))

    current_dir = os.path.join(base_dir, dirname)
    input_dir = os.path.join(base_dir, "inputs", dirname)
    output_dir = os.path.join(base_dir, "outputs", dirname)
    machine = ""

    if dirname != "":
        machine = dirname.split(os.sep)[0]  # assumption here is that inputs are always in /inputs/<machine_name>

    for f in os.listdir(input_dir):
        source = os.path.join(base_dir, "inputs", dirname, f)
        if os.path.isdir(source):
            test_all_inputs_in(base_dir,os.path.join(dirname,f)) # recurse
        else:
            test_path = source
            result_path = os.path.join(base_dir,"outputs",dirname,f)
            try:
                os.makedirs(result_path)
            except:
                pass


            test_res = perform_test(sys.argv[1], source, result_path, sys.argv[2:])

            assert(not test_res[0] or os.path.isfile(os.path.join(output_dir, "boomerang.log")))
            try:
                shutil.move(os.path.join(output_dir, "boomerang.log"), os.path.join(output_dir, f+".log"))
            except:
                 pass

            if not test_res[0]:
                crashes[machine].append([source,test_res[1]])
            elif test_res[3] != None:
                times[test_res[2]] = test_res[3]


if os.path.isdir(os.path.join(TESTS_DIR, "outputs_prev")):
    shutil.rmtree(os.path.join(TESTS_DIR, "outputs_prev"))

if os.path.isdir(os.path.join(TESTS_DIR, "outputs")):
    shutil.move(os.path.join(TESTS_DIR, "outputs"),os.path.join(TESTS_DIR, "outputs_prev"))

#exit(1)
#sh -c "./boomerang -o functest $4 test/$1/$2 2>/dev/null >/dev/null"
crashes = defaultdict(list)
times = {}

test_all_inputs_in(TESTS_DIR)
print("\n")

for machine, crash_list in crashes.items():
    numCrashes = len(crash_list)
    print("\nEncountered " + str(numCrashes) + " program failures for " + machine)
    for test in crash_list:
        print("Decompiler failed on " + test[0] + " - " + str(test[1]))

sorted_times = sorted(times.items(), key = operator.itemgetter(1), reverse = True)
print("\nSlowest run in bytes/sec " + sorted_times[0][0] + " - " + str(int(sorted_times[0][1]/1000)) + " kBytes/sec")
