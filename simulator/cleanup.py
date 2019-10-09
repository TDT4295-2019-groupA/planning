import re

def get_it(name_of_file, name_to_write):
    file = open(name_of_file, "r")
    result = []
    for line in file:
        #templine = re.sub("step_n_samples\([0-9]*\)\n", "", line)
        templine = re.sub("send_spi\(\[", "", line)
        templine = re.sub("step_n_samples\(", "", templine)
        templine = re.sub("\]?\)", "", templine)
        templine = re.sub("\n", "", templine)
        templine = re.sub("0x", "", templine)
        templine = re.split(",\ ", templine)
        stringbuild = ""
        if(templine[0] == "01"):
            #print(templine)
            for x in range(15, -1, -1):
                stringbuild += str(templine[x+9])
            for x in range(6, -1, -1):
                stringbuild += str(templine[x+3])
            stringbuild += str(templine[2]) + str(templine[1]) 
            stringbuild += str(templine[0])
        elif(templine[0] == "02"):
            stringbuild += str(templine[11])
            stringbuild += str(templine[10])
            stringbuild += str(templine[9])
            for x in range(0, 4):
                stringbuild += str(templine[x+5])
            stringbuild += str(templine[4])
            stringbuild += str(templine[3])
            stringbuild += str(templine[2]) + str(templine[1])
            stringbuild += str(templine[0])
        else:
            stringbuild += "skip:" + str(templine[0])
        if (stringbuild != ""):
            result.append(stringbuild)
    file.close()
    file = open(name_to_write, "w")
    newresult = []
    for x in result:
        if(x[0] == "s"):
            newresult.append(x)
        else:
            newresult.append(int(x, 16))
    for x in newresult:
        file.write(str(x) + "\n")
    file.close()

read_name = input("insert name of file to clean: ")
write_name = input("insert name of file to write to: ")
get_it(read_name, write_name)
