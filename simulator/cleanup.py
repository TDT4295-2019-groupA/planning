import re

def get_it(name_of_file, name_to_write):
    file = open(name_of_file, "r")
    result = []
    for line in file:
        templine = line.strip().replace("0x", "").replace("step_n_samples", "")
        templine = re.sub("[0-9A-F],\ ", "", templine)
        templine = re.sub("\([0-9]*\)", "", templine)
        templine = re.split("send_spi\(\[", templine)
        for x in templine:
            if x != "":
                result.append(x.replace("])", ""))
                #result += x.replace("])", "")
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
