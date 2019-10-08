import re

def get_it(name_of_file, name_to_write):
    file = open(name_of_file, "r")
    result = []
    for line in file:
        templine = re.sub("step_n_samples\([0-9]*\)\n", "", line)
        templine = re.sub("0x", "", templine)
        templine = re.sub(",\ ", "", templine)
        templine = re.sub("send_spi\(\[", "", templine)
        templine = re.sub("\]\)", "", templine)
        templine = re.sub("\n", "", templine)
        if (templine != ""):
            result.append(templine)
            print(templine)
    file.close()
    file = open(name_to_write, "w")
    newresult = []
    for x in result:
        newresult += [int(x, 16)]
    for x in newresult:
        file.write(str(x) + "\n")
    file.close()

read_name = input("insert name of file to clean: ")
write_name = input("insert name of file to write to: ")
get_it(read_name, write_name)
